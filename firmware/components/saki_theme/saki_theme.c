#include "saki_theme.h"

#include <stdint.h>
#include <string.h>

#include "nvs.h"
#include "saki_stage_assets.h"

#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))
#define COPY_SET(value) { .items = value, .count = ARRAY_SIZE(value) }

static const char *const s_idle_copy[] = {
    "还没开始？那就不开始。",
    "任务呢？一个都没有～",
};
static const char *const s_starting_copy[] = {
    "为什么要执行这个命令！",
    "要开始了？我可没答应。",
};
static const char *const s_thinking_copy[] = {
    "是这样没错，但不是这样。",
    "我懂了。大概懂了。",
};
static const char *const s_working_copy[] = {
    "我什么都会做的！",
    "都交给我吧，大概没问题。",
};
static const char *const s_waiting_user_copy[] = {
    "你愿意回复我一辈子吗？",
    "回答呢？回答去哪里了？",
};
static const char *const s_waiting_approval_copy[] = {
    "可以执行吗？真的可以吗？",
    "你不点允许，我可不敢动。",
};
static const char *const s_completed_copy[] = {
    "是这样没错，这次真对了。",
    "客服小祥，圆满下班！",
};
static const char *const s_failed_copy[] = {
    "全——都不会做～",
};
static const char *const s_cancelled_copy[] = {
    "任务，已经解散了。",
    "退出没错，但不是这样退。",
};

static const saki_theme_pack_t s_classic = {
    .id = "classic",
    .display_name = "经典",
};

static const saki_theme_pack_t s_saki_stage = {
    .id = "saki_stage",
    .display_name = "Saki Stage",
    .icons = {
        &saki_theme_saki_stage_idle,
        &saki_theme_saki_stage_starting,
        &saki_theme_saki_stage_thinking,
        &saki_theme_saki_stage_working,
        &saki_theme_saki_stage_waiting_user,
        &saki_theme_saki_stage_waiting_approval,
        &saki_theme_saki_stage_completed,
        &saki_theme_saki_stage_failed,
        &saki_theme_saki_stage_cancelled,
    },
    .copy = {
        COPY_SET(s_idle_copy),
        COPY_SET(s_starting_copy),
        COPY_SET(s_thinking_copy),
        COPY_SET(s_working_copy),
        COPY_SET(s_waiting_user_copy),
        COPY_SET(s_waiting_approval_copy),
        COPY_SET(s_completed_copy),
        COPY_SET(s_failed_copy),
        COPY_SET(s_cancelled_copy),
    },
};

static const saki_theme_pack_t *const s_packs[] = {
    &s_classic,
    &s_saki_stage,
};

static uint32_t saki_theme_hash_string(uint32_t hash, const char *value)
{
    if (value == NULL) return hash;
    while (*value != '\0') {
        hash ^= (uint8_t)*value++;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

size_t saki_theme_pack_count(void)
{
    return ARRAY_SIZE(s_packs);
}

const saki_theme_pack_t *saki_theme_pack_at(size_t index)
{
    return index < ARRAY_SIZE(s_packs) ? s_packs[index] : NULL;
}

const saki_theme_pack_t *saki_theme_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t index = 0; index < ARRAY_SIZE(s_packs); ++index) {
        if (strcmp(s_packs[index]->id, id) == 0) return s_packs[index];
    }
    return NULL;
}

const saki_theme_pack_t *saki_theme_default(void)
{
    return &s_saki_stage;
}

const saki_theme_pack_t *saki_theme_next(const saki_theme_pack_t *pack)
{
    for (size_t index = 0; index < ARRAY_SIZE(s_packs); ++index) {
        if (s_packs[index] == pack) return s_packs[(index + 1) % ARRAY_SIZE(s_packs)];
    }
    return saki_theme_default();
}

const lv_img_dsc_t *saki_theme_icon(const saki_theme_pack_t *pack, saki_agent_state_t state)
{
    if (pack == NULL || state < SAKI_AGENT_IDLE || state > SAKI_AGENT_CANCELLED) return NULL;
    return pack->icons[state];
}

const char *saki_theme_copy(const saki_theme_pack_t *pack,
    saki_theme_copy_mode_t mode,
    saki_agent_state_t state,
    const char *task_id,
    const char *run_id,
    const char *activity_key)
{
    uint32_t hash = UINT32_C(2166136261);
    const saki_theme_copy_set_t *set;

    if (pack == NULL || mode == SAKI_THEME_COPY_OFF ||
        state < SAKI_AGENT_IDLE || state > SAKI_AGENT_CANCELLED) return NULL;
    set = &pack->copy[state];
    if (set->items == NULL || set->count == 0) return NULL;
    hash = saki_theme_hash_string(hash, pack->id);
    hash = saki_theme_hash_string(hash, task_id);
    hash = saki_theme_hash_string(hash, run_id);
    hash = saki_theme_hash_string(hash, activity_key);
    hash ^= (uint32_t)state;
    hash *= UINT32_C(16777619);
    return set->items[hash % set->count];
}

void saki_theme_preferences_load(const saki_theme_pack_t **pack, saki_theme_copy_mode_t *mode)
{
    char pack_id[32] = {0};
    size_t pack_id_size = sizeof(pack_id);
    uint8_t stored_mode = SAKI_THEME_COPY_LIGHT;
    nvs_handle_t handle;

    if (pack == NULL || mode == NULL) return;
    *pack = saki_theme_default();
    *mode = SAKI_THEME_COPY_LIGHT;
    if (nvs_open("saki_theme", NVS_READONLY, &handle) != ESP_OK) return;
    if (nvs_get_str(handle, "pack", pack_id, &pack_id_size) == ESP_OK) {
        const saki_theme_pack_t *stored_pack = saki_theme_find(pack_id);
        if (stored_pack != NULL) *pack = stored_pack;
    }
    if (nvs_get_u8(handle, "copy", &stored_mode) == ESP_OK &&
        stored_mode <= SAKI_THEME_COPY_LIGHT) {
        *mode = (saki_theme_copy_mode_t)stored_mode;
    }
    nvs_close(handle);
}

esp_err_t saki_theme_preferences_save(const saki_theme_pack_t *pack,
    saki_theme_copy_mode_t mode)
{
    esp_err_t result;
    nvs_handle_t handle;

    if (pack == NULL || saki_theme_find(pack->id) != pack || mode > SAKI_THEME_COPY_LIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    result = nvs_open("saki_theme", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_str(handle, "pack", pack->id);
    if (result == ESP_OK) result = nvs_set_u8(handle, "copy", (uint8_t)mode);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}
