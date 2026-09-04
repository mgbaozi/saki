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
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "saki_model.h"
#include "saki_protocol.h"
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

static saki_protocol_engine_t s_protocol;
static saki_state_snapshot_t s_last_live_snapshot;
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
}

static esp_err_t saki_submit_state(const saki_state_snapshot_t *snapshot)
{
    bool overwrote_pending = false;
    esp_err_t result = saki_ui_submit_tracked(snapshot, &overwrote_pending);

    if (result == ESP_OK && overwrote_pending) {
        saki_protocol_engine_note_ui_overwrite(&s_protocol);
    }
    return result;
}

static void saki_show_disconnected(void)
{
    saki_state_snapshot_t snapshot;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

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
}

static esp_err_t saki_apply_state(
    const saki_state_snapshot_t *snapshot,
    void *context
)
{
    esp_err_t result;
    (void)context;

    result = saki_submit_state(snapshot);
    if (result == ESP_OK && snapshot->connected) {
        saki_state_snapshot_copy(&s_last_live_snapshot, snapshot);
        s_has_last_live_snapshot = true;
    }
    return result;
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

    saki_protocol_engine_disconnect(protocol);
    if (!connected) {
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
        saki_show_disconnected();
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

    saki_state_snapshot_init(&initial_state);
    initial_state.connected = false;
    ESP_ERROR_CHECK(saki_ui_start(&initial_state, SAKI_UI_DEMO_ENABLED));

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
    saki_protocol_engine_init(
        &s_protocol,
        device_id,
        SAKI_FIRMWARE_VERSION,
        saki_usb_transmit,
        saki_apply_state,
        NULL
    );
    saki_protocol_engine_set_runtime_provider(
        &s_protocol,
        saki_collect_runtime_metrics,
        NULL
    );
    ESP_ERROR_CHECK(saki_usb_start(
        device_id,
        saki_receive_usb,
        saki_usb_connection_changed,
        saki_usb_poll,
        &s_protocol
    ));
    s_app_main_stack_min_bytes = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}
