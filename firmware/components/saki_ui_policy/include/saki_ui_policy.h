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

uint32_t saki_ui_policy_on_local_activity(
    saki_ui_policy_t *policy,
    uint64_t now_ms
);

uint32_t saki_ui_policy_tick(
    saki_ui_policy_t *policy,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
);

uint8_t saki_ui_policy_dimming_opacity(uint8_t brightness_percent);

/* sessions[0] is the Host's latest submitted session. Local browsing is temporary. */
typedef struct {
    char latest_id[SAKI_TASK_ID_CAPACITY];
    char latest_run[33];
    char selected_id[SAKI_TASK_ID_CAPACITY];
    uint64_t deadline_ms;
} saki_session_view_t;

int saki_session_view_update(saki_session_view_t *view,
    const saki_display_snapshot_t *display, uint64_t now_ms);
int saki_session_view_select(saki_session_view_t *view,
    const saki_display_snapshot_t *display, int index, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
