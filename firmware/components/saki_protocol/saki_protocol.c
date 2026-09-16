#include "saki_protocol.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

#define SAKI_PROTOCOL_VERSION 1
#define SAKI_PROTOCOL_MAX_INVALID_STREAK 5

/* Bound cJSON recursion and reject embedded NULs before allocating a tree. */
static bool saki_json_frame_bounded(const char *frame, size_t length)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = frame[i];
        if (ch == 0) return false;
        if (quoted) {
            if (escaped) {
                /* cJSON strings are NUL terminated; escaped NUL would truncate identity. */
                if (ch == 'u' && i + 4 < length && memcmp(frame + i + 1, "0000", 4) == 0) return false;
                escaped = false;
            } else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 16) return false; }
        else if (ch == '}' || ch == ']') { if (depth == 0) return false; --depth; }
    }
    return depth == 0 && !quoted;
}

static void saki_protocol_handle_frame(
    const char *frame,
    size_t length,
    void *context
);
static void saki_protocol_handle_framer_error(
    saki_framer_error_t error,
    void *context
);

static void saki_protocol_increment(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        ++(*counter);
    }
}

void saki_ndjson_framer_init(saki_ndjson_framer_t *framer)
{
    if (framer == NULL) {
        return;
    }
    memset(framer, 0, sizeof(*framer));
}

void saki_ndjson_framer_feed(
    saki_ndjson_framer_t *framer,
    const uint8_t *data,
    size_t length,
    saki_framer_frame_fn frame_callback,
    saki_framer_error_fn error_callback,
    void *context
)
{
    if (framer == NULL || data == NULL) {
        return;
    }

    for (size_t index = 0; index < length; ++index) {
        uint8_t byte = data[index];
        if (byte == '\n') {
            if (framer->discarding) {
                framer->discarding = false;
                framer->length = 0;
                continue;
            }
            if (framer->length > 0 && framer->frame[framer->length - 1] == '\r') {
                --framer->length;
            }
            if (framer->length > 0 && frame_callback != NULL) {
                framer->frame[framer->length] = '\0';
                frame_callback(framer->frame, framer->length, context);
            }
            framer->length = 0;
            continue;
        }

        if (framer->discarding) {
            continue;
        }
        if (framer->length == SAKI_PROTOCOL_MAX_FRAME) {
            framer->discarding = true;
            framer->length = 0;
            if (error_callback != NULL) {
                error_callback(SAKI_FRAMER_FRAME_TOO_LARGE, context);
            }
            continue;
        }
        framer->frame[framer->length++] = (char)byte;
    }
}

static bool saki_copy_json_text(
    const cJSON *object,
    const char *key,
    char *destination,
    size_t capacity,
    bool required,
    bool allow_empty
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    saki_utf8_copy_result_t copy_result;

    destination[0] = '\0';
    if (item == NULL) {
        return !required;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    if (!allow_empty && item->valuestring[0] == '\0') {
        return false;
    }
    copy_result = saki_utf8_copy(destination, capacity, item->valuestring);
    return copy_result == SAKI_UTF8_COPY_OK;
}

static bool saki_json_uint32(const cJSON *object, const char *key, uint32_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    double number;

    if (!cJSON_IsNumber(item)) {
        return false;
    }
    number = item->valuedouble;
    if (!isfinite(number) || number < 0 || number > UINT32_MAX || floor(number) != number) {
        return false;
    }
    *value = (uint32_t)number;
    return true;
}

static bool saki_json_uint64_optional(
    const cJSON *object,
    const char *key,
    uint64_t *value
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    double number;

    *value = 0;
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    number = item->valuedouble;
    if (!isfinite(number) || number < 0 || number > 9007199254740991.0 ||
        floor(number) != number) {
        return false;
    }
    *value = (uint64_t)number;
    return true;
}

static bool saki_uuid_valid(const char *value)
{
    static const size_t hyphens[] = {8, 13, 18, 23};

    if (strlen(value) != 36) {
        return false;
    }
    for (size_t index = 0; index < 36; ++index) {
        bool is_hyphen = false;
        for (size_t hyphen = 0; hyphen < sizeof(hyphens) / sizeof(hyphens[0]); ++hyphen) {
            if (index == hyphens[hyphen]) {
                is_hyphen = true;
                break;
            }
        }
        if (is_hyphen) {
            if (value[index] != '-') {
                return false;
            }
        } else if (!((value[index] >= '0' && value[index] <= '9') ||
                     (value[index] >= 'a' && value[index] <= 'f') ||
                     (value[index] >= 'A' && value[index] <= 'F'))) {
            return false;
        }
    }
    return true;
}

static esp_err_t saki_protocol_send(
    saki_protocol_engine_t *engine,
    const char *message,
    int length
)
{
    esp_err_t result;

    if (length < 0 || length >= SAKI_PROTOCOL_TX_CAPACITY) {
        saki_protocol_increment(&engine->diagnostics.tx_drops);
        return ESP_ERR_INVALID_SIZE;
    }
    result = engine->transmit(
        (const uint8_t *)message,
        (size_t)length,
        engine->callback_context
    );
    if (result != ESP_OK) {
        saki_protocol_increment(&engine->diagnostics.tx_drops);
    }
    return result;
}

static uint32_t saki_protocol_next_id(saki_protocol_engine_t *engine)
{
    uint32_t value = engine->next_id;
    ++engine->next_id;
    return value;
}

static void saki_protocol_reset_session(saki_protocol_engine_t *engine)
{
    engine->handshaken = false;
    engine->multi_session = false;
    engine->has_committed_display = false;
    engine->has_last_seq = false;
    engine->has_last_activity = false;
    engine->last_seq = 0;
    engine->last_activity_ms = 0;
    engine->session[0] = '\0';
}

static void saki_protocol_mark_activity(saki_protocol_engine_t *engine)
{
    engine->last_activity_ms = (uint64_t)(esp_timer_get_time() / 1000);
    engine->has_last_activity = true;
}

static void saki_protocol_invalid(saki_protocol_engine_t *engine)
{
    saki_protocol_increment(&engine->diagnostics.invalid_frames);
    if (engine->invalid_streak != UINT8_MAX) {
        ++engine->invalid_streak;
    }
    if (engine->invalid_streak >= SAKI_PROTOCOL_MAX_INVALID_STREAK) {
        engine->invalid_streak = 0;
        saki_protocol_reset_session(engine);
    }
}

static void saki_protocol_send_error(
    saki_protocol_engine_t *engine,
    bool has_reply,
    uint32_t reply_to,
    const char *code,
    const char *message,
    bool count_invalid
)
{
    char response[SAKI_PROTOCOL_TX_CAPACITY];
    int length;
    uint32_t id = saki_protocol_next_id(engine);

    if (has_reply) {
        length = snprintf(
            response,
            sizeof(response),
            "{\"v\":1,\"type\":\"error\",\"id\":%" PRIu32
            ",\"reply_to\":%" PRIu32 ",\"code\":\"%s\",\"message\":\"%s\"}\n",
            id,
            reply_to,
            code,
            message
        );
    } else {
        length = snprintf(
            response,
            sizeof(response),
            "{\"v\":1,\"type\":\"error\",\"id\":%" PRIu32
            ",\"code\":\"%s\",\"message\":\"%s\"}\n",
            id,
            code,
            message
        );
    }
    (void)saki_protocol_send(engine, response, length);
    if (count_invalid) {
        saki_protocol_invalid(engine);
    }
}

static void saki_protocol_send_ack(
    saki_protocol_engine_t *engine,
    uint32_t reply_to,
    bool applied
)
{
    char response[SAKI_PROTOCOL_TX_CAPACITY];
    int length = snprintf(
        response,
        sizeof(response),
        "{\"v\":1,\"type\":\"ack\",\"id\":%" PRIu32
        ",\"reply_to\":%" PRIu32 ",\"ok\":true,\"applied\":%s,\"last_seq\":%" PRIu32 "}\n",
        saki_protocol_next_id(engine),
        reply_to,
        applied ? "true" : "false",
        engine->last_seq
    );
    (void)saki_protocol_send(engine, response, length);
}

static bool saki_protocol_session_matches(
    const saki_protocol_engine_t *engine,
    const cJSON *root
)
{
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
    return cJSON_IsString(session) && session->valuestring != NULL &&
           strcmp(session->valuestring, engine->session) == 0;
}

static bool saki_protocol_seq_is_new(
    const saki_protocol_engine_t *engine,
    uint32_t sequence
)
{
    return !engine->has_last_seq || (int32_t)(sequence - engine->last_seq) > 0;
}

static bool saki_protocol_parse_state_name(
    const cJSON *root,
    saki_agent_state_t *state
)
{
    static const struct {
        const char *name;
        saki_agent_state_t state;
    } states[] = {
        {"idle", SAKI_AGENT_IDLE},
        {"starting", SAKI_AGENT_STARTING},
        {"thinking", SAKI_AGENT_THINKING},
        {"working", SAKI_AGENT_WORKING},
        {"waiting_user", SAKI_AGENT_WAITING_USER},
        {"waiting_approval", SAKI_AGENT_WAITING_APPROVAL},
        {"completed", SAKI_AGENT_COMPLETED},
        {"failed", SAKI_AGENT_FAILED},
        {"cancelled", SAKI_AGENT_CANCELLED},
    };
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "state");

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        if (strcmp(item->valuestring, states[index].name) == 0) {
            *state = states[index].state;
            return true;
        }
    }
    return false;
}

static bool saki_protocol_parse_snapshot(
    const saki_protocol_engine_t *engine,
    const cJSON *root,
    saki_state_snapshot_t *snapshot
)
{
    const cJSON *task;
    const cJSON *activity;
    const cJSON *progress;
    const cJSON *agent;
    const cJSON *mode;
    const cJSON *percent;

    saki_state_snapshot_init(snapshot);
    snapshot->connected = true;
    snprintf(
        snapshot->transport,
        sizeof(snapshot->transport),
        "%s",
        saki_transport_name(engine->transport)
    );

    if (!saki_protocol_parse_state_name(root, &snapshot->state) ||
        !saki_json_uint64_optional(root, "elapsed_ms", &snapshot->elapsed_ms)) {
        return false;
    }

    task = cJSON_GetObjectItemCaseSensitive(root, "task");
    if (snapshot->state != SAKI_AGENT_IDLE && !cJSON_IsObject(task)) {
        return false;
    }
    if (task != NULL) {
        if (!cJSON_IsObject(task) ||
            !saki_copy_json_text(
                task,
                "id",
                snapshot->task_id,
                sizeof(snapshot->task_id),
                true,
                false
            ) ||
            !saki_copy_json_text(
                task,
                "title",
                snapshot->task_title,
                sizeof(snapshot->task_title),
                true,
                false
            )) {
            return false;
        }
    }

    activity = cJSON_GetObjectItemCaseSensitive(root, "activity");
    if (activity != NULL &&
        (!cJSON_IsObject(activity) ||
         !saki_copy_json_text(
             activity,
             "summary",
             snapshot->activity,
             sizeof(snapshot->activity),
             false,
             true
         ) ||
         !saki_copy_json_text(
             activity,
             "detail",
             snapshot->detail,
             sizeof(snapshot->detail),
             false,
             true
         ))) {
        return false;
    }

    progress = cJSON_GetObjectItemCaseSensitive(root, "progress");
    if (progress != NULL) {
        if (!cJSON_IsObject(progress)) {
            return false;
        }
        mode = cJSON_GetObjectItemCaseSensitive(progress, "mode");
        if (!cJSON_IsString(mode) || mode->valuestring == NULL) {
            return false;
        }
        if (strcmp(mode->valuestring, "none") == 0) {
            snapshot->progress_mode = SAKI_PROGRESS_NONE;
        } else if (strcmp(mode->valuestring, "indeterminate") == 0) {
            snapshot->progress_mode = SAKI_PROGRESS_INDETERMINATE;
        } else if (strcmp(mode->valuestring, "determinate") == 0) {
            snapshot->progress_mode = SAKI_PROGRESS_DETERMINATE;
            percent = cJSON_GetObjectItemCaseSensitive(progress, "percent");
            if (!cJSON_IsNumber(percent) || !isfinite(percent->valuedouble) ||
                percent->valuedouble < 0 ||
                percent->valuedouble > 100 || floor(percent->valuedouble) != percent->valuedouble) {
                return false;
            }
            snapshot->progress_percent = (uint8_t)percent->valuedouble;
        } else {
            return false;
        }
        if (!saki_copy_json_text(
                progress,
                "label",
                snapshot->progress_label,
                sizeof(snapshot->progress_label),
                false,
                true
            )) {
            return false;
        }
    }

    agent = cJSON_GetObjectItemCaseSensitive(root, "agent");
    if (agent != NULL &&
        (!cJSON_IsObject(agent) ||
         !saki_copy_json_text(
             agent,
             "name",
             snapshot->agent_name,
             sizeof(snapshot->agent_name),
             false,
             true
         ) ||
         !saki_copy_json_text(
             agent,
             "model",
             snapshot->model_name,
             sizeof(snapshot->model_name),
             false,
             true
         ))) {
        return false;
    }
    return true;
}

static void saki_protocol_handle_hello(
    saki_protocol_engine_t *engine,
    const cJSON *root,
    uint32_t request_id
)
{
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    bool multi = cJSON_IsString(mode) && strcmp(mode->valuestring, "multi-session") == 0;
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(root, "role");
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
    char response[SAKI_PROTOCOL_TX_CAPACITY];
    int length;

    if (!cJSON_IsString(role) || role->valuestring == NULL ||
        strcmp(role->valuestring, "host") != 0 ||
        !cJSON_IsString(session) || session->valuestring == NULL ||
        !saki_uuid_valid(session->valuestring)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "hello role or session is invalid",
            true
        );
        return;
    }

    if (mode != NULL && (!multi || engine->submit_display == NULL)) {
        saki_protocol_send_error(engine, true, request_id, "invalid_field", "unsupported mode", true);
        return;
    }
    engine->multi_session = multi;
    engine->has_committed_display = false;
    snprintf(engine->session, sizeof(engine->session), "%s", session->valuestring);
    saki_protocol_increment(&engine->diagnostics.valid_frames);
    engine->handshaken = true;
    engine->has_last_seq = false;
    engine->last_seq = 0;
    engine->invalid_streak = 0;
    saki_protocol_mark_activity(engine);

    length = snprintf(
        response,
        sizeof(response),
        "{\"v\":1,\"type\":\"hello\",\"id\":%" PRIu32
        ",\"reply_to\":%" PRIu32
        ",\"role\":\"device\",\"device\":{\"name\":\"saki-box3\",\"fw\":\"%s\",\"id\":\"%s\"},"
        "\"screen\":{\"width\":320,\"height\":240},"
        "\"capabilities\":[\"status\",\"progress\",\"utf8\",\"touch-detail\"%s%s%s%s]%s}\n",
        saki_protocol_next_id(engine),
        request_id,
        engine->firmware_version,
        engine->device_id,
        (engine->capability_flags & SAKI_PROTOCOL_CAPABILITY_BLE) != 0
            ? ",\"ble\""
            : "",
        (engine->capability_flags & SAKI_PROTOCOL_CAPABILITY_SECURE_CONNECTION) != 0
            ? ",\"secure-connection\""
            : "",
        (engine->capability_flags &
         SAKI_PROTOCOL_CAPABILITY_TRANSPORT_ARBITRATION) != 0
            ? ",\"transport-arbitration\""
            : "",
        engine->submit_display != NULL ? ",\"multi-session\"" : "",
        multi ? ",\"mode\":\"multi-session\"" : ""
    );
    (void)saki_protocol_send(engine, response, length);
}

static void saki_protocol_submit_snapshot(
    saki_protocol_engine_t *engine,
    const saki_state_snapshot_t *snapshot,
    uint32_t request_id,
    uint32_t sequence
)
{
    saki_transport_outcome_t outcome;

    if (engine->submit_candidate == NULL) {
        if (engine->apply_state(snapshot, engine->callback_context) != ESP_OK) {
            saki_protocol_send_error(
                engine,
                true,
                request_id,
                "busy",
                "display queue is unavailable",
                false
            );
            return;
        }
        engine->last_seq = sequence;
        engine->has_last_seq = true;
        engine->invalid_streak = 0;
        saki_protocol_send_ack(engine, request_id, true);
        return;
    }

    outcome = engine->submit_candidate(
        snapshot,
        engine->transport,
        engine->session,
        sequence,
        engine->last_activity_ms,
        engine->callback_context
    );
    if (outcome.has_last_seq) {
        engine->last_seq = outcome.last_seq;
        engine->has_last_seq = true;
    }
    engine->invalid_streak = 0;
    switch (outcome.result) {
    case SAKI_TRANSPORT_APPLIED:
        saki_protocol_send_ack(engine, request_id, true);
        break;
    case SAKI_TRANSPORT_STALE:
        saki_protocol_increment(&engine->diagnostics.old_sequences);
        saki_protocol_send_ack(engine, request_id, false);
        break;
    case SAKI_TRANSPORT_BUSY:
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "busy",
            "higher-priority transport is active",
            false
        );
        break;
    case SAKI_TRANSPORT_APPLY_FAILED:
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "busy",
            "display queue is unavailable",
            false
        );
        break;
    case SAKI_TRANSPORT_INVALID:
    default:
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "transport candidate is invalid",
            false
        );
        break;
    }
}

static void saki_protocol_handle_status(
    saki_protocol_engine_t *engine,
    const cJSON *root,
    uint32_t request_id
)
{
    const cJSON *state_item;
    uint32_t sequence;
    saki_state_snapshot_t snapshot;
    saki_agent_state_t parsed_state;

    if (engine->multi_session) {
        saki_protocol_send_error(engine, true, request_id, "invalid_field", "legacy write in multi mode", false);
        return;
    }
    if (!engine->handshaken) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "not_handshaken",
            "hello is required before status",
            true
        );
        return;
    }
    if (!saki_protocol_session_matches(engine, root) ||
        !saki_json_uint32(root, "seq", &sequence)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "status session or seq is invalid",
            true
        );
        return;
    }
    if (engine->submit_candidate == NULL &&
        !saki_protocol_seq_is_new(engine, sequence)) {
        saki_protocol_increment(&engine->diagnostics.valid_frames);
        saki_protocol_increment(&engine->diagnostics.old_sequences);
        saki_protocol_mark_activity(engine);
        engine->invalid_streak = 0;
        saki_protocol_send_ack(engine, request_id, false);
        return;
    }
    state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (state_item == NULL) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "missing_field",
            "status state is required",
            true
        );
        return;
    }
    if (!cJSON_IsString(state_item) || state_item->valuestring == NULL) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "status state must be a string",
            true
        );
        return;
    }
    if (!saki_protocol_parse_state_name(root, &parsed_state)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_state",
            "unsupported state value",
            true
        );
        return;
    }
    if (!saki_protocol_parse_snapshot(engine, root, &snapshot)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "status snapshot is invalid",
            true
        );
        return;
    }
    saki_protocol_increment(&engine->diagnostics.valid_frames);
    saki_protocol_mark_activity(engine);
    snapshot.received_at_ms = engine->last_activity_ms;
    saki_protocol_submit_snapshot(engine, &snapshot, request_id, sequence);
}

static void saki_protocol_handle_sessions(
    saki_protocol_engine_t *engine, const cJSON *root, uint32_t request_id, const char *frame, size_t frame_length
)
{
    uint32_t sequence, total, hidden, rejected;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "sessions");
    saki_display_snapshot_t *display = &engine->display_candidate;
    if (!engine->handshaken || !engine->multi_session || engine->submit_display == NULL) {
        saki_protocol_send_error(engine, true, request_id, "not_handshaken", "multi-session hello required", true);
        return;
    }
    if (!saki_protocol_session_matches(engine, root) ||
        !saki_json_uint32(root, "seq", &sequence) || !cJSON_IsArray(items) ||
        cJSON_GetArraySize(items) > SAKI_DISPLAY_MAX_SESSIONS ||
        !saki_json_uint32(root, "total", &total) || total > 32 ||
        total < (uint32_t)cJSON_GetArraySize(items) ||
        !saki_json_uint32(root, "hidden_attention", &hidden) || hidden > total - cJSON_GetArraySize(items) ||
        !saki_json_uint32(root, "capacity_rejected", &rejected)) {
        goto invalid;
    }
    memset(display, 0, sizeof(*display));
    display->multi_session = true;
    display->connected = true;
    display->count = cJSON_GetArraySize(items);
    display->total = total;
    display->hidden_attention = hidden;
    display->capacity_rejected = rejected;
    snprintf(display->transport, sizeof(display->transport), "%s", saki_transport_name(engine->transport));
    for (uint8_t i = 0; i < display->count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(items, i);
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(item, "source");
        const cJSON *fresh = cJSON_GetObjectItemCaseSensitive(item, "fresh");
        if (!cJSON_IsObject(item) || !saki_protocol_parse_snapshot(engine, item, &display->items[i]) ||
            !cJSON_IsString(source) || !cJSON_IsBool(fresh) ||
            !saki_json_uint32(item, "revision", &display->revisions[i]) ||
            !saki_copy_json_text(item, "run_id", display->run_ids[i], sizeof(display->run_ids[i]), true, false) ||
            display->items[i].task_id[0] == '\0') {
            goto invalid;
        }
        const char *label;
        if (strcmp(source->valuestring, "codex") == 0) label = "Codex";
        else if (strcmp(source->valuestring, "claude_code") == 0) label = "Claude Code";
        else if (strcmp(source->valuestring, "legacy") == 0) label = "Agent";
        else goto invalid;
        if (strcmp(source->valuestring, "legacy") != 0) {
            if (strlen(display->items[i].task_id) != 32) goto invalid;
            for (size_t k = 0; k < 32; ++k) {
                char ch = display->items[i].task_id[k];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) goto invalid;
            }
        }
        for (size_t k = 0; display->run_ids[i][k]; ++k) {
            char ch = display->run_ids[i][k];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_')) goto invalid;
        }
        snprintf(display->items[i].agent_name, sizeof(display->items[i].agent_name), "%s", label);
        display->items[i].stale = !cJSON_IsTrue(fresh);
        for (uint8_t j = 0; j < i; ++j) {
            if (strcmp(display->items[i].task_id, display->items[j].task_id) == 0) goto invalid;
        }
    }
    saki_protocol_mark_activity(engine);
    for (uint8_t i = 0; i < display->count; ++i) display->items[i].received_at_ms = engine->last_activity_ms;
    saki_transport_outcome_t outcome = engine->submit_display(
        display, engine->transport, engine->session, sequence, engine->last_activity_ms, engine->callback_context);
    if (outcome.has_last_seq) {
        engine->last_seq = outcome.last_seq;
        engine->has_last_seq = true;
    }
    bool applied = outcome.result == SAKI_TRANSPORT_APPLIED;
    bool committed = applied || (outcome.result == SAKI_TRANSPORT_STALE &&
        engine->has_committed_display && engine->committed_display_seq == sequence &&
        engine->committed_display_length == frame_length &&
        memcmp(engine->committed_display_frame, frame, frame_length) == 0 && engine->last_seq == sequence);
    if (outcome.result != SAKI_TRANSPORT_APPLIED && outcome.result != SAKI_TRANSPORT_STALE) {
        saki_protocol_send_error(engine, true, request_id, "busy", "display commit rejected", false);
        return;
    }
    if (applied) {
        engine->has_committed_display = true;
        engine->committed_display_seq = sequence;
        engine->committed_display_length = frame_length;
        memcpy(engine->committed_display_frame, frame, frame_length);
    }
    char response[256];
    int length = snprintf(response, sizeof(response),
        "{\"v\":1,\"type\":\"ack\",\"id\":%" PRIu32 ",\"reply_to\":%" PRIu32
        ",\"ok\":true,\"applied\":%s,\"committed\":%s,\"last_seq\":%" PRIu32 "}\n",
        saki_protocol_next_id(engine), request_id, applied ? "true" : "false", committed ? "true" : "false", engine->last_seq);
    (void)saki_protocol_send(engine, response, length);
    engine->invalid_streak = 0;
    saki_protocol_increment(&engine->diagnostics.valid_frames);
    return;
invalid:
    saki_protocol_send_error(engine, true, request_id, "invalid_field", "invalid display set", true);
}

static void saki_protocol_handle_clear(
    saki_protocol_engine_t *engine,
    const cJSON *root,
    uint32_t request_id
)
{
    uint32_t sequence;
    saki_state_snapshot_t snapshot;

    if (engine->multi_session) {
        saki_protocol_send_error(engine, true, request_id, "invalid_field", "legacy write in multi mode", false);
        return;
    }
    if (!engine->handshaken) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "not_handshaken",
            "hello is required before clear",
            true
        );
        return;
    }
    if (!saki_protocol_session_matches(engine, root) ||
        !saki_json_uint32(root, "seq", &sequence)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "clear session or seq is invalid",
            true
        );
        return;
    }
    if (engine->submit_candidate == NULL &&
        !saki_protocol_seq_is_new(engine, sequence)) {
        saki_protocol_increment(&engine->diagnostics.valid_frames);
        saki_protocol_increment(&engine->diagnostics.old_sequences);
        saki_protocol_mark_activity(engine);
        engine->invalid_streak = 0;
        saki_protocol_send_ack(engine, request_id, false);
        return;
    }

    saki_protocol_increment(&engine->diagnostics.valid_frames);
    saki_protocol_mark_activity(engine);
    saki_state_snapshot_init(&snapshot);
    snapshot.connected = true;
    snapshot.received_at_ms = engine->last_activity_ms;
    snprintf(
        snapshot.transport,
        sizeof(snapshot.transport),
        "%s",
        saki_transport_name(engine->transport)
    );
    saki_protocol_submit_snapshot(engine, &snapshot, request_id, sequence);
}

static void saki_protocol_handle_ping(
    saki_protocol_engine_t *engine,
    const cJSON *root,
    uint32_t request_id
)
{
    char response[SAKI_PROTOCOL_TX_CAPACITY];
    saki_runtime_metrics_t runtime = {0};
    int length;
    int appended;

    if (!engine->handshaken || !saki_protocol_session_matches(engine, root)) {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "not_handshaken",
            "active session is required before ping",
            true
        );
        return;
    }
    saki_protocol_increment(&engine->diagnostics.valid_frames);
    saki_protocol_mark_activity(engine);
    engine->invalid_streak = 0;
    length = snprintf(
        response,
        sizeof(response),
        "{\"v\":1,\"type\":\"pong\",\"id\":%" PRIu32
        ",\"reply_to\":%" PRIu32 ",\"uptime_ms\":%" PRIu64
        ",\"last_seq\":%" PRIu32
        ",\"diagnostics\":{\"valid_frames\":%" PRIu32
        ",\"invalid_frames\":%" PRIu32
        ",\"oversized_frames\":%" PRIu32
        ",\"old_sequences\":%" PRIu32
        ",\"ui_queue_overwrites\":%" PRIu32
        ",\"tx_drops\":%" PRIu32
        ",\"heartbeat_timeouts\":%" PRIu32 "}",
        saki_protocol_next_id(engine),
        request_id,
        (uint64_t)(esp_timer_get_time() / 1000),
        engine->last_seq,
        engine->diagnostics.valid_frames,
        engine->diagnostics.invalid_frames,
        engine->diagnostics.oversized_frames,
        engine->diagnostics.old_sequences,
        engine->diagnostics.ui_queue_overwrites,
        engine->diagnostics.tx_drops,
        engine->diagnostics.heartbeat_timeouts
    );
    if (length < 0 || (size_t)length >= sizeof(response)) {
        saki_protocol_increment(&engine->diagnostics.tx_drops);
        return;
    }
    if (engine->collect_runtime != NULL) {
        engine->collect_runtime(&runtime, engine->runtime_context);
        appended = snprintf(
            response + length,
            sizeof(response) - (size_t)length,
            ",\"runtime\":{\"heap_free_bytes\":%" PRIu32
            ",\"heap_min_bytes\":%" PRIu32
            ",\"internal_free_bytes\":%" PRIu32
            ",\"internal_min_bytes\":%" PRIu32
            ",\"app_stack_min_bytes\":%" PRIu32
            ",\"ui_stack_min_bytes\":%" PRIu32
            ",\"usb_stack_min_bytes\":%" PRIu32
            ",\"ble_stack_min_bytes\":%" PRIu32
            ",\"ble_rx_drops\":%" PRIu32
            ",\"ble_tx_drops\":%" PRIu32
            ",\"ble_security_rejections\":%" PRIu32
            ",\"ble_pairing_rejections\":%" PRIu32
            ",\"ble_disconnects\":%" PRIu32
            ",\"transport_switches\":%" PRIu32
            ",\"transport_rejections\":%" PRIu32 "}",
            runtime.heap_free_bytes,
            runtime.heap_min_bytes,
            runtime.internal_free_bytes,
            runtime.internal_min_bytes,
            runtime.app_stack_min_bytes,
            runtime.ui_stack_min_bytes,
            runtime.usb_stack_min_bytes,
            runtime.ble_stack_min_bytes,
            runtime.ble_rx_drops,
            runtime.ble_tx_drops,
            runtime.ble_security_rejections,
            runtime.ble_pairing_rejections,
            runtime.ble_disconnects,
            runtime.transport_switches,
            runtime.transport_rejections
        );
        if (appended < 0 || (size_t)appended >= sizeof(response) - (size_t)length) {
            saki_protocol_increment(&engine->diagnostics.tx_drops);
            return;
        }
        length += appended;
    }
    appended = snprintf(
        response + length,
        sizeof(response) - (size_t)length,
        "}\n"
    );
    if (appended < 0 || (size_t)appended >= sizeof(response) - (size_t)length) {
        saki_protocol_increment(&engine->diagnostics.tx_drops);
        return;
    }
    length += appended;
    (void)saki_protocol_send(engine, response, length);
}

static void saki_protocol_handle_frame(
    const char *frame,
    size_t length,
    void *context
)
{
    saki_protocol_engine_t *engine = context;
    cJSON *root;
    const cJSON *version;
    const cJSON *type;
    uint32_t request_id = 0;
    bool has_request_id;

    if (!saki_utf8_validate((const uint8_t *)frame, length) || !saki_json_frame_bounded(frame, length)) {
        saki_protocol_send_error(
            engine,
            false,
            0,
            "invalid_json",
            "frame is not valid UTF-8 JSON",
            true
        );
        return;
    }
    const char *end = NULL;
    root = cJSON_ParseWithLengthOpts(frame, length, &end, false);
    while (end != NULL && end < frame + length && (*end == ' ' || *end == '\t' || *end == '\r')) ++end;

    if (root == NULL || !cJSON_IsObject(root) || end != frame + length) {
        cJSON_Delete(root);
        saki_protocol_send_error(
            engine,
            false,
            0,
            "invalid_json",
            "frame is not a JSON object",
            true
        );
        return;
    }

    has_request_id = saki_json_uint32(root, "id", &request_id);
    version = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (!cJSON_IsNumber(version) || !isfinite(version->valuedouble) ||
        version->valuedouble < 0 || version->valuedouble > UINT32_MAX ||
        floor(version->valuedouble) != version->valuedouble) {
        saki_protocol_send_error(
            engine,
            has_request_id,
            request_id,
            "missing_field",
            "protocol version is required",
            true
        );
        cJSON_Delete(root);
        return;
    }
    if ((uint32_t)version->valuedouble != SAKI_PROTOCOL_VERSION) {
        saki_protocol_send_error(
            engine,
            has_request_id,
            request_id,
            "unsupported_version",
            "only protocol version 1 is supported",
            true
        );
        cJSON_Delete(root);
        return;
    }

    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL || !has_request_id) {
        saki_protocol_send_error(
            engine,
            has_request_id,
            request_id,
            "missing_field",
            "message type and id are required",
            true
        );
    } else if (strcmp(type->valuestring, "hello") == 0) {
        saki_protocol_handle_hello(engine, root, request_id);
    } else if (strcmp(type->valuestring, "sessions") == 0) {
        saki_protocol_handle_sessions(engine, root, request_id, frame, length);
    } else if (strcmp(type->valuestring, "status") == 0) {
        saki_protocol_handle_status(engine, root, request_id);
    } else if (strcmp(type->valuestring, "clear") == 0) {
        saki_protocol_handle_clear(engine, root, request_id);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        saki_protocol_handle_ping(engine, root, request_id);
    } else {
        saki_protocol_send_error(
            engine,
            true,
            request_id,
            "invalid_field",
            "unsupported message type",
            true
        );
    }
    cJSON_Delete(root);
}

static void saki_protocol_handle_framer_error(
    saki_framer_error_t error,
    void *context
)
{
    saki_protocol_engine_t *engine = context;
    if (error == SAKI_FRAMER_FRAME_TOO_LARGE) {
        saki_protocol_increment(&engine->diagnostics.oversized_frames);
        saki_protocol_send_error(
            engine,
            false,
            0,
            "frame_too_large",
            "frame exceeds 2048 bytes",
            true
        );
    }
}

void saki_protocol_engine_init(
    saki_protocol_engine_t *engine,
    const char *device_id,
    const char *firmware_version,
    saki_protocol_tx_fn transmit,
    saki_protocol_state_fn apply_state,
    void *callback_context
)
{
    if (engine == NULL) {
        return;
    }
    memset(engine, 0, sizeof(*engine));
    saki_ndjson_framer_init(&engine->framer);
    engine->next_id = 1;
    engine->transmit = transmit;
    engine->apply_state = apply_state;
    engine->transport = SAKI_TRANSPORT_USB;
    engine->callback_context = callback_context;
    snprintf(engine->device_id, sizeof(engine->device_id), "%s", device_id);
    snprintf(
        engine->firmware_version,
        sizeof(engine->firmware_version),
        "%s",
        firmware_version
    );
}

void saki_protocol_engine_init_peer(
    saki_protocol_engine_t *engine,
    const char *device_id,
    const char *firmware_version,
    saki_transport_id_t transport,
    uint32_t capability_flags,
    saki_protocol_tx_fn transmit,
    saki_protocol_candidate_fn submit_candidate,
    void *callback_context
)
{
    if (engine == NULL) {
        return;
    }
    memset(engine, 0, sizeof(*engine));
    saki_ndjson_framer_init(&engine->framer);
    engine->next_id = 1;
    engine->transport = transport;
    engine->capability_flags = capability_flags;
    engine->transmit = transmit;
    engine->submit_candidate = submit_candidate;
    engine->callback_context = callback_context;
    snprintf(engine->device_id, sizeof(engine->device_id), "%s", device_id);
    snprintf(
        engine->firmware_version,
        sizeof(engine->firmware_version),
        "%s",
        firmware_version
    );
}

void saki_protocol_engine_disconnect(saki_protocol_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    saki_ndjson_framer_init(&engine->framer);
    saki_protocol_reset_session(engine);
    engine->invalid_streak = 0;
}

void saki_protocol_engine_set_runtime_provider(
    saki_protocol_engine_t *engine,
    saki_protocol_runtime_fn collect_runtime,
    void *runtime_context
)
{
    if (engine == NULL) {
        return;
    }
    engine->collect_runtime = collect_runtime;
    engine->runtime_context = runtime_context;
}

void saki_protocol_engine_receive(
    saki_protocol_engine_t *engine,
    const uint8_t *data,
    size_t length
)
{
    if (engine == NULL || data == NULL || engine->transmit == NULL ||
        (engine->apply_state == NULL && engine->submit_candidate == NULL)) {
        return;
    }
    saki_ndjson_framer_feed(
        &engine->framer,
        data,
        length,
        saki_protocol_handle_frame,
        saki_protocol_handle_framer_error,
        engine
    );
}

bool saki_protocol_engine_check_timeout(
    saki_protocol_engine_t *engine,
    uint64_t now_ms
)
{
    if (engine == NULL || !engine->handshaken || !engine->has_last_activity ||
        now_ms < engine->last_activity_ms ||
        now_ms - engine->last_activity_ms < SAKI_PROTOCOL_HEARTBEAT_TIMEOUT_MS) {
        return false;
    }

    saki_protocol_increment(&engine->diagnostics.heartbeat_timeouts);
    saki_ndjson_framer_init(&engine->framer);
    saki_protocol_reset_session(engine);
    engine->invalid_streak = 0;
    return true;
}

void saki_protocol_engine_note_ui_overwrite(saki_protocol_engine_t *engine)
{
    if (engine != NULL) {
        saki_protocol_increment(&engine->diagnostics.ui_queue_overwrites);
    }
}

void saki_protocol_engine_get_diagnostics(
    const saki_protocol_engine_t *engine,
    saki_protocol_diagnostics_t *diagnostics
)
{
    if (engine == NULL || diagnostics == NULL) {
        return;
    }
    *diagnostics = engine->diagnostics;
}
