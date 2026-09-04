#include "saki_ui_policy.h"

#include <string.h>

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
