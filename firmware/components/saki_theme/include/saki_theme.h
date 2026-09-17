#ifndef SAKI_THEME_H
#define SAKI_THEME_H

#include <stddef.h>

#include "esp_err.h"
#include "lvgl.h"
#include "saki_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAKI_THEME_COPY_OFF = 0,
    SAKI_THEME_COPY_LIGHT,
} saki_theme_copy_mode_t;

typedef struct {
    const char *const *items;
    size_t count;
} saki_theme_copy_set_t;

typedef struct {
    const char *id;
    const char *display_name;
    const lv_img_dsc_t *icons[SAKI_AGENT_CANCELLED + 1];
    saki_theme_copy_set_t copy[SAKI_AGENT_CANCELLED + 1];
} saki_theme_pack_t;

size_t saki_theme_pack_count(void);
const saki_theme_pack_t *saki_theme_pack_at(size_t index);
const saki_theme_pack_t *saki_theme_find(const char *id);
const saki_theme_pack_t *saki_theme_default(void);
const saki_theme_pack_t *saki_theme_next(const saki_theme_pack_t *pack);
const lv_img_dsc_t *saki_theme_icon(
    const saki_theme_pack_t *pack,
    saki_agent_state_t state
);
const char *saki_theme_copy(
    const saki_theme_pack_t *pack,
    saki_theme_copy_mode_t mode,
    saki_agent_state_t state,
    const char *task_id,
    const char *run_id,
    const char *activity_key
);
void saki_theme_preferences_load(
    const saki_theme_pack_t **pack,
    saki_theme_copy_mode_t *mode
);
esp_err_t saki_theme_preferences_save(
    const saki_theme_pack_t *pack,
    saki_theme_copy_mode_t mode
);

#ifdef __cplusplus
}
#endif

#endif
