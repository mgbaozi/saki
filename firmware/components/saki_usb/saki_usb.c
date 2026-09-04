#include "saki_usb.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#define SAKI_USB_RX_STREAM_BYTES 4096
#define SAKI_USB_TX_QUEUE_LENGTH 8
#define SAKI_USB_TX_MESSAGE_BYTES 640
#define SAKI_USB_TASK_STACK_BYTES 10240
#define SAKI_USB_TASK_PRIORITY 5
#define SAKI_USB_TASK_WAIT_MS 10
#define SAKI_USB_TX_TIMEOUT_MS 250

typedef struct {
    size_t length;
    uint8_t data[SAKI_USB_TX_MESSAGE_BYTES];
} saki_usb_tx_message_t;

static const char *TAG = "saki_usb";
static const char s_language[] = {0x09, 0x04};
static char s_serial_number[17];
static const char *s_string_descriptors[] = {
    s_language,
    "Saki",
    "Saki Agent Display",
    s_serial_number,
    "Saki Status",
};

static StreamBufferHandle_t s_rx_stream;
static QueueHandle_t s_tx_queue;
static saki_usb_rx_fn s_receive;
static saki_usb_connection_fn s_connection_changed;
static saki_usb_poll_fn s_poll;
static void *s_callback_context;
static volatile bool s_dtr;
static TaskHandle_t s_task_handle;

static void saki_usb_rx_callback(int interface, cdcacm_event_t *event)
{
    uint8_t buffer[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t received = 0;
    (void)event;

    if (tinyusb_cdcacm_read(interface, buffer, sizeof(buffer), &received) != ESP_OK ||
        received == 0) {
        return;
    }
    if (xStreamBufferSend(s_rx_stream, buffer, received, 0) != received) {
        ESP_LOGW(TAG, "RX stream full; dropped %u bytes", (unsigned)received);
    }
}

static void saki_usb_line_state_callback(int interface, cdcacm_event_t *event)
{
    (void)interface;
    s_dtr = event->line_state_changed_data.dtr;
}

static void saki_usb_device_event(tinyusb_event_t *event, void *argument)
{
    (void)argument;
    if (event->id == TINYUSB_EVENT_DETACHED) {
        s_dtr = false;
    }
}

static esp_err_t saki_usb_write_message(const saki_usb_tx_message_t *message)
{
    size_t offset = 0;
    TickType_t started = xTaskGetTickCount();

    while (offset < message->length) {
        size_t queued = tinyusb_cdcacm_write_queue(
            TINYUSB_CDC_ACM_0,
            message->data + offset,
            message->length - offset
        );
        offset += queued;
        esp_err_t result = tinyusb_cdcacm_write_flush(
            TINYUSB_CDC_ACM_0,
            pdMS_TO_TICKS(SAKI_USB_TASK_WAIT_MS)
        );
        if (result != ESP_OK && result != ESP_ERR_NOT_FINISHED) {
            return result;
        }
        if (offset < message->length) {
            if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(SAKI_USB_TX_TIMEOUT_MS)) {
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    return ESP_OK;
}

static void saki_usb_task(void *argument)
{
    uint8_t rx_chunk[256];
    saki_usb_tx_message_t message;
    bool previous_connection = false;
    (void)argument;

    while (true) {
        bool connected = s_dtr;
        if (connected != previous_connection) {
            if (!connected) {
                xStreamBufferReset(s_rx_stream);
                xQueueReset(s_tx_queue);
            }
            if (s_connection_changed != NULL) {
                s_connection_changed(connected, s_callback_context);
            }
            ESP_LOGI(TAG, "CDC connection %s", connected ? "ready" : "closed");
            previous_connection = connected;
        }

        size_t received = xStreamBufferReceive(
            s_rx_stream,
            rx_chunk,
            sizeof(rx_chunk),
            pdMS_TO_TICKS(SAKI_USB_TASK_WAIT_MS)
        );
        if (received > 0 && s_receive != NULL) {
            s_receive(rx_chunk, received, s_callback_context);
        }

        if (xQueueReceive(s_tx_queue, &message, 0) == pdTRUE && connected) {
            esp_err_t result = saki_usb_write_message(&message);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "CDC write failed: %s", esp_err_to_name(result));
            }
        }

        if (s_poll != NULL) {
            s_poll(s_callback_context);
        }
    }
}

esp_err_t saki_usb_start(
    const char *serial_number,
    saki_usb_rx_fn receive,
    saki_usb_connection_fn connection_changed,
    saki_usb_poll_fn poll,
    void *context
)
{
    BaseType_t task_created;
    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG(saki_usb_device_event);
    const tinyusb_config_cdcacm_t cdc_config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = saki_usb_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = saki_usb_line_state_callback,
        .callback_line_coding_changed = NULL,
    };

    if (serial_number == NULL || receive == NULL || s_rx_stream != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_serial_number, sizeof(s_serial_number), "%s", serial_number);
    s_receive = receive;
    s_connection_changed = connection_changed;
    s_poll = poll;
    s_callback_context = context;

    s_rx_stream = xStreamBufferCreate(SAKI_USB_RX_STREAM_BYTES, 1);
    s_tx_queue = xQueueCreate(SAKI_USB_TX_QUEUE_LENGTH, sizeof(saki_usb_tx_message_t));
    if (s_rx_stream == NULL || s_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    task_created = xTaskCreate(
        saki_usb_task,
        "saki_usb",
        SAKI_USB_TASK_STACK_BYTES,
        NULL,
        SAKI_USB_TASK_PRIORITY,
        &s_task_handle
    );
    if (task_created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    usb_config.descriptor.string = s_string_descriptors;
    usb_config.descriptor.string_count =
        sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]);
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&usb_config), TAG, "install TinyUSB");
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&cdc_config), TAG, "initialize CDC ACM");
    ESP_LOGI(TAG, "TinyUSB CDC initialized as %s", s_serial_number);
    return ESP_OK;
}

esp_err_t saki_usb_transmit(
    const uint8_t *data,
    size_t length,
    void *context
)
{
    saki_usb_tx_message_t message;
    (void)context;

    if (data == NULL || length == 0 || length > sizeof(message.data)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tx_queue == NULL || !s_dtr) {
        return ESP_ERR_INVALID_STATE;
    }
    message.length = length;
    memcpy(message.data, data, length);
    return xQueueSend(s_tx_queue, &message, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

bool saki_usb_connected(void)
{
    return s_dtr;
}

uint32_t saki_usb_stack_high_watermark_bytes(void)
{
    TaskHandle_t task = s_task_handle;

    return task == NULL ? 0 : (uint32_t)uxTaskGetStackHighWaterMark(task);
}
