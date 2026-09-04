#ifndef SAKI_USB_H
#define SAKI_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*saki_usb_rx_fn)(
    const uint8_t *data,
    size_t length,
    void *context
);
typedef void (*saki_usb_connection_fn)(bool connected, void *context);
typedef void (*saki_usb_poll_fn)(void *context);

esp_err_t saki_usb_start(
    const char *serial_number,
    saki_usb_rx_fn receive,
    saki_usb_connection_fn connection_changed,
    saki_usb_poll_fn poll,
    void *context
);
esp_err_t saki_usb_transmit(
    const uint8_t *data,
    size_t length,
    void *context
);
bool saki_usb_connected(void);
uint32_t saki_usb_stack_high_watermark_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
