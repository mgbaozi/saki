#ifndef SAKI_UI_H
#define SAKI_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "saki_model.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t saki_ui_start(const saki_state_snapshot_t *initial_state, bool demo_mode);
uint32_t saki_ui_stack_high_watermark_bytes(void);
esp_err_t saki_ui_submit(const saki_state_snapshot_t *snapshot);
esp_err_t saki_ui_submit_tracked(
    const saki_state_snapshot_t *snapshot,
    bool *overwrote_pending
);

#ifdef __cplusplus
}
#endif

#endif
