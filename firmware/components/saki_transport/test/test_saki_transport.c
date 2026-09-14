#include <string.h>

#include "saki_transport.h"
#include "unity.h"

#define SESSION_A "00000000-0000-4000-8000-000000000001"
#define SESSION_B "00000000-0000-4000-8000-000000000002"

typedef struct {
    uint32_t apply_count;
    bool fail_apply;
    saki_state_snapshot_t latest;
} transport_capture_t;

static esp_err_t capture_apply(
    const saki_state_snapshot_t *snapshot,
    void *context
)
{
    transport_capture_t *capture = context;

    if (capture->fail_apply) {
        return ESP_FAIL;
    }
    ++capture->apply_count;
    saki_state_snapshot_copy(&capture->latest, snapshot);
    return ESP_OK;
}

static void initialize(
    saki_transport_manager_t *manager,
    transport_capture_t *capture
)
{
    memset(capture, 0, sizeof(*capture));
    saki_transport_manager_init(manager, capture_apply, capture);
}

static saki_transport_outcome_t submit(
    saki_transport_manager_t *manager,
    saki_transport_id_t transport,
    const char *session,
    uint32_t sequence,
    uint64_t now_ms
)
{
    saki_state_snapshot_t snapshot;

    saki_state_snapshot_init(&snapshot);
    snapshot.state = SAKI_AGENT_WORKING;
    return saki_transport_manager_submit(
        manager,
        transport,
        session,
        sequence,
        &snapshot,
        now_ms
    );
}

TEST_CASE("USB rejects BLE state while authoritative", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_outcome_t outcome;

    initialize(&manager, &capture);
    outcome = submit(&manager, SAKI_TRANSPORT_USB, SESSION_A, 1, 100);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_APPLIED, outcome.result);
    TEST_ASSERT_EQUAL_STRING("USB", capture.latest.transport);

    outcome = submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 2, 200);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_BUSY, outcome.result);
    TEST_ASSERT_EQUAL_UINT32(1, capture.apply_count);
    TEST_ASSERT_EQUAL_UINT32(1, outcome.last_seq);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_USB, manager.active);
}

TEST_CASE("USB atomically takes over active BLE", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_diagnostics_t diagnostics;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 1, 100).result
    );
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_USB, SESSION_A, 2, 200).result
    );
    TEST_ASSERT_EQUAL_UINT32(2, capture.apply_count);
    TEST_ASSERT_EQUAL_STRING("USB", capture.latest.transport);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_USB, manager.active);
    saki_transport_manager_get_diagnostics(&manager, &diagnostics);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.switches);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.rejections);
}

TEST_CASE("sequence de-duplication spans transports", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_outcome_t outcome;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, UINT32_MAX, 100).result
    );
    outcome = submit(&manager, SAKI_TRANSPORT_USB, SESSION_A, UINT32_MAX, 200);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_STALE, outcome.result);
    TEST_ASSERT_EQUAL_UINT32(1, capture.apply_count);

    outcome = submit(&manager, SAKI_TRANSPORT_USB, SESSION_A, 0, 300);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_APPLIED, outcome.result);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.last_seq);
    TEST_ASSERT_EQUAL_STRING("USB", capture.latest.transport);
}

TEST_CASE("USB disconnect grace delays BLE takeover", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_diagnostics_t diagnostics;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_USB, SESSION_A, 1, 100).result
    );
    TEST_ASSERT_FALSE(saki_transport_manager_link_down(
        &manager,
        SAKI_TRANSPORT_USB,
        200
    ));
    TEST_ASSERT_TRUE(manager.usb_grace_active);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_BUSY,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 2, 2199).result
    );
    TEST_ASSERT_FALSE(saki_transport_manager_tick(&manager, 2199));
    TEST_ASSERT_TRUE(saki_transport_manager_tick(&manager, 2200));
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_NONE, manager.active);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 2, 2201).result
    );
    TEST_ASSERT_EQUAL_STRING("BLE", capture.latest.transport);
    saki_transport_manager_get_diagnostics(&manager, &diagnostics);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.switches);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.rejections);
}

TEST_CASE("new session commits only after successful apply", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_outcome_t outcome;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 10, 100).result
    );
    capture.fail_apply = true;
    outcome = submit(&manager, SAKI_TRANSPORT_USB, SESSION_B, 1, 200);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_APPLY_FAILED, outcome.result);
    TEST_ASSERT_EQUAL_STRING(SESSION_A, manager.session);
    TEST_ASSERT_EQUAL_UINT32(10, manager.last_seq);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_BLE, manager.active);

    capture.fail_apply = false;
    outcome = submit(&manager, SAKI_TRANSPORT_USB, SESSION_B, 1, 300);
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_APPLIED, outcome.result);
    TEST_ASSERT_EQUAL_STRING(SESSION_B, manager.session);
    TEST_ASSERT_EQUAL_UINT32(1, manager.last_seq);
}

TEST_CASE("active BLE disconnect becomes offline immediately", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 1, 100).result
    );
    TEST_ASSERT_TRUE(saki_transport_manager_link_down(
        &manager,
        SAKI_TRANSPORT_BLE,
        200
    ));
    TEST_ASSERT_EQUAL(SAKI_TRANSPORT_NONE, manager.active);
    TEST_ASSERT_FALSE(manager.usb_grace_active);
}

TEST_CASE("same transport reconnect is not counted as a switch", "[saki][transport]")
{
    saki_transport_manager_t manager;
    transport_capture_t capture;
    saki_transport_diagnostics_t diagnostics;

    initialize(&manager, &capture);
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 1, 100).result
    );
    TEST_ASSERT_TRUE(saki_transport_manager_link_down(
        &manager,
        SAKI_TRANSPORT_BLE,
        200
    ));
    TEST_ASSERT_EQUAL(
        SAKI_TRANSPORT_APPLIED,
        submit(&manager, SAKI_TRANSPORT_BLE, SESSION_A, 2, 300).result
    );
    saki_transport_manager_get_diagnostics(&manager, &diagnostics);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.switches);
}
