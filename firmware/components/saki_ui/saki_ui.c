#include "saki_ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aw9523b.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd.h"
#include "lvgl.h"
#include "my_spi.h"
#include "myiic.h"
#include "saki_font_cjk_16.h"
#include "saki_ui_policy.h"
#include "touch.h"

#define SAKI_UI_TASK_STACK_BYTES 10240
#define SAKI_UI_TASK_PRIORITY    5
#define SAKI_UI_TICK_PERIOD_US   1000
#define SAKI_UI_HANDLER_MS       10
#define SAKI_UI_DEMO_PERIOD_MS   3000
#define SAKI_PROGRESS_ANIM_MS    1100
#define SAKI_PROGRESS_SEGMENT    24
#define SAKI_DETAIL_TIMEOUT_MS   15000
#define SAKI_DETAIL_LEFT         12
#define SAKI_DETAIL_RIGHT        308
#define SAKI_DETAIL_TOP          88
#define SAKI_DETAIL_BOTTOM       194

static const char *TAG = "saki_ui";

static QueueHandle_t s_state_queue;
static bool s_demo_mode;
static TaskHandle_t s_task_handle;
static saki_state_snapshot_t s_current_snapshot;
static saki_ui_policy_t s_policy;

static lv_obj_t *s_status_dot;
static lv_obj_t *s_agent_label;
static lv_obj_t *s_transport_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_elapsed_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_activity_card;
static lv_obj_t *s_activity_label;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_model_label;
static lv_obj_t *s_dimming_overlay;
static bool s_progress_animating;

static lv_disp_draw_buf_t s_display_buffer;
static lv_disp_drv_t s_display_driver;
static lv_font_t s_font_14;
static lv_font_t s_font_16;

static void saki_handle_tap(lv_coord_t x, lv_coord_t y);

static void saki_fonts_init(void)
{
    s_font_14 = lv_font_montserrat_14;
    s_font_14.fallback = &saki_font_cjk_16;
    if (s_font_14.line_height < saki_font_cjk_16.line_height) {
        s_font_14.line_height = saki_font_cjk_16.line_height;
    }
    if (s_font_14.base_line < saki_font_cjk_16.base_line) {
        s_font_14.base_line = saki_font_cjk_16.base_line;
    }

    s_font_16 = lv_font_montserrat_16;
    s_font_16.fallback = &saki_font_cjk_16;
    if (s_font_16.line_height < saki_font_cjk_16.line_height) {
        s_font_16.line_height = saki_font_cjk_16.line_height;
    }
    if (s_font_16.base_line < saki_font_cjk_16.base_line) {
        s_font_16.base_line = saki_font_cjk_16.base_line;
    }
}

static void saki_lvgl_tick(void *argument)
{
    (void)argument;
    lv_tick_inc(1);
}

static void saki_display_flush(
    lv_disp_drv_t *driver,
    const lv_area_t *area,
    lv_color_t *color_map
)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)driver->user_data;
    int32_t width = area->x2 - area->x1 + 1;
    int32_t height = area->y2 - area->y1 + 1;

    for (int32_t index = 0; index < width * height; ++index) {
        uint16_t color = color_map[index].full;
        color_map[index].full = (color << 8) | (color >> 8);
    }

    esp_lcd_panel_draw_bitmap(
        panel,
        area->x1,
        area->y1,
        area->x2 + 1,
        area->y2 + 1,
        color_map
    );
    lv_disp_flush_ready(driver);
}

static void saki_touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    static lv_coord_t last_x;
    static lv_coord_t last_y;
    static bool was_pressed;
    bool pressed;
    (void)driver;

    tp_dev.scan(0);
    pressed = (tp_dev.sta & TP_PRES_DOWN) != 0;
    if (pressed) {
        last_x = tp_dev.x[0];
        last_y = tp_dev.y[0];
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
        if (was_pressed) {
            saki_handle_tap(last_x, last_y);
        }
    }
    was_pressed = pressed;
    data->point.x = last_x;
    data->point.y = last_y;
}

static esp_err_t saki_display_init(void)
{
    lv_color_t *buffer_one;
    lv_color_t *buffer_two;
    static lv_indev_drv_t input_driver;

    my_spi_init();
    myiic_init();
    aw9523b_init();
    lcd_init();
    tp_dev.init();

    buffer_one = heap_caps_malloc(
        lcddev.width * 60 * sizeof(lv_color_t),
        MALLOC_CAP_DMA
    );
    buffer_two = heap_caps_malloc(
        lcddev.width * 60 * sizeof(lv_color_t),
        MALLOC_CAP_DMA
    );
    if (buffer_one == NULL || buffer_two == NULL) {
        free(buffer_one);
        free(buffer_two);
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(
        &s_display_buffer,
        buffer_one,
        buffer_two,
        lcddev.width * 60
    );
    lv_disp_drv_init(&s_display_driver);
    s_display_driver.hor_res = lcddev.width;
    s_display_driver.ver_res = lcddev.height;
    s_display_driver.flush_cb = saki_display_flush;
    s_display_driver.draw_buf = &s_display_buffer;
    s_display_driver.user_data = panel_handle;
    lv_disp_drv_register(&s_display_driver);

    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = saki_touch_read;
    lv_indev_drv_register(&input_driver);
    return ESP_OK;
}

static lv_color_t saki_state_color(const saki_state_snapshot_t *snapshot)
{
    if (!snapshot->connected) {
        return lv_color_hex(0x64748B);
    }
    switch (snapshot->state) {
        case SAKI_AGENT_STARTING:
            return lv_color_hex(0x3B82F6);
        case SAKI_AGENT_THINKING:
            return lv_color_hex(0xA855F7);
        case SAKI_AGENT_WORKING:
            return lv_color_hex(0x06B6D4);
        case SAKI_AGENT_WAITING_USER:
        case SAKI_AGENT_WAITING_APPROVAL:
            return lv_color_hex(0xF59E0B);
        case SAKI_AGENT_COMPLETED:
            return lv_color_hex(0x22C55E);
        case SAKI_AGENT_FAILED:
            return lv_color_hex(0xEF4444);
        case SAKI_AGENT_CANCELLED:
            return lv_color_hex(0x94A3B8);
        case SAKI_AGENT_IDLE:
        default:
            return lv_color_hex(0x60A5FA);
    }
}

static void saki_set_plain_panel(lv_obj_t *object, lv_color_t color)
{
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static void saki_label_base(lv_obj_t *label, lv_color_t color)
{
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &s_font_14, 0);
}

static void saki_create_screen(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_t *footer = lv_obj_create(screen);

    saki_set_plain_panel(screen, lv_color_hex(0x0B1020));

    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 320, 28);
    saki_set_plain_panel(header, lv_color_hex(0x111A2E));

    s_agent_label = lv_label_create(header);
    saki_label_base(s_agent_label, lv_color_hex(0xE2E8F0));
    lv_label_set_text(s_agent_label, "Agent");
    lv_obj_set_pos(s_agent_label, 12, 7);

    s_transport_label = lv_label_create(header);
    saki_label_base(s_transport_label, lv_color_hex(0x93C5FD));
    lv_label_set_text(s_transport_label, "OFFLINE");
    lv_obj_align(s_transport_label, LV_ALIGN_RIGHT_MID, -12, 0);

    s_status_dot = lv_obj_create(screen);
    lv_obj_set_pos(s_status_dot, 16, 43);
    lv_obj_set_size(s_status_dot, 38, 38);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(screen);
    saki_label_base(s_status_label, lv_color_hex(0xF8FAFC));
    lv_obj_set_style_text_font(s_status_label, &s_font_16, 0);
    lv_obj_set_pos(s_status_label, 68, 42);
    lv_obj_set_width(s_status_label, 165);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);

    s_elapsed_label = lv_label_create(screen);
    saki_label_base(s_elapsed_label, lv_color_hex(0x94A3B8));
    lv_obj_set_pos(s_elapsed_label, 244, 44);
    lv_obj_set_width(s_elapsed_label, 62);
    lv_obj_set_style_text_align(s_elapsed_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_title_label = lv_label_create(screen);
    saki_label_base(s_title_label, lv_color_hex(0xF8FAFC));
    lv_obj_set_style_text_font(s_title_label, &s_font_16, 0);
    lv_obj_set_pos(s_title_label, 16, 91);
    lv_obj_set_size(s_title_label, 288, 40);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_DOT);

    s_activity_card = lv_obj_create(screen);
    lv_obj_set_pos(s_activity_card, 12, 137);
    lv_obj_set_size(s_activity_card, 296, 57);
    lv_obj_set_style_bg_color(s_activity_card, lv_color_hex(0x151F36), 0);
    lv_obj_set_style_bg_opa(s_activity_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_activity_card, 1, 0);
    lv_obj_set_style_border_color(s_activity_card, lv_color_hex(0x253553), 0);
    lv_obj_set_style_radius(s_activity_card, 8, 0);
    lv_obj_set_style_pad_all(s_activity_card, 10, 0);
    lv_obj_clear_flag(s_activity_card, LV_OBJ_FLAG_SCROLLABLE);

    s_activity_label = lv_label_create(s_activity_card);
    saki_label_base(s_activity_label, lv_color_hex(0xCBD5E1));
    lv_obj_set_width(s_activity_label, 274);
    lv_label_set_long_mode(s_activity_label, LV_LABEL_LONG_DOT);

    s_progress_bar = lv_bar_create(screen);
    lv_obj_set_pos(s_progress_bar, 16, 201);
    lv_obj_set_size(s_progress_bar, 238, 8);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x253553), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x06B6D4), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    s_progress_label = lv_label_create(screen);
    saki_label_base(s_progress_label, lv_color_hex(0x94A3B8));
    lv_obj_set_pos(s_progress_label, 260, 197);
    lv_obj_set_width(s_progress_label, 44);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_set_pos(footer, 0, 216);
    lv_obj_set_size(footer, 320, 24);
    saki_set_plain_panel(footer, lv_color_hex(0x111A2E));

    s_model_label = lv_label_create(footer);
    saki_label_base(s_model_label, lv_color_hex(0x64748B));
    lv_obj_set_pos(s_model_label, 12, 5);
    lv_obj_set_width(s_model_label, 296);
    lv_label_set_long_mode(s_model_label, LV_LABEL_LONG_DOT);

    s_dimming_overlay = lv_obj_create(screen);
    lv_obj_set_pos(s_dimming_overlay, 0, 0);
    lv_obj_set_size(s_dimming_overlay, 320, 240);
    saki_set_plain_panel(s_dimming_overlay, lv_color_black());
}

static void saki_format_elapsed(uint64_t elapsed_ms, char *buffer, size_t capacity)
{
    uint64_t seconds = elapsed_ms / 1000;
    uint64_t minutes = seconds / 60;
    snprintf(buffer, capacity, "%02" PRIu64 ":%02" PRIu64, minutes, seconds % 60);
}

static void saki_refresh_elapsed(
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms,
    uint64_t *displayed_seconds,
    bool force
)
{
    char elapsed[24];
    uint64_t elapsed_ms = saki_state_elapsed_at(snapshot, now_ms);
    uint64_t seconds = elapsed_ms / 1000;

    if (!force && seconds == *displayed_seconds) {
        return;
    }
    saki_format_elapsed(elapsed_ms, elapsed, sizeof(elapsed));
    lv_label_set_text(s_elapsed_label, elapsed);
    *displayed_seconds = seconds;
}

static void saki_progress_anim_exec(void *object, int32_t start)
{
    lv_obj_t *bar = object;

    lv_bar_set_start_value(bar, start, LV_ANIM_OFF);
    lv_bar_set_value(bar, start + SAKI_PROGRESS_SEGMENT, LV_ANIM_OFF);
}

static void saki_progress_animation_stop(void)
{
    if (!s_progress_animating) {
        return;
    }
    lv_anim_del(s_progress_bar, saki_progress_anim_exec);
    s_progress_animating = false;
}

static void saki_progress_animation_start(void)
{
    lv_anim_t animation;

    if (s_progress_animating) {
        return;
    }
    lv_bar_set_mode(s_progress_bar, LV_BAR_MODE_RANGE);
    lv_bar_set_value(s_progress_bar, SAKI_PROGRESS_SEGMENT, LV_ANIM_OFF);
    lv_bar_set_start_value(s_progress_bar, 0, LV_ANIM_OFF);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_progress_bar);
    lv_anim_set_values(&animation, 0, 100 - SAKI_PROGRESS_SEGMENT);
    lv_anim_set_exec_cb(&animation, saki_progress_anim_exec);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_time(&animation, SAKI_PROGRESS_ANIM_MS);
    lv_anim_set_playback_time(&animation, SAKI_PROGRESS_ANIM_MS);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
    s_progress_animating = true;
}

static void saki_apply_text_view(const saki_state_snapshot_t *snapshot)
{
    const char *summary;
    bool prioritize_detail;

    summary = snapshot->activity[0]
        ? snapshot->activity
        : "Connect the Mac Host to begin.";
    prioritize_detail = snapshot->detail[0] != '\0' &&
        (snapshot->state == SAKI_AGENT_WAITING_USER ||
         snapshot->state == SAKI_AGENT_WAITING_APPROVAL ||
         snapshot->state == SAKI_AGENT_FAILED);

    if (s_policy.detail_visible && snapshot->detail[0] != '\0') {
        lv_obj_add_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_activity_card, 12, 88);
        lv_obj_set_size(s_activity_card, 296, 106);
        lv_obj_set_size(s_activity_label, 274, 84);
        lv_label_set_text_fmt(s_activity_label, "DETAIL\n%s", snapshot->detail);
        return;
    }

    lv_obj_clear_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_activity_card, 12, 137);
    lv_obj_set_size(s_activity_card, 296, 57);
    lv_obj_set_size(s_activity_label, 274, 36);
    lv_label_set_text(
        s_title_label,
        snapshot->task_title[0] ? snapshot->task_title : "Waiting for a task"
    );
    lv_label_set_text(
        s_activity_label,
        prioritize_detail ? snapshot->detail : summary
    );
}

static void saki_apply_brightness(void)
{
    uint8_t opacity = saki_ui_policy_dimming_opacity(
        s_policy.backlight_percent
    );

    lv_obj_set_style_bg_opa(s_dimming_overlay, opacity, 0);
    lv_obj_move_foreground(s_dimming_overlay);
    ESP_LOGI(
        TAG,
        "visual brightness=%u%% opacity=%u",
        s_policy.backlight_percent,
        opacity
    );
}

static void saki_apply_snapshot(const saki_state_snapshot_t *snapshot)
{
    char progress[16];
    lv_color_t state_color = saki_state_color(snapshot);

    lv_obj_set_style_bg_color(s_status_dot, state_color, 0);
    lv_obj_set_style_shadow_color(s_status_dot, state_color, 0);
    lv_obj_set_style_shadow_width(s_status_dot, 12, 0);
    lv_obj_set_style_shadow_opa(s_status_dot, LV_OPA_30, 0);

    lv_label_set_text(s_agent_label, snapshot->agent_name[0] ? snapshot->agent_name : "Agent");
    lv_label_set_text(
        s_transport_label,
        snapshot->connected ? snapshot->transport : "OFFLINE"
    );
    lv_obj_set_style_text_color(s_transport_label, state_color, 0);
    lv_label_set_text(
        s_status_label,
        snapshot->connected ? saki_state_display_name(snapshot->state) : "DISCONNECTED"
    );

    saki_apply_text_view(snapshot);
    lv_label_set_text(
        s_model_label,
        snapshot->model_name[0] ? snapshot->model_name : "Saki protocol v1"
    );

    if (!snapshot->connected || snapshot->progress_mode == SAKI_PROGRESS_NONE) {
        saki_progress_animation_stop();
        lv_bar_set_mode(s_progress_bar, LV_BAR_MODE_NORMAL);
        lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_progress_label, LV_OBJ_FLAG_HIDDEN);
    } else if (snapshot->progress_mode == SAKI_PROGRESS_INDETERMINATE) {
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_progress_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_progress_label, "...");
        saki_progress_animation_start();
    } else {
        uint8_t percent = snapshot->progress_percent;
        saki_progress_animation_stop();
        lv_bar_set_mode(s_progress_bar, LV_BAR_MODE_NORMAL);
        if (percent > 100) {
            percent = 100;
        }
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_progress_label, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_progress_bar, percent, LV_ANIM_ON);
        snprintf(progress, sizeof(progress), "%u%%", percent);
        lv_label_set_text(s_progress_label, progress);
    }

    lv_obj_set_style_border_color(s_activity_card, state_color, 0);
}

static void saki_apply_policy_changes(uint32_t changes)
{
    if ((changes & SAKI_UI_POLICY_BACKLIGHT_CHANGED) != 0) {
        saki_apply_brightness();
    }
    if ((changes & SAKI_UI_POLICY_VIEW_CHANGED) != 0) {
        saki_apply_text_view(&s_current_snapshot);
    }
}

static void saki_handle_tap(lv_coord_t x, lv_coord_t y)
{
    bool in_detail_region = x >= SAKI_DETAIL_LEFT && x < SAKI_DETAIL_RIGHT &&
        y >= SAKI_DETAIL_TOP && y < SAKI_DETAIL_BOTTOM;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint32_t changes = saki_ui_policy_on_tap(
        &s_policy,
        &s_current_snapshot,
        in_detail_region,
        now_ms
    );

    ESP_LOGD(
        TAG,
        "tap x=%d y=%d region=%d changes=0x%" PRIx32,
        (int)x,
        (int)y,
        in_detail_region,
        changes
    );
    saki_apply_policy_changes(changes);
}

static void saki_demo_snapshot(size_t index, saki_state_snapshot_t *snapshot)
{
    static const size_t demo_slot_count = 10;
    static const saki_agent_state_t states[] = {
        SAKI_AGENT_IDLE,
        SAKI_AGENT_STARTING,
        SAKI_AGENT_THINKING,
        SAKI_AGENT_WORKING,
        SAKI_AGENT_WAITING_USER,
        SAKI_AGENT_WAITING_APPROVAL,
        SAKI_AGENT_COMPLETED,
        SAKI_AGENT_FAILED,
        SAKI_AGENT_CANCELLED,
    };
    static const char *activities[] = {
        "Connected. No active task.",
        "Preparing the workspace",
        "Planning the next step",
        "Building the ESP32 firmware",
        "More information is required",
        "Permission is required to continue",
        "All requested work is complete",
        "The build failed",
        "The task was cancelled",
    };
    size_t state_index = index % demo_slot_count;

    saki_state_snapshot_init(snapshot);
    if (state_index == demo_slot_count - 1) {
        snprintf(snapshot->agent_name, sizeof(snapshot->agent_name), "%s", "Codex");
        snprintf(
            snapshot->task_title,
            sizeof(snapshot->task_title),
            "%s",
            "Implement the Saki Agent status display"
        );
        snprintf(
            snapshot->activity,
            sizeof(snapshot->activity),
            "%s",
            "Connection lost. Waiting for the Mac Host."
        );
        snapshot->state = SAKI_AGENT_WORKING;
        snapshot->elapsed_ms = 333000;
        return;
    }

    snapshot->connected = true;
    snapshot->state = states[state_index];
    snprintf(snapshot->transport, sizeof(snapshot->transport), "%s", "DEMO");
    snprintf(snapshot->agent_name, sizeof(snapshot->agent_name), "%s", "Codex");
    snprintf(snapshot->model_name, sizeof(snapshot->model_name), "%s", "gpt-5.6");
    snprintf(snapshot->activity, sizeof(snapshot->activity), "%s", activities[state_index]);
    snapshot->elapsed_ms = state_index * 37000;

    if (snapshot->state != SAKI_AGENT_IDLE) {
        snprintf(snapshot->task_id, sizeof(snapshot->task_id), "%s", "demo-task");
        snprintf(
            snapshot->task_title,
            sizeof(snapshot->task_title),
            "%s",
            "Implement the Saki Agent status display"
        );
        snapshot->progress_mode = SAKI_PROGRESS_INDETERMINATE;
    }
    if (snapshot->state == SAKI_AGENT_WORKING) {
        snapshot->progress_mode = SAKI_PROGRESS_DETERMINATE;
        snapshot->progress_percent = 62;
    } else if (snapshot->state == SAKI_AGENT_COMPLETED) {
        snapshot->progress_mode = SAKI_PROGRESS_DETERMINATE;
        snapshot->progress_percent = 100;
    } else if (saki_state_is_terminal(snapshot->state)) {
        snapshot->progress_mode = SAKI_PROGRESS_NONE;
    }
}

static void saki_ui_task(void *argument)
{
    saki_state_snapshot_t update;
    TickType_t last_demo_tick = xTaskGetTickCount();
    uint64_t displayed_elapsed_seconds = UINT64_MAX;
    uint64_t now_ms;
    uint32_t policy_changes;
    size_t demo_index = 0;
    esp_timer_handle_t tick_timer;
    const saki_ui_policy_config_t policy_config = {
        .active_percent = CONFIG_SAKI_BACKLIGHT_ACTIVE_PERCENT,
        .idle_percent = CONFIG_SAKI_BACKLIGHT_IDLE_PERCENT,
        .disconnected_percent = CONFIG_SAKI_BACKLIGHT_DISCONNECTED_PERCENT,
        .idle_timeout_ms = CONFIG_SAKI_BACKLIGHT_IDLE_TIMEOUT_SECONDS * UINT64_C(1000),
        .disconnected_timeout_ms =
            CONFIG_SAKI_BACKLIGHT_DISCONNECTED_TIMEOUT_SECONDS * UINT64_C(1000),
        .detail_timeout_ms = SAKI_DETAIL_TIMEOUT_MS,
    };
    const esp_timer_create_args_t tick_arguments = {
        .callback = saki_lvgl_tick,
        .name = "saki_lvgl_tick",
    };
    (void)argument;

    lv_init();
    saki_fonts_init();
    ESP_ERROR_CHECK(saki_display_init());
    ESP_ERROR_CHECK(esp_timer_create(&tick_arguments, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, SAKI_UI_TICK_PERIOD_US));
    saki_create_screen();

    now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    saki_ui_policy_init(&s_policy, &policy_config, now_ms);
    saki_state_snapshot_init(&s_current_snapshot);
    if (xQueueReceive(s_state_queue, &s_current_snapshot, 0) != pdTRUE) {
        s_current_snapshot.connected = false;
    }
    saki_apply_snapshot(&s_current_snapshot);
    saki_apply_brightness();
    saki_refresh_elapsed(
        &s_current_snapshot,
        now_ms,
        &displayed_elapsed_seconds,
        true
    );

    while (true) {
        if (xQueueReceive(s_state_queue, &update, 0) == pdTRUE) {
            now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            saki_state_snapshot_copy(&s_current_snapshot, &update);
            policy_changes = saki_ui_policy_on_snapshot(
                &s_policy,
                &s_current_snapshot,
                now_ms
            );
            saki_apply_snapshot(&s_current_snapshot);
            saki_apply_policy_changes(
                policy_changes & SAKI_UI_POLICY_BACKLIGHT_CHANGED
            );
            saki_refresh_elapsed(
                &s_current_snapshot,
                now_ms,
                &displayed_elapsed_seconds,
                true
            );
        }

        if (s_demo_mode &&
            xTaskGetTickCount() - last_demo_tick >= pdMS_TO_TICKS(SAKI_UI_DEMO_PERIOD_MS)) {
            now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            saki_demo_snapshot(demo_index++, &s_current_snapshot);
            s_current_snapshot.received_at_ms = now_ms;
            policy_changes = saki_ui_policy_on_snapshot(
                &s_policy,
                &s_current_snapshot,
                now_ms
            );
            saki_apply_snapshot(&s_current_snapshot);
            saki_apply_policy_changes(
                policy_changes & SAKI_UI_POLICY_BACKLIGHT_CHANGED
            );
            saki_refresh_elapsed(
                &s_current_snapshot,
                now_ms,
                &displayed_elapsed_seconds,
                true
            );
            last_demo_tick = xTaskGetTickCount();
        }

        now_ms = (uint64_t)(esp_timer_get_time() / 1000);
        policy_changes = saki_ui_policy_tick(
            &s_policy,
            &s_current_snapshot,
            now_ms
        );
        saki_apply_policy_changes(policy_changes);
        saki_refresh_elapsed(
            &s_current_snapshot,
            now_ms,
            &displayed_elapsed_seconds,
            false
        );

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(SAKI_UI_HANDLER_MS));
    }
}

esp_err_t saki_ui_start(const saki_state_snapshot_t *initial_state, bool demo_mode)
{
    BaseType_t created;

    if (s_state_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state_queue = xQueueCreate(1, sizeof(saki_state_snapshot_t));
    if (s_state_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (initial_state != NULL) {
        xQueueOverwrite(s_state_queue, initial_state);
    }
    s_demo_mode = demo_mode;
    created = xTaskCreate(
        saki_ui_task,
        "saki_ui",
        SAKI_UI_TASK_STACK_BYTES,
        NULL,
        SAKI_UI_TASK_PRIORITY,
        &s_task_handle
    );
    if (created != pdPASS) {
        vQueueDelete(s_state_queue);
        s_state_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Saki UI task started (demo=%d)", demo_mode);
    return ESP_OK;
}

uint32_t saki_ui_stack_high_watermark_bytes(void)
{
    TaskHandle_t task = s_task_handle;

    return task == NULL ? 0 : (uint32_t)uxTaskGetStackHighWaterMark(task);
}

esp_err_t saki_ui_submit(const saki_state_snapshot_t *snapshot)
{
    return saki_ui_submit_tracked(snapshot, NULL);
}

esp_err_t saki_ui_submit_tracked(
    const saki_state_snapshot_t *snapshot,
    bool *overwrote_pending
)
{
    bool pending;

    if (overwrote_pending != NULL) {
        *overwrote_pending = false;
    }
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    pending = uxQueueMessagesWaiting(s_state_queue) > 0;
    if (xQueueOverwrite(s_state_queue, snapshot) != pdPASS) {
        return ESP_FAIL;
    }
    if (overwrote_pending != NULL) {
        *overwrote_pending = pending;
    }
    return ESP_OK;
}
