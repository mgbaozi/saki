#ifndef SAKI_UI_POLICY_H
#define SAKI_UI_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "saki_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t active_percent;
    uint8_t idle_percent;
    uint8_t disconnected_percent;
    uint64_t idle_timeout_ms;
    uint64_t disconnected_timeout_ms;
    uint64_t detail_timeout_ms;
} saki_ui_policy_config_t;

typedef struct {
    saki_ui_policy_config_t config;
    bool detail_visible;
    uint8_t backlight_percent;
    uint64_t last_activity_ms;
    uint64_t detail_opened_ms;
} saki_ui_policy_t;

typedef enum {
    SAKI_UI_POLICY_NO_CHANGE = 0,
    SAKI_UI_POLICY_VIEW_CHANGED = 1 << 0,
    SAKI_UI_POLICY_BACKLIGHT_CHANGED = 1 << 1,
} saki_ui_policy_change_t;

void saki_ui_policy_init(
    saki_ui_policy_t *policy,
    const saki_ui_policy_config_t *config,
    uint64_t now_ms
);

uint32_t saki_ui_policy_on_snapshot(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
);

uint32_t saki_ui_policy_on_tap(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    bool in_detail_region,
    uint64_t now_ms
);

uint32_t saki_ui_policy_tick(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
);

uint8_t saki_ui_policy_dimming_opacity(uint8_t brightness_percent);

#ifdef __cplusplus
}
#endif

#endif
