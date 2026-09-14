#ifndef SAKI_BLE_H
#define SAKI_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "saki_button_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAKI_BLE_PAIRING_WINDOW_MS 120000U

typedef void (*saki_ble_rx_fn)(
    const uint8_t *data,
    size_t length,
    void *context
);
typedef void (*saki_ble_connection_fn)(bool ready, void *context);
typedef void (*saki_ble_poll_fn)(void *context);

typedef enum {
    SAKI_BLE_EVENT_NO_BOND = 0,
    SAKI_BLE_EVENT_ALREADY_BONDED,
    SAKI_BLE_EVENT_PAUSED,
    SAKI_BLE_EVENT_WAITING,
    SAKI_BLE_EVENT_PAIRING_WINDOW,
    SAKI_BLE_EVENT_CONNECTING,
    SAKI_BLE_EVENT_CONNECTED,
    SAKI_BLE_EVENT_BONDS_CLEARED,
    SAKI_BLE_EVENT_PAIRING_EXPIRED,
} saki_ble_event_t;

typedef void (*saki_ble_event_fn)(
    saki_ble_event_t event,
    uint32_t remaining_ms,
    void *context
);

typedef struct {
    uint32_t rx_drops;
    uint32_t tx_drops;
    uint32_t security_rejections;
    uint32_t pairing_rejections;
    uint32_t disconnects;
} saki_ble_diagnostics_t;

esp_err_t saki_ble_start(
    saki_ble_rx_fn receive,
    saki_ble_connection_fn connection_changed,
    saki_ble_poll_fn poll,
    saki_ble_event_fn event_received,
    void *context
);

esp_err_t saki_ble_transmit(
    const uint8_t *data,
    size_t length,
    void *context
);

esp_err_t saki_ble_button_event(saki_button_event_t event);
esp_err_t saki_ble_disconnect(void);
bool saki_ble_ready(void);
bool saki_ble_bond_present(void);
bool saki_ble_pairing_window_open(void);
uint32_t saki_ble_stack_high_watermark_bytes(void);
void saki_ble_get_diagnostics(saki_ble_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
