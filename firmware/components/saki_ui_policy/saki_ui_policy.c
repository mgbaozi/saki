#include "saki_ui_policy.h"

#include <string.h>
#include <stdio.h>

int saki_session_view_update(saki_session_view_t *view,
    const saki_display_snapshot_t *display, uint64_t now_ms)
{
    if (!display->multi_session || display->count == 0) {
        memset(view, 0, sizeof(*view));
        return 0;
    }
    if (strcmp(view->latest_id, display->items[0].task_id) != 0 ||
        strcmp(view->latest_run, display->run_ids[0]) != 0 || now_ms >= view->deadline_ms) {
        view->selected_id[0] = '\0';
    }
    snprintf(view->latest_id, sizeof(view->latest_id), "%s", display->items[0].task_id);
    snprintf(view->latest_run, sizeof(view->latest_run), "%s", display->run_ids[0]);
    for (uint8_t i = 1; i < display->count; ++i) {
        if (strcmp(view->selected_id, display->items[i].task_id) == 0) return i;
    }
    view->selected_id[0] = '\0';
    return 0;
}

int saki_session_view_select(saki_session_view_t *view,
    const saki_display_snapshot_t *display, int index, uint64_t now_ms)
{
    (void)saki_session_view_update(view, display, now_ms);
    view->selected_id[0] = '\0';
    if (display->multi_session && index > 0 && index < display->count) {
        snprintf(view->selected_id, sizeof(view->selected_id), "%s", display->items[index].task_id);
        view->deadline_ms = now_ms + 15000;
        return index;
    }
    return 0;
}

static bool saki_ui_timeout_reached(
    uint64_t now_ms,
    uint64_t started_ms,
    uint64_t timeout_ms
)
{
    return now_ms >= started_ms && now_ms - started_ms >= timeout_ms;
}

void saki_ui_policy_init(
    saki_ui_policy_t *policy,
    const saki_ui_policy_config_t *config,
    uint64_t now_ms
)
{
    if (policy == NULL || config == NULL) {
        return;
    }

    memset(policy, 0, sizeof(*policy));
    policy->config = *config;
    policy->backlight_percent = config->active_percent;
    policy->last_activity_ms = now_ms;
}

uint32_t saki_ui_policy_on_snapshot(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
)
{
    uint32_t changes = SAKI_UI_POLICY_NO_CHANGE;

    if (policy == NULL || snapshot == NULL) {
        return changes;
    }

    policy->last_activity_ms = now_ms;
    if (policy->backlight_percent != policy->config.active_percent) {
        policy->backlight_percent = policy->config.active_percent;
        changes |= SAKI_UI_POLICY_BACKLIGHT_CHANGED;
    }
    if (policy->detail_visible && snapshot->detail[0] == '\0') {
        policy->detail_visible = false;
        policy->detail_opened_ms = 0;
        changes |= SAKI_UI_POLICY_VIEW_CHANGED;
    }
    return changes;
}

uint32_t saki_ui_policy_on_tap(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    bool in_detail_region,
    uint64_t now_ms
)
{
    if (policy == NULL || snapshot == NULL) {
        return SAKI_UI_POLICY_NO_CHANGE;
    }

    policy->last_activity_ms = now_ms;
    if (policy->backlight_percent != policy->config.active_percent) {
        policy->backlight_percent = policy->config.active_percent;
        return SAKI_UI_POLICY_BACKLIGHT_CHANGED;
    }
    if (!in_detail_region || snapshot->detail[0] == '\0') {
        return SAKI_UI_POLICY_NO_CHANGE;
    }

    policy->detail_visible = !policy->detail_visible;
    policy->detail_opened_ms = policy->detail_visible ? now_ms : 0;
    return SAKI_UI_POLICY_VIEW_CHANGED;
}

uint32_t saki_ui_policy_on_local_activity(
    saki_ui_policy_t *policy,
    uint64_t now_ms
)
{
    if (policy == NULL) {
        return SAKI_UI_POLICY_NO_CHANGE;
    }
    policy->last_activity_ms = now_ms;
    if (policy->backlight_percent == policy->config.active_percent) {
        return SAKI_UI_POLICY_NO_CHANGE;
    }
    policy->backlight_percent = policy->config.active_percent;
    return SAKI_UI_POLICY_BACKLIGHT_CHANGED;
}

uint32_t saki_ui_policy_tick(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
)
{
    uint32_t changes = SAKI_UI_POLICY_NO_CHANGE;
    uint8_t target_percent;

    if (policy == NULL || snapshot == NULL) {
        return changes;
    }

    if (policy->detail_visible &&
        saki_ui_timeout_reached(
            now_ms,
            policy->detail_opened_ms,
            policy->config.detail_timeout_ms
        )) {
        policy->detail_visible = false;
        policy->detail_opened_ms = 0;
        changes |= SAKI_UI_POLICY_VIEW_CHANGED;
    }

    target_percent = policy->config.active_percent;
    if (!snapshot->connected &&
        saki_ui_timeout_reached(
            now_ms,
            policy->last_activity_ms,
            policy->config.disconnected_timeout_ms
        )) {
        target_percent = policy->config.disconnected_percent;
    } else if (snapshot->connected && snapshot->state == SAKI_AGENT_IDLE &&
               saki_ui_timeout_reached(
                   now_ms,
                   policy->last_activity_ms,
                   policy->config.idle_timeout_ms
               )) {
        target_percent = policy->config.idle_percent;
    }

    if (target_percent != policy->backlight_percent) {
        policy->backlight_percent = target_percent;
        changes |= SAKI_UI_POLICY_BACKLIGHT_CHANGED;
    }
    return changes;
}

uint8_t saki_ui_policy_dimming_opacity(uint8_t brightness_percent)
{
    uint16_t dimming_percent;

    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    dimming_percent = 100U - brightness_percent;
    return (uint8_t)((dimming_percent * 255U + 50U) / 100U);
}
