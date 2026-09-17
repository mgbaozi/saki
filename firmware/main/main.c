/**
 ******************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2026-01-28
 * @brief       Saki Agent status display
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ******************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 ******************************************************************************
 */

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "saki_ble.h"
#include "saki_model.h"
#include "saki_protocol.h"
#include "saki_transport.h"
#include "saki_ui.h"
#include "saki_usb.h"

#ifndef SAKI_FIRMWARE_VERSION
#error "SAKI_FIRMWARE_VERSION must be provided by the selected build profile"
#endif

#ifdef CONFIG_SAKI_UI_DEMO
#define SAKI_UI_DEMO_ENABLED true
#else
#define SAKI_UI_DEMO_ENABLED false
#endif

static saki_protocol_engine_t *s_usb_protocol;
static saki_protocol_engine_t *s_ble_protocol;
static saki_transport_manager_t s_transport_manager;
static SemaphoreHandle_t s_transport_mutex;
static saki_state_snapshot_t s_last_live_snapshot;
static saki_display_snapshot_t *s_last_display;
static bool s_multi_display;
static bool s_has_last_live_snapshot;
static uint32_t s_app_main_stack_min_bytes;

static uint32_t saki_metric_u32(size_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void saki_collect_runtime_metrics(
    saki_runtime_metrics_t *metrics,
    void *context
)
{
    saki_ble_diagnostics_t ble_diagnostics;
    saki_transport_diagnostics_t transport_diagnostics;
    (void)context;

    metrics->heap_free_bytes = saki_metric_u32(
        heap_caps_get_free_size(MALLOC_CAP_8BIT)
    );
    metrics->heap_min_bytes = saki_metric_u32(
        heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
    );
    metrics->internal_free_bytes = saki_metric_u32(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    metrics->internal_min_bytes = saki_metric_u32(
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    metrics->app_stack_min_bytes = s_app_main_stack_min_bytes;
    metrics->ui_stack_min_bytes = saki_ui_stack_high_watermark_bytes();
    metrics->usb_stack_min_bytes = saki_usb_stack_high_watermark_bytes();
    metrics->ble_stack_min_bytes = saki_ble_stack_high_watermark_bytes();
    saki_ble_get_diagnostics(&ble_diagnostics);
    metrics->ble_rx_drops = ble_diagnostics.rx_drops;
    metrics->ble_tx_drops = ble_diagnostics.tx_drops;
    metrics->ble_security_rejections = ble_diagnostics.security_rejections;
    metrics->ble_pairing_rejections = ble_diagnostics.pairing_rejections;
    metrics->ble_disconnects = ble_diagnostics.disconnects;
    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    saki_transport_manager_get_diagnostics(
        &s_transport_manager,
        &transport_diagnostics
    );
    xSemaphoreGive(s_transport_mutex);
    metrics->transport_switches = transport_diagnostics.switches;
    metrics->transport_rejections = transport_diagnostics.rejections;
}

static esp_err_t saki_submit_state(const saki_state_snapshot_t *snapshot)
{
    bool overwrote_pending = false;
    esp_err_t result = saki_ui_submit_tracked(snapshot, &overwrote_pending);

    if (result == ESP_OK && overwrote_pending) {
        saki_protocol_engine_note_ui_overwrite(s_usb_protocol);
        saki_protocol_engine_note_ui_overwrite(s_ble_protocol);
    }
    return result;
}

static void saki_show_disconnected(void)
{
    saki_state_snapshot_t snapshot;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    if (s_transport_manager.active != SAKI_TRANSPORT_NONE) {
        xSemaphoreGive(s_transport_mutex);
        return;
    }
    if (s_multi_display) {
        for (uint8_t i = 0; i < s_last_display->count; ++i) {
            saki_state_snapshot_t *item = &s_last_display->items[i];
            item->elapsed_ms = saki_state_elapsed_at(item, now_ms);
            item->received_at_ms = now_ms;
            item->connected = false;
            snprintf(item->transport, sizeof(item->transport), "%s", "OFFLINE");
        }
        s_last_display->connected = false;
        snprintf(s_last_display->transport, sizeof(s_last_display->transport), "%s", "OFFLINE");
        (void)saki_ui_submit_display(s_last_display);
        xSemaphoreGive(s_transport_mutex);
        return;
    }
    if (s_has_last_live_snapshot) {
        saki_state_snapshot_copy(&snapshot, &s_last_live_snapshot);
        snapshot.elapsed_ms = saki_state_elapsed_at(&snapshot, now_ms);
        snapshot.received_at_ms = now_ms;
    } else {
        saki_state_snapshot_init(&snapshot);
    }
    snapshot.connected = false;
    snprintf(snapshot.transport, sizeof(snapshot.transport), "%s", "OFFLINE");
    saki_state_snapshot_copy(&s_last_live_snapshot, &snapshot);
    (void)saki_submit_state(&snapshot);
    xSemaphoreGive(s_transport_mutex);
}

static esp_err_t saki_apply_state(
    const saki_state_snapshot_t *snapshot,
    void *context
)
{
    esp_err_t result;
    (void)context;

    result = saki_submit_state(snapshot);
    if (result == ESP_OK) s_multi_display = false;
    if (result == ESP_OK && snapshot->connected) {
        saki_state_snapshot_copy(&s_last_live_snapshot, snapshot);
        s_has_last_live_snapshot = true;
    }
    return result;
}

static esp_err_t saki_apply_display(const saki_display_snapshot_t *display, void *context)
{
    (void)context;
    esp_err_t result = saki_ui_submit_display(display);
    if (result == ESP_OK) {
        *s_last_display = *display;
        s_multi_display = true;
    }
    return result;
}

static saki_transport_outcome_t saki_submit_display(
    const saki_display_snapshot_t *display, saki_transport_id_t transport,
    const char *session, uint32_t sequence, uint64_t now_ms, void *context
)
{
    (void)context;
    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    saki_transport_outcome_t outcome = saki_transport_manager_submit_display(
        &s_transport_manager, transport, session, sequence, display, now_ms);
    xSemaphoreGive(s_transport_mutex);
    return outcome;
}

static saki_transport_outcome_t saki_submit_candidate(
    const saki_state_snapshot_t *snapshot,
    saki_transport_id_t transport,
    const char *session,
    uint32_t sequence,
    uint64_t now_ms,
    void *context
)
{
    saki_transport_outcome_t outcome;
    (void)context;

    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    outcome = saki_transport_manager_submit(
        &s_transport_manager,
        transport,
        session,
        sequence,
        snapshot,
        now_ms
    );
    xSemaphoreGive(s_transport_mutex);
    return outcome;
}

static bool saki_link_down(
    saki_transport_id_t transport,
    uint64_t now_ms
)
{
    bool should_show_offline;

    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    should_show_offline = saki_transport_manager_link_down(
        &s_transport_manager,
        transport,
        now_ms
    );
    xSemaphoreGive(s_transport_mutex);
    return should_show_offline;
}

static bool saki_transport_tick(uint64_t now_ms)
{
    bool should_show_offline;

    xSemaphoreTake(s_transport_mutex, portMAX_DELAY);
    should_show_offline = saki_transport_manager_tick(
        &s_transport_manager,
        now_ms
    );
    xSemaphoreGive(s_transport_mutex);
    return should_show_offline;
}

static void saki_receive_usb(
    const uint8_t *data,
    size_t length,
    void *context
)
{
    saki_protocol_engine_receive(context, data, length);
}

static void saki_usb_connection_changed(bool connected, void *context)
{
    saki_protocol_engine_t *protocol = context;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

    saki_protocol_engine_disconnect(protocol);
    if (!connected && saki_link_down(SAKI_TRANSPORT_USB, now_ms)) {
        saki_show_disconnected();
    }
}

static void saki_usb_poll(void *context)
{
    saki_protocol_engine_t *protocol = context;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

    if (saki_protocol_engine_check_timeout(protocol, now_ms)) {
        ESP_LOGW(
            "saki_main",
            "Host heartbeat timed out (count=%" PRIu32 ")",
            protocol->diagnostics.heartbeat_timeouts
        );
        if (saki_link_down(SAKI_TRANSPORT_USB, now_ms)) {
            saki_show_disconnected();
        }
    }
    if (saki_transport_tick(now_ms)) {
        saki_show_disconnected();
    }
}

static void saki_receive_ble(
    const uint8_t *data,
    size_t length,
    void *context
)
{
    saki_protocol_engine_receive(context, data, length);
}

static void saki_ble_connection_changed(bool ready, void *context)
{
    saki_protocol_engine_t *protocol = context;

    if (ready) {
        return;
    }
    saki_protocol_engine_disconnect(protocol);
    if (saki_link_down(SAKI_TRANSPORT_BLE, (uint64_t)(esp_timer_get_time() / 1000))) {
        saki_show_disconnected();
    }
}

static void saki_ble_poll(void *context)
{
    saki_protocol_engine_t *protocol = context;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

    if (!saki_protocol_engine_check_timeout(protocol, now_ms)) {
        return;
    }
    ESP_LOGW(
        "saki_main",
        "BLE Host heartbeat timed out (count=%" PRIu32 ")",
        protocol->diagnostics.heartbeat_timeouts
    );
    (void)saki_ble_disconnect();
    if (saki_link_down(SAKI_TRANSPORT_BLE, now_ms)) {
        saki_show_disconnected();
    }
}

static void saki_button_changed(saki_button_event_t event, void *context)
{
    (void)context;
    if (saki_ble_button_event(event) != ESP_OK) {
        ESP_LOGW("saki_main", "K2 BLE action could not be queued");
    }
}

static void saki_ble_event_received(
    saki_ble_event_t event,
    uint32_t remaining_ms,
    void *context
)
{
    saki_ui_ble_notice_t notice;
    (void)context;

    switch (event) {
    case SAKI_BLE_EVENT_NO_BOND:
        notice = SAKI_UI_BLE_NO_BOND;
        break;
    case SAKI_BLE_EVENT_ALREADY_BONDED:
        notice = SAKI_UI_BLE_ALREADY_BONDED;
        break;
    case SAKI_BLE_EVENT_PAUSED:
        notice = SAKI_UI_BLE_PAUSED;
        break;
    case SAKI_BLE_EVENT_WAITING:
        notice = SAKI_UI_BLE_WAITING;
        break;
    case SAKI_BLE_EVENT_PAIRING_WINDOW:
        notice = SAKI_UI_BLE_PAIRING_WINDOW;
        break;
    case SAKI_BLE_EVENT_CONNECTING:
        notice = SAKI_UI_BLE_CONNECTING;
        break;
    case SAKI_BLE_EVENT_CONNECTED:
        notice = SAKI_UI_BLE_CONNECTED;
        break;
    case SAKI_BLE_EVENT_BONDS_CLEARED:
        notice = SAKI_UI_BLE_BONDS_CLEARED;
        break;
    case SAKI_BLE_EVENT_PAIRING_EXPIRED:
    default:
        notice = SAKI_UI_BLE_PAIRING_EXPIRED;
        break;
    }
    if (saki_ui_notify_ble(notice, remaining_ms) != ESP_OK) {
        ESP_LOGW("saki_main", "BLE UI notice could not be queued");
    }
}


/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;
    uint8_t mac[6];
    char device_id[13];
    saki_state_snapshot_t initial_state;

    ret = nvs_flash_init();     /* 初始化NVS */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BASE));
    snprintf(
        device_id,
        sizeof(device_id),
        "%02x%02x%02x%02x%02x%02x",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );
    /* These task-owned buffers are not DMA/ISR memory. Keep internal RAM for
     * BLE, FreeRTOS and transient JSON/UI allocations; PSRAM is a board requirement. */
    s_usb_protocol = heap_caps_calloc(1, sizeof(*s_usb_protocol), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ble_protocol = heap_caps_calloc(1, sizeof(*s_ble_protocol), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_last_display = heap_caps_calloc(1, sizeof(*s_last_display), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_usb_protocol && s_ble_protocol && s_last_display ? ESP_OK : ESP_ERR_NO_MEM);
    s_transport_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_transport_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    saki_transport_manager_init(&s_transport_manager, saki_apply_state, NULL);
    s_transport_manager.apply_display = saki_apply_display;
    saki_protocol_engine_init_peer(
        s_usb_protocol,
        device_id,
        SAKI_FIRMWARE_VERSION,
        SAKI_TRANSPORT_USB,
        SAKI_PROTOCOL_CAPABILITY_BLE |
            SAKI_PROTOCOL_CAPABILITY_SECURE_CONNECTION |
            SAKI_PROTOCOL_CAPABILITY_TRANSPORT_ARBITRATION |
            SAKI_PROTOCOL_CAPABILITY_GENERIC_SOURCE,
        saki_usb_transmit,
        saki_submit_candidate,
        NULL
    );
    saki_protocol_engine_set_runtime_provider(
        s_usb_protocol,
        saki_collect_runtime_metrics,
        NULL
    );
    saki_protocol_engine_init_peer(
        s_ble_protocol,
        device_id,
        SAKI_FIRMWARE_VERSION,
        SAKI_TRANSPORT_BLE,
        SAKI_PROTOCOL_CAPABILITY_BLE |
            SAKI_PROTOCOL_CAPABILITY_SECURE_CONNECTION |
            SAKI_PROTOCOL_CAPABILITY_TRANSPORT_ARBITRATION |
            SAKI_PROTOCOL_CAPABILITY_GENERIC_SOURCE,
        saki_ble_transmit,
        saki_submit_candidate,
        NULL
    );
    saki_protocol_engine_set_runtime_provider(
        s_ble_protocol,
        saki_collect_runtime_metrics,
        NULL
    );
    s_usb_protocol->submit_display = saki_submit_display;
    s_ble_protocol->submit_display = saki_submit_display;
    saki_ui_set_button_callback(saki_button_changed, NULL);
    saki_state_snapshot_init(&initial_state);
    initial_state.connected = false;
    ESP_ERROR_CHECK(saki_ui_start(&initial_state, SAKI_UI_DEMO_ENABLED));
    ESP_ERROR_CHECK(saki_usb_start(
        device_id,
        saki_receive_usb,
        saki_usb_connection_changed,
        saki_usb_poll,
        s_usb_protocol
    ));
    ret = saki_ble_start(
        saki_receive_ble,
        saki_ble_connection_changed,
        saki_ble_poll,
        saki_ble_event_received,
        s_ble_protocol
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            "saki_main",
            "BLE unavailable; USB remains active: %s",
            esp_err_to_name(ret)
        );
        (void)saki_ui_notify_ble(SAKI_UI_BLE_UNAVAILABLE, 0);
    }
    s_app_main_stack_min_bytes = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}
