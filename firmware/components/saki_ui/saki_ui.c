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
#include "saki_button_policy.h"
#include "saki_font_cjk_16.h"
#include "saki_ui_policy.h"
#include "touch.h"

#define SAKI_UI_TASK_STACK_BYTES 10240
#define SAKI_UI_TASK_PRIORITY    5
#define SAKI_UI_TICK_PERIOD_US   1000
#define SAKI_UI_HANDLER_MS       10
#define SAKI_UI_START_TIMEOUT_MS 10000
#define SAKI_UI_DEMO_PERIOD_MS   3000
#define SAKI_PROGRESS_ANIM_MS    1100
#define SAKI_PROGRESS_SEGMENT    24
#define SAKI_DETAIL_TIMEOUT_MS   15000
#define SAKI_DETAIL_LEFT         12
#define SAKI_DETAIL_RIGHT        308
#define SAKI_DETAIL_TOP          88
#define SAKI_DETAIL_BOTTOM       194
#define SAKI_BUTTON_POLL_MS      20
#define SAKI_BLE_NOTICE_MS       2400U
#define SAKI_UI_BLE_PAIRING_WINDOW_MS 120000U

typedef struct {
    saki_ui_ble_notice_t notice;
    uint32_t remaining_ms;
} saki_ui_ble_message_t;

static const char *TAG = "saki_ui";

static QueueHandle_t s_state_queue;
static QueueHandle_t s_ble_queue;
static bool s_demo_mode;
static TaskHandle_t s_task_handle;
static esp_err_t s_start_result;
static saki_state_snapshot_t s_current_snapshot;
static saki_display_snapshot_t s_current_display;
#define SAKI_SIDEBAR_LEFT 240
#define SAKI_SIDEBAR_TOP 28
#define SAKI_SIDEBAR_ROW_HEIGHT 62
#define SAKI_SIDEBAR_COUNT (SAKI_DISPLAY_MAX_SESSIONS - 1)
static bool s_sidebar_dirty = true;
static int s_selected_session;
static saki_session_view_t s_session_view;
static lv_obj_t *s_sidebar;
static lv_obj_t *s_sidebar_rows[SAKI_SIDEBAR_COUNT];
static lv_obj_t *s_sidebar_labels[SAKI_SIDEBAR_COUNT];
static lv_obj_t *s_sidebar_marks[SAKI_SIDEBAR_COUNT];
static int s_sidebar_indices[SAKI_SIDEBAR_COUNT];

static saki_ui_policy_t s_policy;
static saki_button_policy_t s_button_policy;
static saki_ui_button_fn s_button_callback;
static void *s_button_context;

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
static lv_obj_t *s_ble_overlay;
static lv_obj_t *s_ble_overlay_title;
static lv_obj_t *s_ble_overlay_detail;
static lv_obj_t *s_ble_overlay_progress;
static bool s_progress_animating;
static bool s_button_overlay_visible;
static bool s_ble_notice_visible;
static saki_ui_ble_notice_t s_ble_notice;
static uint64_t s_ble_notice_deadline_ms;
static uint64_t s_ble_pairing_deadline_ms;

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
    lv_obj_set_style_text_line_space(label, 0, 0);
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
    lv_obj_set_style_pad_all(s_activity_card, 7, 0);
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
    lv_obj_set_pos(s_model_label, 12, 2);
    lv_obj_set_width(s_model_label, 296);
    lv_label_set_long_mode(s_model_label, LV_LABEL_LONG_DOT);

    s_sidebar = lv_obj_create(screen);
    saki_set_plain_panel(s_sidebar, lv_color_hex(0x111A2E));
    lv_obj_set_pos(s_sidebar, SAKI_SIDEBAR_LEFT, SAKI_SIDEBAR_TOP);
    lv_obj_set_size(s_sidebar, 80, 188);
    for (uint8_t i = 0; i < SAKI_SIDEBAR_COUNT; ++i) {
        s_sidebar_indices[i] = -1;
        s_sidebar_rows[i] = lv_obj_create(s_sidebar);
        saki_set_plain_panel(s_sidebar_rows[i], lv_color_hex(0x151D30));
        lv_obj_set_pos(s_sidebar_rows[i], 4, 1 + i * SAKI_SIDEBAR_ROW_HEIGHT);
        lv_obj_set_size(s_sidebar_rows[i], 72, 60);
        s_sidebar_marks[i] = lv_obj_create(s_sidebar_rows[i]);
        saki_set_plain_panel(s_sidebar_marks[i], lv_color_hex(0x64748B));
        lv_obj_set_pos(s_sidebar_marks[i], 0, 4);
        lv_obj_set_size(s_sidebar_marks[i], 3, 50);
        s_sidebar_labels[i] = lv_label_create(s_sidebar_rows[i]);
        saki_label_base(s_sidebar_labels[i], lv_color_hex(0xE2E8F0));
        lv_obj_set_pos(s_sidebar_labels[i], 8, 0);
        lv_obj_set_size(s_sidebar_labels[i], 62, 60);
        lv_label_set_long_mode(s_sidebar_labels[i], LV_LABEL_LONG_CLIP);
    }
    lv_obj_add_flag(s_sidebar, LV_OBJ_FLAG_HIDDEN);

    s_ble_overlay = lv_obj_create(screen);
    lv_obj_set_pos(s_ble_overlay, 12, 73);
    lv_obj_set_size(s_ble_overlay, 296, 121);
    lv_obj_set_style_bg_color(s_ble_overlay, lv_color_hex(0x111A2E), 0);
    lv_obj_set_style_bg_opa(s_ble_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ble_overlay, 2, 0);
    lv_obj_set_style_border_color(s_ble_overlay, lv_color_hex(0x60A5FA), 0);
    lv_obj_set_style_radius(s_ble_overlay, 10, 0);
    lv_obj_set_style_pad_all(s_ble_overlay, 12, 0);
    lv_obj_clear_flag(s_ble_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_ble_overlay_title = lv_label_create(s_ble_overlay);
    saki_label_base(s_ble_overlay_title, lv_color_hex(0xF8FAFC));
    lv_obj_set_style_text_font(s_ble_overlay_title, &s_font_16, 0);
    lv_obj_set_width(s_ble_overlay_title, 268);

    s_ble_overlay_detail = lv_label_create(s_ble_overlay);
    saki_label_base(s_ble_overlay_detail, lv_color_hex(0xCBD5E1));
    lv_obj_set_pos(s_ble_overlay_detail, 0, 32);
    lv_obj_set_size(s_ble_overlay_detail, 268, 42);
    lv_label_set_long_mode(s_ble_overlay_detail, LV_LABEL_LONG_WRAP);

    s_ble_overlay_progress = lv_bar_create(s_ble_overlay);
    lv_obj_set_pos(s_ble_overlay_progress, 0, 83);
    lv_obj_set_size(s_ble_overlay_progress, 268, 8);
    lv_bar_set_range(s_ble_overlay_progress, 0, 100);
    lv_obj_set_style_bg_color(
        s_ble_overlay_progress,
        lv_color_hex(0x253553),
        LV_PART_MAIN
    );
    lv_obj_set_style_bg_color(
        s_ble_overlay_progress,
        lv_color_hex(0x60A5FA),
        LV_PART_INDICATOR
    );
    lv_obj_set_style_radius(
        s_ble_overlay_progress,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN
    );
    lv_obj_set_style_radius(
        s_ble_overlay_progress,
        LV_RADIUS_CIRCLE,
        LV_PART_INDICATOR
    );
    lv_obj_add_flag(s_ble_overlay, LV_OBJ_FLAG_HIDDEN);

    s_dimming_overlay = lv_obj_create(screen);
    lv_obj_set_pos(s_dimming_overlay, 0, 0);
    lv_obj_set_size(s_dimming_overlay, 320, 240);
    saki_set_plain_panel(s_dimming_overlay, lv_color_black());
}

static void saki_ble_overlay_show(
    const char *title,
    const char *detail,
    int32_t progress
)
{
    lv_label_set_text(s_ble_overlay_title, title);
    lv_label_set_text(s_ble_overlay_detail, detail);
    if (progress < 0) {
        lv_obj_add_flag(s_ble_overlay_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_ble_overlay_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_ble_overlay_progress, progress, LV_ANIM_OFF);
    }
    lv_obj_clear_flag(s_ble_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_dimming_overlay);
    lv_obj_move_foreground(s_ble_overlay);
}

static void saki_ble_overlay_hide_if_idle(void)
{
    if (!s_button_overlay_visible && !s_ble_notice_visible) {
        lv_obj_add_flag(s_ble_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void saki_ble_overlay_apply_notice(uint64_t now_ms)
{
    char title[40];
    char detail[80];
    int32_t progress = -1;

    switch (s_ble_notice) {
    case SAKI_UI_BLE_NO_BOND:
        snprintf(title, sizeof(title), "%s", "No BLE bond");
        snprintf(detail, sizeof(detail), "%s", "Hold K2 for 2s to pair.");
        break;
    case SAKI_UI_BLE_ALREADY_BONDED:
        snprintf(title, sizeof(title), "%s", "BLE already paired");
        snprintf(detail, sizeof(detail), "%s", "Hold K2 for 5s to clear first.");
        break;
    case SAKI_UI_BLE_PAUSED:
        snprintf(title, sizeof(title), "%s", "BLE paused");
        snprintf(detail, sizeof(detail), "%s", "Short-press K2 to reconnect.");
        break;
    case SAKI_UI_BLE_WAITING:
        snprintf(title, sizeof(title), "%s", "BLE ready");
        snprintf(detail, sizeof(detail), "%s", "Waiting for the paired Mac.");
        break;
    case SAKI_UI_BLE_PAIRING_WINDOW: {
        uint64_t remaining_ms = s_ble_pairing_deadline_ms > now_ms
            ? s_ble_pairing_deadline_ms - now_ms
            : 0;
        unsigned long remaining_seconds = (unsigned long)((remaining_ms + 999U) / 1000U);

        snprintf(title, sizeof(title), "BLE pairing: %lus", remaining_seconds);
        snprintf(detail, sizeof(detail), "%s", "Open Bluetooth on the Mac now.");
        progress = (int32_t)(remaining_ms > SAKI_UI_BLE_PAIRING_WINDOW_MS
            ? 100U
            : remaining_ms / (SAKI_UI_BLE_PAIRING_WINDOW_MS / 100U));
        break;
    }
    case SAKI_UI_BLE_CONNECTING:
        snprintf(title, sizeof(title), "%s", "Securing BLE link");
        snprintf(detail, sizeof(detail), "%s", "Waiting for encrypted connection.");
        break;
    case SAKI_UI_BLE_CONNECTED:
        snprintf(title, sizeof(title), "%s", "BLE connected");
        snprintf(detail, sizeof(detail), "%s", "The Mac can now send status.");
        break;
    case SAKI_UI_BLE_BONDS_CLEARED:
        snprintf(title, sizeof(title), "%s", "BLE bonds cleared");
        snprintf(detail, sizeof(detail), "%s", "Hold K2 for 2s to pair again.");
        break;
    case SAKI_UI_BLE_PAIRING_EXPIRED:
        snprintf(title, sizeof(title), "%s", "Pairing window closed");
        snprintf(detail, sizeof(detail), "%s", "Hold K2 for 2s to reopen it.");
        break;
    case SAKI_UI_BLE_UNAVAILABLE:
    default:
        snprintf(title, sizeof(title), "%s", "BLE unavailable");
        snprintf(detail, sizeof(detail), "%s", "USB remains available.");
        break;
    }
    saki_ble_overlay_show(title, detail, progress);
}

static void saki_ble_overlay_set_notice(
    const saki_ui_ble_message_t *message,
    uint64_t now_ms
)
{
    s_ble_notice = message->notice;
    s_ble_notice_visible = true;
    s_ble_pairing_deadline_ms = 0;
    if (message->notice == SAKI_UI_BLE_PAIRING_WINDOW) {
        s_ble_pairing_deadline_ms = now_ms + message->remaining_ms;
        s_ble_notice_deadline_ms = s_ble_pairing_deadline_ms;
    } else {
        s_ble_notice_deadline_ms = now_ms + SAKI_BLE_NOTICE_MS;
    }
    if (!s_button_overlay_visible) {
        saki_ble_overlay_apply_notice(now_ms);
    }
}

static void saki_ble_overlay_update_button(uint64_t now_ms)
{
    char title[40];
    uint32_t held_ms = saki_button_policy_held_ms(&s_button_policy, now_ms);
    uint32_t progress;

    if (!s_button_policy.stable_pressed) {
        if (s_button_overlay_visible) {
            s_button_overlay_visible = false;
            if (s_ble_notice_visible) {
                saki_ble_overlay_apply_notice(now_ms);
            } else {
                saki_ble_overlay_hide_if_idle();
            }
        }
        return;
    }

    s_button_overlay_visible = true;
    progress = held_ms >= SAKI_BUTTON_CLEAR_HOLD_MS
        ? 100U
        : held_ms / (SAKI_BUTTON_CLEAR_HOLD_MS / 100U);
    snprintf(
        title,
        sizeof(title),
        "K2 held: %lu.%lus",
        (unsigned long)(held_ms / 1000U),
        (unsigned long)((held_ms % 1000U) / 100U)
    );
    saki_ble_overlay_show(
        title,
        held_ms < SAKI_BUTTON_PAIR_HOLD_MS
            ? "Hold 2s to pair / 5s to clear."
            : held_ms < SAKI_BUTTON_CLEAR_HOLD_MS
                ? "Release: pair / keep holding: clear."
                : "Clearing all BLE bonds...",
        (int32_t)progress
    );
}

static void saki_ble_overlay_tick(uint64_t now_ms)
{
    if (s_ble_notice_visible && now_ms >= s_ble_notice_deadline_ms) {
        s_ble_notice_visible = false;
        s_ble_pairing_deadline_ms = 0;
        saki_ble_overlay_hide_if_idle();
    } else if (s_ble_notice_visible && !s_button_overlay_visible &&
               s_ble_notice == SAKI_UI_BLE_PAIRING_WINDOW) {
        saki_ble_overlay_apply_notice(now_ms);
    }
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

static bool saki_has_sidebar(void)
{
    return s_current_display.multi_session && s_current_display.count > 1;
}

static const char *saki_short_state(saki_agent_state_t state)
{
    switch (state) {
    case SAKI_AGENT_STARTING: return "启动中";
    case SAKI_AGENT_THINKING: return "思考中";
    case SAKI_AGENT_WORKING: return "执行中";
    case SAKI_AGENT_WAITING_USER: return "待输入";
    case SAKI_AGENT_WAITING_APPROVAL: return "待批准";
    case SAKI_AGENT_COMPLETED: return "已完成";
    case SAKI_AGENT_FAILED: return "失败";
    case SAKI_AGENT_CANCELLED: return "已取消";
    default: return "空闲";
    }
}

static void saki_apply_main_layout(void)
{
    bool compact = saki_has_sidebar();
    lv_obj_set_pos(s_status_label, compact ? 60 : 68, 42);
    lv_obj_set_width(s_status_label, compact ? 172 : 165);
    lv_obj_set_size(s_status_dot, compact ? 32 : 38, compact ? 32 : 38);
    lv_obj_set_pos(s_elapsed_label, compact ? 60 : 244, compact ? 66 : 44);
    lv_obj_set_width(s_elapsed_label, compact ? 172 : 62);
    lv_obj_set_style_text_align(s_elapsed_label, compact ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_title_label, compact ? 216 : 288);
    lv_obj_set_width(s_progress_bar, compact ? 164 : 238);
    lv_obj_set_pos(s_progress_label, compact ? 188 : 260, 197);
}

static void saki_apply_text_view(const saki_state_snapshot_t *snapshot)
{
    const char *summary;
    bool prioritize_detail;
    lv_coord_t card_width = saki_has_sidebar() ? 220 : 296;

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
        lv_obj_set_size(s_activity_card, card_width, 106);
        lv_obj_set_size(s_activity_label, card_width - 16, 84);
        lv_label_set_text_fmt(s_activity_label, "DETAIL\n%s", snapshot->detail);
        return;
    }

    lv_obj_clear_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_activity_card, 12, 137);
    lv_obj_set_size(s_activity_card, card_width, 57);
    lv_obj_set_size(s_activity_label, card_width - 16, 40);
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
    saki_apply_main_layout();
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

    if (s_current_display.multi_session && snapshot->connected) {
        lv_label_set_text_fmt(s_status_label, "%s%s", saki_short_state(snapshot->state),
            snapshot->stale ? " · 过期" : "");
    }
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

static void saki_render_sidebar(void)
{
    if (!s_sidebar_dirty) return;
    s_sidebar_dirty = false;
    if (!s_current_display.multi_session) {
        lv_obj_add_flag(s_sidebar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (saki_has_sidebar()) lv_obj_clear_flag(s_sidebar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_sidebar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_model_label, "%s  %u/%u  !%u%s",
        s_selected_session > 0 ? "查看 · 顶栏返回" : "最新提交",
        s_current_display.count, s_current_display.total, s_current_display.hidden_attention,
        s_current_display.capacity_rejected ? " FULL" : "");
    if (!s_current_display.count) lv_label_set_text(s_title_label, "暂无会话");
    uint8_t row = 0;
    for (uint8_t i = 0; i < s_current_display.count; ++i) {
        if (i == s_selected_session) continue;
        const saki_state_snapshot_t *item = &s_current_display.items[i];
        char short_id[5];
        (void)saki_utf8_copy(short_id, sizeof(short_id), item->task_id);
        const char *source = strcmp(item->agent_name, "Claude Code") == 0 ? "Claude" : item->agent_name;
        s_sidebar_indices[row] = i;
        lv_obj_clear_flag(s_sidebar_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_color_t color = saki_state_color(item);
        lv_obj_set_style_bg_color(s_sidebar_marks[row], color, 0);
        lv_obj_set_style_text_color(s_sidebar_labels[row], color, 0);
        lv_label_set_text_fmt(s_sidebar_labels[row], "%s\n%s%s\n%s", source, short_id,
            item->stale ? "*" : "", saki_short_state(item->state));
        ++row;
    }
    while (row < SAKI_SIDEBAR_COUNT) {
        s_sidebar_indices[row] = -1;
        lv_obj_add_flag(s_sidebar_rows[row++], LV_OBJ_FLAG_HIDDEN);
    }
}

static void saki_select_session(int index, uint64_t now_ms)
{
    s_sidebar_dirty = true;
    s_selected_session = saki_session_view_select(&s_session_view, &s_current_display, index, now_ms);
    if (s_current_display.count) {
        s_current_snapshot = s_current_display.items[s_selected_session];
        s_policy.detail_visible = false;
        saki_apply_snapshot(&s_current_snapshot);
        /* Selection can change elapsed within the same second. */
        uint64_t ignored = 0;
        saki_refresh_elapsed(&s_current_snapshot, now_ms, &ignored, true);
    }
    saki_render_sidebar();
}

static void saki_handle_tap(lv_coord_t x, lv_coord_t y)
{
    bool in_detail_region = x >= SAKI_DETAIL_LEFT && x < (saki_has_sidebar() ? 232 : SAKI_DETAIL_RIGHT) &&
        y >= SAKI_DETAIL_TOP && y < SAKI_DETAIL_BOTTOM;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (s_current_display.multi_session) {
        bool was_dim = s_policy.backlight_percent < s_policy.config.active_percent;
        saki_apply_policy_changes(saki_ui_policy_on_local_activity(&s_policy, now_ms));
        if (was_dim || s_button_overlay_visible || s_ble_notice_visible) return;
        if (saki_has_sidebar() && x >= SAKI_SIDEBAR_LEFT &&
            y >= SAKI_SIDEBAR_TOP && y < 216) {
            int row = (y - SAKI_SIDEBAR_TOP) / SAKI_SIDEBAR_ROW_HEIGHT;
            if (row < SAKI_SIDEBAR_COUNT && s_sidebar_indices[row] >= 0)
                saki_select_session(s_sidebar_indices[row], now_ms);
            return;
        }
        if (s_selected_session > 0 && y < SAKI_SIDEBAR_TOP) {
            saki_select_session(0, now_ms);
            return;
        }
        if (s_selected_session > 0) s_session_view.deadline_ms = now_ms + SAKI_DETAIL_TIMEOUT_MS;
    }
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
    TaskHandle_t start_waiter = (TaskHandle_t)argument;
    saki_ui_ble_message_t ble_message;
    TickType_t last_demo_tick = xTaskGetTickCount();
    TickType_t last_button_tick = xTaskGetTickCount();
    uint64_t displayed_elapsed_seconds = UINT64_MAX;
    uint64_t now_ms;
    uint32_t policy_changes;
    size_t demo_index = 0;
    esp_timer_handle_t tick_timer = NULL;
    esp_err_t result;
    uint8_t button_inputs[2] = {0};
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
    lv_init();
    saki_fonts_init();
    result = saki_display_init();
    if (result != ESP_OK) {
        goto start_failed;
    }
    result = esp_timer_create(&tick_arguments, &tick_timer);
    if (result != ESP_OK) {
        goto start_failed;
    }
    result = esp_timer_start_periodic(tick_timer, SAKI_UI_TICK_PERIOD_US);
    if (result != ESP_OK) {
        goto start_failed;
    }
    saki_create_screen();

    now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    saki_ui_policy_init(&s_policy, &policy_config, now_ms);
    if (aw9523b_read_byte(button_inputs, sizeof(button_inputs)) == ESP_OK) {
        saki_button_policy_init(
            &s_button_policy,
            (button_inputs[0] & KEY_K2) != 0,
            now_ms
        );
    } else {
        saki_button_policy_init(&s_button_policy, false, now_ms);
    }
    saki_state_snapshot_init(&s_current_snapshot);
    if (xQueueReceive(s_state_queue, &s_current_display, 0) == pdTRUE) {
        s_selected_session = saki_session_view_update(&s_session_view, &s_current_display, now_ms);
        if (s_current_display.count) s_current_snapshot = s_current_display.items[0];
        else {
            s_current_snapshot.connected = s_current_display.connected;
            snprintf(s_current_snapshot.transport, sizeof(s_current_snapshot.transport),
                "%s", s_current_display.transport);
        }
    } else {
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
    s_start_result = ESP_OK;
    if (start_waiter != NULL) {
        xTaskNotifyGive(start_waiter);
    }

    while (true) {
        if (xTaskGetTickCount() - last_button_tick >=
            pdMS_TO_TICKS(SAKI_BUTTON_POLL_MS)) {
            saki_button_event_t button_event = SAKI_BUTTON_EVENT_NONE;

            now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            if (aw9523b_read_byte(button_inputs, sizeof(button_inputs)) == ESP_OK) {
                button_event = saki_button_policy_update(
                    &s_button_policy,
                    (button_inputs[0] & KEY_K2) != 0,
                    now_ms
                );
                if (s_button_policy.stable_pressed ||
                    button_event != SAKI_BUTTON_EVENT_NONE) {
                    saki_apply_policy_changes(
                        saki_ui_policy_on_local_activity(&s_policy, now_ms)
                    );
                }
                if (button_event != SAKI_BUTTON_EVENT_NONE &&
                    s_button_callback != NULL) {
                    s_button_callback(button_event, s_button_context);
                }
            }
            saki_ble_overlay_update_button(now_ms);
            last_button_tick = xTaskGetTickCount();
        }

        if (xQueueReceive(s_ble_queue, &ble_message, 0) == pdTRUE) {
            now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            saki_ble_overlay_set_notice(&ble_message, now_ms);
        }

        if (xQueueReceive(s_state_queue, &s_current_display, 0) == pdTRUE) {
            now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            s_sidebar_dirty = true;
            bool new_submission = s_current_display.multi_session && s_current_display.count > 0 &&
                (strcmp(s_session_view.latest_id, s_current_display.items[0].task_id) != 0 ||
                 strcmp(s_session_view.latest_run, s_current_display.run_ids[0]) != 0);
            s_selected_session = saki_session_view_update(&s_session_view, &s_current_display, now_ms);
            if (s_current_display.count > 0) {
                if (new_submission || strcmp(s_current_snapshot.task_id,
                    s_current_display.items[s_selected_session].task_id) != 0) {
                    s_policy.detail_visible = false;
                }
                s_current_snapshot = s_current_display.items[s_selected_session];
            } else {
                saki_state_snapshot_init(&s_current_snapshot);
                s_current_snapshot.connected = s_current_display.connected;
                snprintf(s_current_snapshot.transport, sizeof(s_current_snapshot.transport),
                    "%s", s_current_display.transport);
            }
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
        saki_ble_overlay_tick(now_ms);
        saki_refresh_elapsed(
            &s_current_snapshot,
            now_ms,
            &displayed_elapsed_seconds,
            false
        );

        if (s_current_display.multi_session) {
            if (s_selected_session > 0 && now_ms >= s_session_view.deadline_ms) saki_select_session(0, now_ms);
        }
        saki_render_sidebar();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(SAKI_UI_HANDLER_MS));
    }

start_failed:
    ESP_LOGE(TAG, "UI initialization failed: %s", esp_err_to_name(result));
    if (tick_timer != NULL) {
        (void)esp_timer_stop(tick_timer);
        (void)esp_timer_delete(tick_timer);
    }
    s_start_result = result;
    if (start_waiter != NULL) {
        xTaskNotifyGive(start_waiter);
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void saki_ui_set_button_callback(
    saki_ui_button_fn callback,
    void *context
)
{
    s_button_callback = callback;
    s_button_context = context;
}

esp_err_t saki_ui_start(const saki_state_snapshot_t *initial_state, bool demo_mode)
{
    BaseType_t created;
    uint32_t notified;

    if (s_state_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state_queue = xQueueCreate(1, sizeof(saki_display_snapshot_t));
    s_ble_queue = xQueueCreate(1, sizeof(saki_ui_ble_message_t));
    if (s_state_queue == NULL || s_ble_queue == NULL) {
        if (s_state_queue != NULL) {
            vQueueDelete(s_state_queue);
            s_state_queue = NULL;
        }
        if (s_ble_queue != NULL) {
            vQueueDelete(s_ble_queue);
            s_ble_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }
    if (initial_state != NULL) {
        (void)saki_ui_submit(initial_state);
    }
    s_demo_mode = demo_mode;
    s_start_result = ESP_ERR_INVALID_STATE;
    created = xTaskCreate(
        saki_ui_task,
        "saki_ui",
        SAKI_UI_TASK_STACK_BYTES,
        xTaskGetCurrentTaskHandle(),
        SAKI_UI_TASK_PRIORITY,
        &s_task_handle
    );
    if (created != pdPASS) {
        vQueueDelete(s_state_queue);
        vQueueDelete(s_ble_queue);
        s_state_queue = NULL;
        s_ble_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    notified = ulTaskNotifyTake(
        pdTRUE,
        pdMS_TO_TICKS(SAKI_UI_START_TIMEOUT_MS)
    );
    if (notified == 0) {
        ESP_LOGE(TAG, "UI initialization timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (s_start_result != ESP_OK) {
        vQueueDelete(s_state_queue);
        vQueueDelete(s_ble_queue);
        s_state_queue = NULL;
        s_ble_queue = NULL;
        return s_start_result;
    }
    ESP_LOGI(TAG, "Saki UI ready (demo=%d)", demo_mode);
    return ESP_OK;
}

esp_err_t saki_ui_notify_ble(
    saki_ui_ble_notice_t notice,
    uint32_t remaining_ms
)
{
    saki_ui_ble_message_t message = {
        .notice = notice,
        .remaining_ms = remaining_ms,
    };

    if (notice < SAKI_UI_BLE_NO_BOND || notice > SAKI_UI_BLE_UNAVAILABLE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ble_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueOverwrite(s_ble_queue, &message) == pdPASS
        ? ESP_OK
        : ESP_FAIL;
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
    saki_display_snapshot_t *display = calloc(1, sizeof(*display));
    if (display == NULL) return ESP_ERR_NO_MEM;
    display->count = 1;
    display->total = 1;
    display->connected = snapshot->connected;
    display->items[0] = *snapshot;
    snprintf(display->transport, sizeof(display->transport), "%s", snapshot->transport);
    BaseType_t queued = xQueueOverwrite(s_state_queue, display);
    free(display);
    if (queued != pdPASS) return ESP_FAIL;
    if (overwrote_pending != NULL) {
        *overwrote_pending = pending;
    }
    return ESP_OK;
}

esp_err_t saki_ui_submit_display(const saki_display_snapshot_t *display)
{
    if (display == NULL || display->count > SAKI_DISPLAY_MAX_SESSIONS) return ESP_ERR_INVALID_ARG;
    if (s_state_queue == NULL) return ESP_ERR_INVALID_STATE;
    return xQueueOverwrite(s_state_queue, display) == pdPASS ? ESP_OK : ESP_FAIL;
}
