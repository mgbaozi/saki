#ifndef SAKI_TRANSPORT_H
#define SAKI_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "saki_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAKI_TRANSPORT_SESSION_CAPACITY 37U
#define SAKI_TRANSPORT_USB_GRACE_MS 2000U

typedef enum {
    SAKI_TRANSPORT_NONE = 0,
    SAKI_TRANSPORT_BLE,
    SAKI_TRANSPORT_USB,
} saki_transport_id_t;

typedef enum {
    SAKI_TRANSPORT_APPLIED = 0,
    SAKI_TRANSPORT_STALE,
    SAKI_TRANSPORT_BUSY,
    SAKI_TRANSPORT_APPLY_FAILED,
    SAKI_TRANSPORT_INVALID,
} saki_transport_result_t;

typedef struct {
    saki_transport_result_t result;
    bool has_last_seq;
    uint32_t last_seq;
} saki_transport_outcome_t;

typedef struct {
    uint32_t switches;
    uint32_t rejections;
} saki_transport_diagnostics_t;

typedef esp_err_t (*saki_transport_apply_fn)(
    const saki_state_snapshot_t *snapshot,
    void *context
);

typedef struct {
    saki_transport_id_t active;
    saki_transport_id_t last_applied_transport;
    bool has_session;
    bool has_last_seq;
    bool usb_grace_active;
    uint32_t last_seq;
    uint64_t usb_grace_deadline_ms;
    char session[SAKI_TRANSPORT_SESSION_CAPACITY];
    saki_transport_diagnostics_t diagnostics;
    saki_transport_apply_fn apply;
    void *apply_context;
} saki_transport_manager_t;

void saki_transport_manager_init(
    saki_transport_manager_t *manager,
    saki_transport_apply_fn apply,
    void *apply_context
);

saki_transport_outcome_t saki_transport_manager_submit(
    saki_transport_manager_t *manager,
    saki_transport_id_t transport,
    const char *session,
    uint32_t sequence,
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
);

bool saki_transport_manager_link_down(
    saki_transport_manager_t *manager,
    saki_transport_id_t transport,
    uint64_t now_ms
);

bool saki_transport_manager_tick(
    saki_transport_manager_t *manager,
    uint64_t now_ms
);

const char *saki_transport_name(saki_transport_id_t transport);
void saki_transport_manager_get_diagnostics(
    const saki_transport_manager_t *manager,
    saki_transport_diagnostics_t *diagnostics
);

#ifdef __cplusplus
}
#endif

#endif
