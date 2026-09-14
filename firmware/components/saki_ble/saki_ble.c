#include "saki_ble.h"
#include "saki_ble_ids.h"

#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define SAKI_BLE_RX_STREAM_BYTES 4096U
#define SAKI_BLE_RX_CHUNK_BYTES 512U
#define SAKI_BLE_TX_QUEUE_LENGTH 8U
#define SAKI_BLE_TX_MESSAGE_BYTES 1024U
#define SAKI_BLE_CONTROL_QUEUE_LENGTH 16U
#define SAKI_BLE_TASK_STACK_BYTES 10240U
#define SAKI_BLE_TASK_PRIORITY 5U
#define SAKI_BLE_TASK_WAIT_MS 20U
#define SAKI_BLE_NOTIFY_TIMEOUT_MS 300U
#define SAKI_BLE_MIN_ATT_MTU 23U
#define SAKI_BLE_MAX_ATT_MTU 517U

typedef enum {
    SAKI_BLE_CONTROL_REFRESH = 0,
    SAKI_BLE_CONTROL_STATE_CHANGED,
    SAKI_BLE_CONTROL_DISCONNECTED,
    SAKI_BLE_CONTROL_DISCONNECT_REQUEST,
    SAKI_BLE_CONTROL_BUTTON,
} saki_ble_control_type_t;

typedef struct {
    saki_ble_control_type_t type;
    saki_button_event_t button_event;
} saki_ble_control_t;

typedef struct {
    size_t length;
    uint8_t data[SAKI_BLE_TX_MESSAGE_BYTES];
} saki_ble_tx_message_t;

static const char *TAG = "saki_ble";
static ble_uuid_any_t s_service_uuid;
static ble_uuid_any_t s_rx_uuid;
static ble_uuid_any_t s_tx_uuid;

static StreamBufferHandle_t s_rx_stream;
static QueueHandle_t s_tx_queue;
static QueueHandle_t s_control_queue;
static TaskHandle_t s_worker_task;
static saki_ble_rx_fn s_receive;
static saki_ble_connection_fn s_connection_changed;
static saki_ble_poll_fn s_poll;
static saki_ble_event_fn s_event_received;
static void *s_callback_context;
static uint16_t s_rx_handle;
static uint16_t s_tx_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_synced;
static bool s_secure;
static bool s_subscribed;
static bool s_ready_notified;
static bool s_bond_present;
static bool s_manual_paused;
static bool s_pairing_window;
static uint64_t s_pairing_deadline_ms;
static int s_notify_status;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static saki_ble_diagnostics_t s_diagnostics;

void ble_store_config_init(void);

static int saki_ble_gap_event(struct ble_gap_event *event, void *argument);

static int saki_ble_initialize_uuids(void)
{
    int rc = ble_uuid_from_str(&s_service_uuid, SAKI_BLE_SERVICE_UUID);

    if (rc == 0) {
        rc = ble_uuid_from_str(&s_rx_uuid, SAKI_BLE_RX_UUID);
    }
    if (rc == 0) {
        rc = ble_uuid_from_str(&s_tx_uuid, SAKI_BLE_TX_UUID);
    }
    return rc;
}

static void saki_ble_emit_event(
    saki_ble_event_t event,
    uint32_t remaining_ms
)
{
    if (s_event_received != NULL) {
        s_event_received(event, remaining_ms, s_callback_context);
    }
}

static void saki_ble_increment(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        ++(*counter);
    }
}

static uint64_t saki_ble_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void saki_ble_post_control(
    saki_ble_control_type_t type,
    saki_button_event_t button_event
)
{
    saki_ble_control_t control = {
        .type = type,
        .button_event = button_event,
    };

    if (s_control_queue == NULL ||
        xQueueSend(s_control_queue, &control, 0) != pdTRUE) {
        ESP_LOGW(TAG, "BLE control queue full; event=%d", (int)type);
    }
}

static bool saki_ble_load_bond(ble_addr_t *peer_address)
{
    ble_addr_t peer;
    int peer_count = 0;
    int rc = ble_store_util_bonded_peers(&peer, &peer_count, 1);

    if (rc != 0 || peer_count != 1) {
        return false;
    }
    if (peer_address != NULL) {
        *peer_address = peer;
    }
    return true;
}

static bool saki_ble_peer_is_bonded(const ble_addr_t *peer_address)
{
    ble_addr_t bonded_peer;

    return peer_address != NULL && saki_ble_load_bond(&bonded_peer) &&
           ble_addr_cmp(peer_address, &bonded_peer) == 0;
}

static int saki_ble_gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *context,
    void *argument
)
{
    struct ble_gap_conn_desc description;
    uint8_t chunk[SAKI_BLE_RX_CHUNK_BYTES];
    uint16_t length = 0;
    bool subscribed;
    int rc;
    (void)argument;

    if (context == NULL || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        attr_handle != s_rx_handle || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    rc = ble_gap_conn_find(conn_handle, &description);
    if (rc != 0 || !description.sec_state.encrypted ||
        !description.sec_state.bonded) {
        portENTER_CRITICAL(&s_state_lock);
        saki_ble_increment(&s_diagnostics.security_rejections);
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    portENTER_CRITICAL(&s_state_lock);
    subscribed = s_subscribed && s_conn_handle == conn_handle;
    portEXIT_CRITICAL(&s_state_lock);
    if (!subscribed) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (OS_MBUF_PKTLEN(context->om) == 0 ||
        OS_MBUF_PKTLEN(context->om) > sizeof(chunk)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    rc = ble_hs_mbuf_to_flat(context->om, chunk, sizeof(chunk), &length);
    if (rc != 0 || length == 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (s_rx_stream == NULL ||
        xStreamBufferSpacesAvailable(s_rx_stream) < length ||
        xStreamBufferSend(s_rx_stream, chunk, length, 0) != length) {
        portENTER_CRITICAL(&s_state_lock);
        saki_ble_increment(&s_diagnostics.rx_drops);
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static const struct ble_gatt_svc_def saki_ble_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = saki_ble_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                .val_handle = &s_rx_handle,
                .min_key_size = 16,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = saki_ble_gatt_access,
                .flags = BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
                .val_handle = &s_tx_handle,
                .min_key_size = 16,
            },
            {0},
        },
    },
    {0},
};

static void saki_ble_refresh_advertising(void)
{
    struct ble_gap_adv_params parameters;
    struct ble_hs_adv_fields fields;
    ble_addr_t bonded_peer;
    bool connected;
    bool synced;
    bool bond_present;
    bool manual_paused;
    bool pairing_window;
    uint64_t pairing_deadline_ms;
    int rc;

    portENTER_CRITICAL(&s_state_lock);
    connected = s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
    synced = s_synced;
    bond_present = s_bond_present;
    manual_paused = s_manual_paused;
    pairing_window = s_pairing_window;
    pairing_deadline_ms = s_pairing_deadline_ms;
    portEXIT_CRITICAL(&s_state_lock);

    if (!synced || connected) {
        return;
    }
    if (ble_gap_adv_active()) {
        (void)ble_gap_adv_stop();
    }
    if (bond_present && manual_paused) {
        ESP_LOGI(TAG, "BLE advertising paused by K2");
        saki_ble_emit_event(SAKI_BLE_EVENT_PAUSED, 0);
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)SAKI_BLE_NAME;
    fields.name_len = sizeof(SAKI_BLE_NAME) - 1;
    fields.name_is_complete = 1;
    fields.uuids128 = &s_service_uuid.u128;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "set advertising data failed: %d", rc);
        return;
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    if (!bond_present && pairing_window) {
        parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
        parameters.filter_policy = BLE_HCI_ADV_FILT_NONE;
    } else if (bond_present) {
        if (!saki_ble_load_bond(&bonded_peer) ||
            ble_gap_wl_set(&bonded_peer, 1) != 0) {
            ESP_LOGE(TAG, "bond allow-list setup failed; advertising safely disabled");
            return;
        }
        parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
        parameters.filter_policy = BLE_HCI_ADV_FILT_CONN;
    } else {
        parameters.conn_mode = BLE_GAP_CONN_MODE_NON;
        parameters.filter_policy = BLE_HCI_ADV_FILT_NONE;
    }
    rc = ble_gap_adv_start(
        s_own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &parameters,
        saki_ble_gap_event,
        NULL
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "start advertising failed: %d", rc);
    } else if (pairing_window) {
        uint64_t now_ms = saki_ble_now_ms();
        uint64_t remaining_ms = pairing_deadline_ms > now_ms
            ? pairing_deadline_ms - now_ms
            : 0;
        saki_ble_emit_event(
            SAKI_BLE_EVENT_PAIRING_WINDOW,
            remaining_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining_ms
        );
    } else if (bond_present) {
        saki_ble_emit_event(SAKI_BLE_EVENT_WAITING, 0);
    }
}

static void saki_ble_maybe_report_ready(void)
{
    bool notify_ready = false;
    bool connecting = false;

    portENTER_CRITICAL(&s_state_lock);
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_secure &&
        s_subscribed && !s_ready_notified) {
        s_ready_notified = true;
        notify_ready = true;
    } else if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
               !s_ready_notified) {
        connecting = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (notify_ready && s_connection_changed != NULL) {
        s_connection_changed(true, s_callback_context);
    }
    if (notify_ready) {
        saki_ble_emit_event(SAKI_BLE_EVENT_CONNECTED, 0);
    } else if (connecting) {
        saki_ble_emit_event(SAKI_BLE_EVENT_CONNECTING, 0);
    }
}

static void saki_ble_handle_button(saki_button_event_t event)
{
    uint16_t conn_handle;
    bool bond_present;

    portENTER_CRITICAL(&s_state_lock);
    conn_handle = s_conn_handle;
    bond_present = s_bond_present;
    portEXIT_CRITICAL(&s_state_lock);

    switch (event) {
    case SAKI_BUTTON_EVENT_SHORT_RELEASE:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            portENTER_CRITICAL(&s_state_lock);
            s_manual_paused = true;
            portEXIT_CRITICAL(&s_state_lock);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            saki_ble_emit_event(SAKI_BLE_EVENT_PAUSED, 0);
        } else if (bond_present) {
            portENTER_CRITICAL(&s_state_lock);
            s_manual_paused = false;
            portEXIT_CRITICAL(&s_state_lock);
            saki_ble_refresh_advertising();
        } else {
            ESP_LOGI(TAG, "No BLE bond; hold K2 for 2 seconds to pair");
            saki_ble_emit_event(SAKI_BLE_EVENT_NO_BOND, 0);
        }
        break;
    case SAKI_BUTTON_EVENT_PAIR_RELEASE:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_paused = false;
        if (!s_bond_present) {
            s_pairing_window = true;
            s_pairing_deadline_ms =
                saki_ble_now_ms() + SAKI_BLE_PAIRING_WINDOW_MS;
        }
        portEXIT_CRITICAL(&s_state_lock);
        saki_ble_refresh_advertising();
        if (bond_present) {
            saki_ble_emit_event(SAKI_BLE_EVENT_ALREADY_BONDED, 0);
            ESP_LOGI(TAG, "BLE pairing request ignored because a bond exists");
        } else {
            ESP_LOGI(TAG, "BLE pairing window requested");
        }
        break;
    case SAKI_BUTTON_EVENT_CLEAR_THRESHOLD:
        portENTER_CRITICAL(&s_state_lock);
        s_pairing_window = false;
        s_pairing_deadline_ms = 0;
        s_manual_paused = false;
        portEXIT_CRITICAL(&s_state_lock);
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (ble_store_clear() != 0) {
            ESP_LOGE(TAG, "failed to clear BLE bond store");
            break;
        }
        portENTER_CRITICAL(&s_state_lock);
        s_bond_present = false;
        portEXIT_CRITICAL(&s_state_lock);
        saki_ble_refresh_advertising();
        saki_ble_emit_event(SAKI_BLE_EVENT_BONDS_CLEARED, 0);
        ESP_LOGI(TAG, "All BLE bonds cleared by K2");
        break;
    case SAKI_BUTTON_EVENT_NONE:
    default:
        break;
    }
}

static bool saki_ble_pairing_window_tick(void)
{
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    uint64_t now_ms = saki_ble_now_ms();
    bool expired = false;

    portENTER_CRITICAL(&s_state_lock);
    if (s_pairing_window && now_ms >= s_pairing_deadline_ms) {
        s_pairing_window = false;
        s_pairing_deadline_ms = 0;
        if (!s_secure) {
            conn_handle = s_conn_handle;
        }
        expired = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return expired;
}

static void saki_ble_process_tx(const saki_ble_tx_message_t *message)
{
    size_t offset = 0;

    while (offset < message->length) {
        struct os_mbuf *packet;
        uint16_t conn_handle;
        uint16_t mtu;
        size_t chunk_length;
        bool ready;
        int rc;

        portENTER_CRITICAL(&s_state_lock);
        conn_handle = s_conn_handle;
        ready = s_secure && s_subscribed && s_ready_notified;
        portEXIT_CRITICAL(&s_state_lock);
        if (!ready || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            portENTER_CRITICAL(&s_state_lock);
            saki_ble_increment(&s_diagnostics.tx_drops);
            portEXIT_CRITICAL(&s_state_lock);
            return;
        }
        mtu = ble_att_mtu(conn_handle);
        if (mtu < SAKI_BLE_MIN_ATT_MTU || mtu > SAKI_BLE_MAX_ATT_MTU) {
            mtu = SAKI_BLE_MIN_ATT_MTU;
        }
        chunk_length = message->length - offset;
        if (chunk_length > (size_t)(mtu - 3U)) {
            chunk_length = mtu - 3U;
        }
        packet = ble_hs_mbuf_from_flat(message->data + offset, chunk_length);
        if (packet == NULL) {
            portENTER_CRITICAL(&s_state_lock);
            saki_ble_increment(&s_diagnostics.tx_drops);
            portEXIT_CRITICAL(&s_state_lock);
            return;
        }
        (void)ulTaskNotifyTake(pdTRUE, 0);
        portENTER_CRITICAL(&s_state_lock);
        s_notify_status = BLE_HS_EUNKNOWN;
        portEXIT_CRITICAL(&s_state_lock);
        rc = ble_gatts_notify_custom(conn_handle, s_tx_handle, packet);
        if (rc != 0 || ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(SAKI_BLE_NOTIFY_TIMEOUT_MS)
            ) == 0) {
            portENTER_CRITICAL(&s_state_lock);
            saki_ble_increment(&s_diagnostics.tx_drops);
            portEXIT_CRITICAL(&s_state_lock);
            return;
        }
        portENTER_CRITICAL(&s_state_lock);
        rc = s_notify_status;
        portEXIT_CRITICAL(&s_state_lock);
        if (rc != 0) {
            portENTER_CRITICAL(&s_state_lock);
            saki_ble_increment(&s_diagnostics.tx_drops);
            portEXIT_CRITICAL(&s_state_lock);
            return;
        }
        offset += chunk_length;
    }
}

static void saki_ble_worker(void *argument)
{
    uint8_t rx_chunk[256];
    saki_ble_tx_message_t tx_message;
    saki_ble_control_t control;
    (void)argument;

    while (true) {
        if (xQueueReceive(s_control_queue, &control, 0) == pdTRUE) {
            switch (control.type) {
            case SAKI_BLE_CONTROL_REFRESH:
                saki_ble_refresh_advertising();
                break;
            case SAKI_BLE_CONTROL_STATE_CHANGED:
                saki_ble_maybe_report_ready();
                break;
            case SAKI_BLE_CONTROL_DISCONNECTED:
                if (s_connection_changed != NULL) {
                    s_connection_changed(false, s_callback_context);
                }
                saki_ble_refresh_advertising();
                break;
            case SAKI_BLE_CONTROL_DISCONNECT_REQUEST: {
                uint16_t conn_handle;

                portENTER_CRITICAL(&s_state_lock);
                conn_handle = s_conn_handle;
                portEXIT_CRITICAL(&s_state_lock);
                if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                    (void)ble_gap_terminate(
                        conn_handle,
                        BLE_ERR_REM_USER_CONN_TERM
                    );
                }
                break;
            }
            case SAKI_BLE_CONTROL_BUTTON:
                saki_ble_handle_button(control.button_event);
                break;
            default:
                break;
            }
        }

        size_t received = xStreamBufferReceive(
            s_rx_stream,
            rx_chunk,
            sizeof(rx_chunk),
            pdMS_TO_TICKS(SAKI_BLE_TASK_WAIT_MS)
        );
        if (received > 0 && s_receive != NULL) {
            s_receive(rx_chunk, received, s_callback_context);
        }
        if (xQueueReceive(s_tx_queue, &tx_message, 0) == pdTRUE) {
            saki_ble_process_tx(&tx_message);
        }
        if (saki_ble_pairing_window_tick()) {
            ESP_LOGI(TAG, "BLE pairing window expired");
            saki_ble_refresh_advertising();
            saki_ble_emit_event(SAKI_BLE_EVENT_PAIRING_EXPIRED, 0);
        }
        if (s_poll != NULL) {
            s_poll(s_callback_context);
        }
    }
}

static void saki_ble_on_reset(int reason)
{
    portENTER_CRITICAL(&s_state_lock);
    s_synced = false;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGE(TAG, "NimBLE host reset: %d", reason);
}

static void saki_ble_on_sync(void)
{
    bool bond_present;
    int rc = ble_hs_util_ensure_addr(0);

    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "NimBLE address initialization failed: %d", rc);
        return;
    }
    bond_present = saki_ble_load_bond(NULL);
    portENTER_CRITICAL(&s_state_lock);
    s_bond_present = bond_present;
    s_synced = true;
    portEXIT_CRITICAL(&s_state_lock);
    saki_ble_post_control(SAKI_BLE_CONTROL_REFRESH, SAKI_BUTTON_EVENT_NONE);
}

static int saki_ble_gap_event(struct ble_gap_event *event, void *argument)
{
    struct ble_gap_conn_desc description;
    bool authorized;
    int rc;
    (void)argument;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            saki_ble_post_control(
                SAKI_BLE_CONTROL_REFRESH,
                SAKI_BUTTON_EVENT_NONE
            );
            return 0;
        }
        rc = ble_gap_conn_find(event->connect.conn_handle, &description);
        if (rc != 0) {
            (void)ble_gap_terminate(
                event->connect.conn_handle,
                BLE_ERR_REM_USER_CONN_TERM
            );
            return 0;
        }
        portENTER_CRITICAL(&s_state_lock);
        authorized = !s_bond_present && s_pairing_window;
        if (s_bond_present) {
            authorized = true;
        }
        portEXIT_CRITICAL(&s_state_lock);
        if (authorized && saki_ble_bond_present()) {
            authorized = saki_ble_peer_is_bonded(&description.peer_id_addr);
        }
        portENTER_CRITICAL(&s_state_lock);
        if (authorized) {
            s_conn_handle = event->connect.conn_handle;
            s_secure = description.sec_state.encrypted &&
                       description.sec_state.bonded;
            s_subscribed = false;
            s_ready_notified = false;
        } else {
            saki_ble_increment(&s_diagnostics.pairing_rejections);
        }
        portEXIT_CRITICAL(&s_state_lock);
        saki_ble_post_control(
            SAKI_BLE_CONTROL_STATE_CHANGED,
            SAKI_BUTTON_EVENT_NONE
        );
        if (!authorized) {
            (void)ble_gap_terminate(
                event->connect.conn_handle,
                BLE_ERR_REM_USER_CONN_TERM
            );
            return 0;
        }
        rc = ble_gap_security_initiate(event->connect.conn_handle);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "start link security failed: %d", rc);
        }
        saki_ble_post_control(
            SAKI_BLE_CONTROL_STATE_CHANGED,
            SAKI_BUTTON_EVENT_NONE
        );
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&s_state_lock);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_secure = false;
        s_subscribed = false;
        s_ready_notified = false;
        saki_ble_increment(&s_diagnostics.disconnects);
        portEXIT_CRITICAL(&s_state_lock);
        if (s_rx_stream != NULL) {
            xStreamBufferReset(s_rx_stream);
        }
        if (s_tx_queue != NULL) {
            xQueueReset(s_tx_queue);
        }
        saki_ble_post_control(
            SAKI_BLE_CONTROL_DISCONNECTED,
            SAKI_BUTTON_EVENT_NONE
        );
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &description);
        portENTER_CRITICAL(&s_state_lock);
        s_secure = event->enc_change.status == 0 && rc == 0 &&
                   description.sec_state.encrypted &&
                   description.sec_state.bonded;
        if (s_secure) {
            s_bond_present = true;
            s_pairing_window = false;
            s_pairing_deadline_ms = 0;
        } else {
            saki_ble_increment(&s_diagnostics.security_rejections);
        }
        portEXIT_CRITICAL(&s_state_lock);
        if (!s_secure) {
            (void)ble_gap_terminate(
                event->enc_change.conn_handle,
                BLE_ERR_REM_USER_CONN_TERM
            );
        } else {
            saki_ble_post_control(
                SAKI_BLE_CONTROL_STATE_CHANGED,
                SAKI_BUTTON_EVENT_NONE
            );
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            portENTER_CRITICAL(&s_state_lock);
            s_subscribed = event->subscribe.cur_notify != 0;
            portEXIT_CRITICAL(&s_state_lock);
            saki_ble_post_control(
                SAKI_BLE_CONTROL_STATE_CHANGED,
                SAKI_BUTTON_EVENT_NONE
            );
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.attr_handle == s_tx_handle) {
            portENTER_CRITICAL(&s_state_lock);
            s_notify_status = event->notify_tx.status;
            portEXIT_CRITICAL(&s_state_lock);
            if (s_worker_task != NULL) {
                xTaskNotifyGive(s_worker_task);
            }
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        portENTER_CRITICAL(&s_state_lock);
        saki_ble_increment(&s_diagnostics.pairing_rejections);
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        saki_ble_post_control(
            SAKI_BLE_CONTROL_REFRESH,
            SAKI_BUTTON_EVENT_NONE
        );
        return 0;

    default:
        return 0;
    }
}

static void saki_ble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void saki_ble_release_startup_resources(void)
{
    if (s_rx_stream != NULL) {
        vStreamBufferDelete(s_rx_stream);
        s_rx_stream = NULL;
    }
    if (s_tx_queue != NULL) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    if (s_control_queue != NULL) {
        vQueueDelete(s_control_queue);
        s_control_queue = NULL;
    }
    s_worker_task = NULL;
    s_receive = NULL;
    s_connection_changed = NULL;
    s_poll = NULL;
    s_event_received = NULL;
    s_callback_context = NULL;
}

esp_err_t saki_ble_start(
    saki_ble_rx_fn receive,
    saki_ble_connection_fn connection_changed,
    saki_ble_poll_fn poll,
    saki_ble_event_fn event_received,
    void *context
)
{
    BaseType_t task_created;
    esp_err_t result;
    int rc;

    if (receive == NULL || s_rx_stream != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_receive = receive;
    s_connection_changed = connection_changed;
    s_poll = poll;
    s_event_received = event_received;
    s_callback_context = context;
    s_rx_stream = xStreamBufferCreate(SAKI_BLE_RX_STREAM_BYTES, 1);
    s_tx_queue = xQueueCreate(
        SAKI_BLE_TX_QUEUE_LENGTH,
        sizeof(saki_ble_tx_message_t)
    );
    s_control_queue = xQueueCreate(
        SAKI_BLE_CONTROL_QUEUE_LENGTH,
        sizeof(saki_ble_control_t)
    );
    if (s_rx_stream == NULL || s_tx_queue == NULL || s_control_queue == NULL) {
        saki_ble_release_startup_resources();
        return ESP_ERR_NO_MEM;
    }

    result = nimble_port_init();
    if (result != ESP_OK) {
        saki_ble_release_startup_resources();
        return result;
    }
    ble_hs_cfg.reset_cb = saki_ble_on_reset;
    ble_hs_cfg.sync_cb = saki_ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    /* NimBLE's SC-only runtime mode enforces authenticated level 4, which is
     * incompatible with the requested NoIO Just Works flow. Legacy pairing is
     * still compiled out, so level 2 remains encrypted LESC without MITM. */
    ble_hs_cfg.sm_sc_only = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = saki_ble_initialize_uuids();
    if (rc == 0) {
        rc = ble_gatts_count_cfg(saki_ble_services);
    }
    if (rc == 0) {
        rc = ble_gatts_add_svcs(saki_ble_services);
    }
    if (rc != 0) {
        (void)nimble_port_deinit();
        saki_ble_release_startup_resources();
        return ESP_FAIL;
    }
    rc = ble_svc_gap_device_name_set(SAKI_BLE_NAME);
    if (rc != 0) {
        (void)nimble_port_deinit();
        saki_ble_release_startup_resources();
        return ESP_FAIL;
    }
    ble_store_config_init();

    task_created = xTaskCreate(
        saki_ble_worker,
        "saki_ble",
        SAKI_BLE_TASK_STACK_BYTES,
        NULL,
        SAKI_BLE_TASK_PRIORITY,
        &s_worker_task
    );
    if (task_created != pdPASS) {
        (void)nimble_port_deinit();
        saki_ble_release_startup_resources();
        return ESP_ERR_NO_MEM;
    }
    nimble_port_freertos_init(saki_ble_host_task);
    ESP_LOGI(TAG, "NimBLE GATT server started");
    return ESP_OK;
}

esp_err_t saki_ble_transmit(
    const uint8_t *data,
    size_t length,
    void *context
)
{
    saki_ble_tx_message_t message;
    bool ready;
    (void)context;

    if (data == NULL || length == 0 || length > sizeof(message.data)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_state_lock);
    ready = s_ready_notified;
    portEXIT_CRITICAL(&s_state_lock);
    if (!ready || s_tx_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    message.length = length;
    memcpy(message.data, data, length);
    return xQueueSend(s_tx_queue, &message, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

esp_err_t saki_ble_button_event(saki_button_event_t event)
{
    saki_ble_control_t control = {
        .type = SAKI_BLE_CONTROL_BUTTON,
        .button_event = event,
    };

    if (event == SAKI_BUTTON_EVENT_NONE) {
        return ESP_OK;
    }
    if (s_control_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_control_queue, &control, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

esp_err_t saki_ble_disconnect(void)
{
    saki_ble_control_t control = {
        .type = SAKI_BLE_CONTROL_DISCONNECT_REQUEST,
        .button_event = SAKI_BUTTON_EVENT_NONE,
    };

    if (s_control_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_control_queue, &control, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool saki_ble_ready(void)
{
    bool ready;

    portENTER_CRITICAL(&s_state_lock);
    ready = s_ready_notified;
    portEXIT_CRITICAL(&s_state_lock);
    return ready;
}

bool saki_ble_bond_present(void)
{
    bool present;

    portENTER_CRITICAL(&s_state_lock);
    present = s_bond_present;
    portEXIT_CRITICAL(&s_state_lock);
    return present;
}

bool saki_ble_pairing_window_open(void)
{
    bool open;

    portENTER_CRITICAL(&s_state_lock);
    open = s_pairing_window;
    portEXIT_CRITICAL(&s_state_lock);
    return open;
}

uint32_t saki_ble_stack_high_watermark_bytes(void)
{
    TaskHandle_t task = s_worker_task;

    return task == NULL ? 0 : (uint32_t)uxTaskGetStackHighWaterMark(task);
}

void saki_ble_get_diagnostics(saki_ble_diagnostics_t *diagnostics)
{
    if (diagnostics == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_state_lock);
    *diagnostics = s_diagnostics;
    portEXIT_CRITICAL(&s_state_lock);
}
