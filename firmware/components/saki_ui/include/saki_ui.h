#ifndef SAKI_UI_H
#define SAKI_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "saki_button_policy.h"
#include "saki_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*saki_ui_button_fn)(saki_button_event_t event, void *context);

typedef enum {
    SAKI_UI_BLE_NO_BOND = 0,
    SAKI_UI_BLE_ALREADY_BONDED,
    SAKI_UI_BLE_PAUSED,
    SAKI_UI_BLE_WAITING,
    SAKI_UI_BLE_PAIRING_WINDOW,
    SAKI_UI_BLE_CONNECTING,
    SAKI_UI_BLE_CONNECTED,
    SAKI_UI_BLE_BONDS_CLEARED,
    SAKI_UI_BLE_PAIRING_EXPIRED,
    SAKI_UI_BLE_UNAVAILABLE,
} saki_ui_ble_notice_t;

void saki_ui_set_button_callback(
    saki_ui_button_fn callback,
    void *context
);
esp_err_t saki_ui_notify_ble(
    saki_ui_ble_notice_t notice,
    uint32_t remaining_ms
);
esp_err_t saki_ui_start(const saki_state_snapshot_t *initial_state, bool demo_mode);
uint32_t saki_ui_stack_high_watermark_bytes(void);
esp_err_t saki_ui_submit(const saki_state_snapshot_t *snapshot);
esp_err_t saki_ui_submit_display(const saki_display_snapshot_t *display);
esp_err_t saki_ui_submit_tracked(
    const saki_state_snapshot_t *snapshot,
    bool *overwrote_pending
);

#ifdef __cplusplus
}
#endif

#endif
