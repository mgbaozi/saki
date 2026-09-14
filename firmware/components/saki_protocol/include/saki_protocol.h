#ifndef SAKI_PROTOCOL_H
#define SAKI_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "saki_model.h"
#include "saki_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAKI_PROTOCOL_MAX_FRAME       2048
#define SAKI_PROTOCOL_TX_CAPACITY     1024
#define SAKI_PROTOCOL_SESSION_CAPACITY 37
#define SAKI_PROTOCOL_DEVICE_ID_CAPACITY 17
#define SAKI_PROTOCOL_FW_CAPACITY     17
#define SAKI_PROTOCOL_HEARTBEAT_TIMEOUT_MS 15000

typedef enum {
    SAKI_FRAMER_FRAME_TOO_LARGE = 1,
} saki_framer_error_t;

typedef void (*saki_framer_frame_fn)(
    const char *frame,
    size_t length,
    void *context
);
typedef void (*saki_framer_error_fn)(saki_framer_error_t error, void *context);

typedef struct {
    char frame[SAKI_PROTOCOL_MAX_FRAME + 1];
    size_t length;
    bool discarding;
} saki_ndjson_framer_t;

void saki_ndjson_framer_init(saki_ndjson_framer_t *framer);
void saki_ndjson_framer_feed(
    saki_ndjson_framer_t *framer,
    const uint8_t *data,
    size_t length,
    saki_framer_frame_fn frame_callback,
    saki_framer_error_fn error_callback,
    void *context
);

typedef esp_err_t (*saki_protocol_tx_fn)(
    const uint8_t *data,
    size_t length,
    void *context
);
typedef esp_err_t (*saki_protocol_state_fn)(
    const saki_state_snapshot_t *snapshot,
    void *context
);
typedef saki_transport_outcome_t (*saki_protocol_candidate_fn)(
    const saki_state_snapshot_t *snapshot,
    saki_transport_id_t transport,
    const char *session,
    uint32_t sequence,
    uint64_t now_ms,
    void *context
);

typedef enum {
    SAKI_PROTOCOL_CAPABILITY_NONE = 0,
    SAKI_PROTOCOL_CAPABILITY_BLE = 1U << 0,
    SAKI_PROTOCOL_CAPABILITY_SECURE_CONNECTION = 1U << 1,
    SAKI_PROTOCOL_CAPABILITY_TRANSPORT_ARBITRATION = 1U << 2,
} saki_protocol_capability_t;

typedef struct {
    uint32_t valid_frames;
    uint32_t invalid_frames;
    uint32_t oversized_frames;
    uint32_t old_sequences;
    uint32_t ui_queue_overwrites;
    uint32_t tx_drops;
    uint32_t heartbeat_timeouts;
} saki_protocol_diagnostics_t;

typedef struct {
    uint32_t heap_free_bytes;
    uint32_t heap_min_bytes;
    uint32_t internal_free_bytes;
    uint32_t internal_min_bytes;
    uint32_t app_stack_min_bytes;
    uint32_t ui_stack_min_bytes;
    uint32_t usb_stack_min_bytes;
    uint32_t ble_stack_min_bytes;
    uint32_t ble_rx_drops;
    uint32_t ble_tx_drops;
    uint32_t ble_security_rejections;
    uint32_t ble_pairing_rejections;
    uint32_t ble_disconnects;
    uint32_t transport_switches;
    uint32_t transport_rejections;
} saki_runtime_metrics_t;

typedef void (*saki_protocol_runtime_fn)(
    saki_runtime_metrics_t *metrics,
    void *context
);

typedef struct {
    saki_ndjson_framer_t framer;
    bool handshaken;
    bool has_last_seq;
    uint32_t last_seq;
    uint32_t next_id;
    saki_protocol_diagnostics_t diagnostics;
    uint8_t invalid_streak;
    bool has_last_activity;
    uint64_t last_activity_ms;
    char session[SAKI_PROTOCOL_SESSION_CAPACITY];
    char device_id[SAKI_PROTOCOL_DEVICE_ID_CAPACITY];
    char firmware_version[SAKI_PROTOCOL_FW_CAPACITY];
    saki_transport_id_t transport;
    uint32_t capability_flags;
    saki_protocol_tx_fn transmit;
    saki_protocol_state_fn apply_state;
    saki_protocol_candidate_fn submit_candidate;
    void *callback_context;
    saki_protocol_runtime_fn collect_runtime;
    void *runtime_context;
} saki_protocol_engine_t;

void saki_protocol_engine_init(
    saki_protocol_engine_t *engine,
    const char *device_id,
    const char *firmware_version,
    saki_protocol_tx_fn transmit,
    saki_protocol_state_fn apply_state,
    void *callback_context
);
void saki_protocol_engine_init_peer(
    saki_protocol_engine_t *engine,
    const char *device_id,
    const char *firmware_version,
    saki_transport_id_t transport,
    uint32_t capability_flags,
    saki_protocol_tx_fn transmit,
    saki_protocol_candidate_fn submit_candidate,
    void *callback_context
);
void saki_protocol_engine_disconnect(saki_protocol_engine_t *engine);
void saki_protocol_engine_set_runtime_provider(
    saki_protocol_engine_t *engine,
    saki_protocol_runtime_fn collect_runtime,
    void *runtime_context
);
void saki_protocol_engine_receive(
    saki_protocol_engine_t *engine,
    const uint8_t *data,
    size_t length
);
bool saki_protocol_engine_check_timeout(
    saki_protocol_engine_t *engine,
    uint64_t now_ms
);
void saki_protocol_engine_note_ui_overwrite(saki_protocol_engine_t *engine);
void saki_protocol_engine_get_diagnostics(
    const saki_protocol_engine_t *engine,
    saki_protocol_diagnostics_t *diagnostics
);

#ifdef __cplusplus
}
#endif

#endif
