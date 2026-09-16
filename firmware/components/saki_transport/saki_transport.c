#include "saki_transport.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool saki_transport_valid(saki_transport_id_t transport)
{
    return transport == SAKI_TRANSPORT_USB || transport == SAKI_TRANSPORT_BLE;
}

static uint8_t saki_transport_priority(saki_transport_id_t transport)
{
    switch (transport) {
    case SAKI_TRANSPORT_USB:
        return 2;
    case SAKI_TRANSPORT_BLE:
        return 1;
    case SAKI_TRANSPORT_NONE:
    default:
        return 0;
    }
}

static void saki_transport_increment(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        ++(*counter);
    }
}

static bool saki_transport_seq_is_new(
    const saki_transport_manager_t *manager,
    uint32_t sequence
)
{
    return !manager->has_last_seq ||
           (int32_t)(sequence - manager->last_seq) > 0;
}

static saki_transport_outcome_t saki_transport_outcome(
    const saki_transport_manager_t *manager,
    saki_transport_result_t result
)
{
    saki_transport_outcome_t outcome = {
        .result = result,
        .has_last_seq = false,
        .last_seq = 0,
    };

    if (manager != NULL) {
        outcome.has_last_seq = manager->has_last_seq;
        outcome.last_seq = manager->last_seq;
    }
    return outcome;
}

static bool saki_transport_expire_usb_grace(
    saki_transport_manager_t *manager,
    uint64_t now_ms
)
{
    if (!manager->usb_grace_active ||
        now_ms < manager->usb_grace_deadline_ms) {
        return false;
    }
    manager->usb_grace_active = false;
    manager->usb_grace_deadline_ms = 0;
    if (manager->active == SAKI_TRANSPORT_USB) {
        manager->active = SAKI_TRANSPORT_NONE;
        return true;
    }
    return false;
}

void saki_transport_manager_init(
    saki_transport_manager_t *manager,
    saki_transport_apply_fn apply,
    void *apply_context
)
{
    if (manager == NULL) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
    manager->apply = apply;
    manager->apply_context = apply_context;
}

saki_transport_outcome_t saki_transport_manager_submit(
    saki_transport_manager_t *manager,
    saki_transport_id_t transport,
    const char *session,
    uint32_t sequence,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
)
{
    saki_state_snapshot_t candidate;
    bool same_session;

    if (manager == NULL || !saki_transport_valid(transport) || session == NULL ||
        snapshot == NULL || manager->apply == NULL || session[0] == '\0' ||
        strlen(session) >= sizeof(manager->session)) {
        if (manager != NULL) {
            saki_transport_increment(&manager->diagnostics.rejections);
        }
        return saki_transport_outcome(manager, SAKI_TRANSPORT_INVALID);
    }

    (void)saki_transport_expire_usb_grace(manager, now_ms);
    if (manager->active != SAKI_TRANSPORT_NONE &&
        saki_transport_priority(transport) <
            saki_transport_priority(manager->active)) {
        saki_transport_increment(&manager->diagnostics.rejections);
        return saki_transport_outcome(manager, SAKI_TRANSPORT_BUSY);
    }

    same_session = manager->has_session &&
                   strcmp(session, manager->session) == 0;
    if (same_session && manager->multi_session) {
        return saki_transport_outcome(manager, SAKI_TRANSPORT_INVALID);
    }
    if (same_session && !saki_transport_seq_is_new(manager, sequence)) {
        return saki_transport_outcome(manager, SAKI_TRANSPORT_STALE);
    }

    saki_state_snapshot_copy(&candidate, snapshot);
    candidate.connected = true;
    snprintf(
        candidate.transport,
        sizeof(candidate.transport),
        "%s",
        saki_transport_name(transport)
    );
    if (manager->apply(&candidate, manager->apply_context) != ESP_OK) {
        saki_transport_increment(&manager->diagnostics.rejections);
        return saki_transport_outcome(manager, SAKI_TRANSPORT_APPLY_FAILED);
    }

    if (manager->has_session &&
        manager->last_applied_transport != SAKI_TRANSPORT_NONE &&
        manager->last_applied_transport != transport) {
        saki_transport_increment(&manager->diagnostics.switches);
    }
    snprintf(manager->session, sizeof(manager->session), "%s", session);
    manager->multi_session = false;
    manager->has_session = true;
    manager->last_seq = sequence;
    manager->has_last_seq = true;
    manager->active = transport;
    manager->last_applied_transport = transport;
    manager->usb_grace_active = false;
    manager->usb_grace_deadline_ms = 0;
    return saki_transport_outcome(manager, SAKI_TRANSPORT_APPLIED);
}

saki_transport_outcome_t saki_transport_manager_submit_display(
    saki_transport_manager_t *manager, saki_transport_id_t transport,
    const char *session, uint32_t sequence,
    const saki_display_snapshot_t *display, uint64_t now_ms
)
{
    if (manager == NULL || !saki_transport_valid(transport) || session == NULL ||
        display == NULL || manager->apply_display == NULL || session[0] == '\0' ||
        strlen(session) >= sizeof(manager->session) || display->count > SAKI_DISPLAY_MAX_SESSIONS) {
        return saki_transport_outcome(manager, SAKI_TRANSPORT_INVALID);
    }
    (void)saki_transport_expire_usb_grace(manager, now_ms);
    if (manager->active != SAKI_TRANSPORT_NONE &&
        saki_transport_priority(transport) < saki_transport_priority(manager->active)) {
        saki_transport_increment(&manager->diagnostics.rejections);
        return saki_transport_outcome(manager, SAKI_TRANSPORT_BUSY);
    }
    if (manager->has_session && strcmp(session, manager->session) == 0 &&
        !saki_transport_seq_is_new(manager, sequence)) {
        return saki_transport_outcome(manager, SAKI_TRANSPORT_STALE);
    }
    if (manager->apply_display(display, manager->apply_context) != ESP_OK) {
        return saki_transport_outcome(manager, SAKI_TRANSPORT_APPLY_FAILED);
    }
    if (manager->has_session && manager->last_applied_transport != SAKI_TRANSPORT_NONE &&
        manager->last_applied_transport != transport) {
        saki_transport_increment(&manager->diagnostics.switches);
    }
    snprintf(manager->session, sizeof(manager->session), "%s", session);
    manager->multi_session = true;
    manager->has_session = true;
    manager->has_last_seq = true;
    manager->last_seq = sequence;
    manager->active = transport;
    manager->last_applied_transport = transport;
    manager->usb_grace_active = false;
    manager->usb_grace_deadline_ms = 0;
    return saki_transport_outcome(manager, SAKI_TRANSPORT_APPLIED);
}

bool saki_transport_manager_link_down(
    saki_transport_manager_t *manager,
    saki_transport_id_t transport,
    uint64_t now_ms
)
{
    if (manager == NULL || manager->active != transport) {
        return false;
    }
    if (transport == SAKI_TRANSPORT_USB) {
        manager->usb_grace_active = true;
        manager->usb_grace_deadline_ms =
            now_ms > UINT64_MAX - SAKI_TRANSPORT_USB_GRACE_MS
                ? UINT64_MAX
                : now_ms + SAKI_TRANSPORT_USB_GRACE_MS;
        return false;
    }
    if (transport == SAKI_TRANSPORT_BLE) {
        manager->active = SAKI_TRANSPORT_NONE;
        return true;
    }
    return false;
}

bool saki_transport_manager_tick(
    saki_transport_manager_t *manager,
    uint64_t now_ms
)
{
    if (manager == NULL) {
        return false;
    }
    return saki_transport_expire_usb_grace(manager, now_ms);
}

const char *saki_transport_name(saki_transport_id_t transport)
{
    switch (transport) {
    case SAKI_TRANSPORT_USB:
        return "USB";
    case SAKI_TRANSPORT_BLE:
        return "BLE";
    case SAKI_TRANSPORT_NONE:
    default:
        return "OFFLINE";
    }
}

void saki_transport_manager_get_diagnostics(
    const saki_transport_manager_t *manager,
    saki_transport_diagnostics_t *diagnostics
)
{
    if (manager == NULL || diagnostics == NULL) {
        return;
    }
    *diagnostics = manager->diagnostics;
}
