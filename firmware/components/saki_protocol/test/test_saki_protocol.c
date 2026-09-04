#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "saki_protocol.h"
#include "unity.h"

#define TEST_SESSION "00000000-0000-4000-8000-000000000001"

typedef struct {
    char transmitted[2048];
    size_t transmitted_length;
    size_t apply_count;
    saki_state_snapshot_t latest_snapshot;
    bool fail_transmit;
    bool fail_apply;
} protocol_context_t;

typedef struct {
    size_t frame_count;
    size_t error_count;
    char latest_frame[32];
} framer_context_t;

static esp_err_t capture_transmit(
    const uint8_t *data,
    size_t length,
    void *context
)
{
    protocol_context_t *capture = context;

    if (capture->fail_transmit) {
        return ESP_FAIL;
    }
    if (capture->transmitted_length + length >= sizeof(capture->transmitted)) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(capture->transmitted + capture->transmitted_length, data, length);
    capture->transmitted_length += length;
    capture->transmitted[capture->transmitted_length] = '\0';
    return ESP_OK;
}

static esp_err_t capture_state(
    const saki_state_snapshot_t *snapshot,
    void *context
)
{
    protocol_context_t *capture = context;

    if (capture->fail_apply) {
        return ESP_FAIL;
    }
    ++capture->apply_count;
    saki_state_snapshot_copy(&capture->latest_snapshot, snapshot);
    return ESP_OK;
}

static void capture_runtime_metrics(
    saki_runtime_metrics_t *metrics,
    void *context
)
{
    *metrics = *(const saki_runtime_metrics_t *)context;
}

static void capture_frame(const char *frame, size_t length, void *context)
{
    framer_context_t *capture = context;
    size_t copy_length = length;

    if (copy_length >= sizeof(capture->latest_frame)) {
        copy_length = sizeof(capture->latest_frame) - 1;
    }
    ++capture->frame_count;
    memcpy(capture->latest_frame, frame, copy_length);
    capture->latest_frame[copy_length] = '\0';
}

static void capture_framer_error(saki_framer_error_t error, void *context)
{
    framer_context_t *capture = context;

    TEST_ASSERT_EQUAL(SAKI_FRAMER_FRAME_TOO_LARGE, error);
    ++capture->error_count;
}

static void initialize_engine(
    saki_protocol_engine_t *engine,
    protocol_context_t *context
)
{
    memset(context, 0, sizeof(*context));
    saki_protocol_engine_init(
        engine,
        "0123456789ab",
        "test",
        capture_transmit,
        capture_state,
        context
    );
}

static void feed_text(saki_protocol_engine_t *engine, const char *text)
{
    saki_protocol_engine_receive(engine, (const uint8_t *)text, strlen(text));
}

static void feed_hello(saki_protocol_engine_t *engine, uint32_t id)
{
    char message[192];

    snprintf(
        message,
        sizeof(message),
        "{\"v\":1,\"type\":\"hello\",\"id\":%lu,\"role\":\"host\","
        "\"session\":\"%s\"}\n",
        (unsigned long)id,
        TEST_SESSION
    );
    feed_text(engine, message);
}

static void assert_fresh_engine_error(const char *message, const char *error_code)
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    char expected[64];

    initialize_engine(&engine, &capture);
    feed_text(&engine, message);
    snprintf(expected, sizeof(expected), "\"code\":\"%s\"", error_code);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture.transmitted, expected), error_code);
}

TEST_CASE("NDJSON framer handles chunks CRLF and multiple frames", "[saki][protocol]")
{
    static saki_ndjson_framer_t framer;
    static framer_context_t capture;

    memset(&capture, 0, sizeof(capture));
    saki_ndjson_framer_init(&framer);
    saki_ndjson_framer_feed(
        &framer,
        (const uint8_t *)"one\r",
        4,
        capture_frame,
        capture_framer_error,
        &capture
    );
    saki_ndjson_framer_feed(
        &framer,
        (const uint8_t *)"\ntwo\nthree",
        10,
        capture_frame,
        capture_framer_error,
        &capture
    );
    saki_ndjson_framer_feed(
        &framer,
        (const uint8_t *)"\n",
        1,
        capture_frame,
        capture_framer_error,
        &capture
    );

    TEST_ASSERT_EQUAL_UINT32(3, capture.frame_count);
    TEST_ASSERT_EQUAL_STRING("three", capture.latest_frame);
    TEST_ASSERT_EQUAL_UINT32(0, capture.error_count);
}

TEST_CASE("NDJSON framer recovers after oversized input", "[saki][protocol]")
{
    static saki_ndjson_framer_t framer;
    static framer_context_t capture;
    static uint8_t oversized[SAKI_PROTOCOL_MAX_FRAME + 2];

    memset(&capture, 0, sizeof(capture));
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\n';
    saki_ndjson_framer_init(&framer);
    saki_ndjson_framer_feed(
        &framer,
        oversized,
        sizeof(oversized),
        capture_frame,
        capture_framer_error,
        &capture
    );
    saki_ndjson_framer_feed(
        &framer,
        (const uint8_t *)"ok\n",
        3,
        capture_frame,
        capture_framer_error,
        &capture
    );

    TEST_ASSERT_EQUAL_UINT32(1, capture.error_count);
    TEST_ASSERT_EQUAL_UINT32(1, capture.frame_count);
    TEST_ASSERT_EQUAL_STRING("ok", capture.latest_frame);
}

TEST_CASE("protocol applies complete status and rejects old sequence", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    saki_protocol_diagnostics_t diagnostics;
    static const char status[] =
        "{\"v\":1,\"type\":\"status\",\"id\":2,\"session\":\"" TEST_SESSION
        "\",\"seq\":1,\"state\":\"working\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"中文任务\"},"
        "\"activity\":{\"summary\":\"正在测试\"},"
        "\"progress\":{\"mode\":\"determinate\",\"percent\":50},"
        "\"elapsed_ms\":1234,\"agent\":{\"name\":\"Codex\",\"model\":\"gpt-5.6\"}}\n";
    static const char duplicate[] =
        "{\"v\":1,\"type\":\"status\",\"id\":3,\"session\":\"" TEST_SESSION
        "\",\"seq\":1,\"state\":\"working\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"duplicate\"}}\n";
    static const char ping[] =
        "{\"v\":1,\"type\":\"ping\",\"id\":4,\"session\":\"" TEST_SESSION
        "\"}\n";

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    feed_text(&engine, status);
    feed_text(&engine, duplicate);
    feed_text(&engine, ping);
    saki_protocol_engine_get_diagnostics(&engine, &diagnostics);

    TEST_ASSERT_TRUE(engine.handshaken);
    TEST_ASSERT_EQUAL_UINT32(1, capture.apply_count);
    TEST_ASSERT_EQUAL(SAKI_AGENT_WORKING, capture.latest_snapshot.state);
    TEST_ASSERT_EQUAL_STRING("中文任务", capture.latest_snapshot.task_title);
    TEST_ASSERT_EQUAL_UINT8(50, capture.latest_snapshot.progress_percent);
    TEST_ASSERT_TRUE(capture.latest_snapshot.received_at_ms > 0);
    TEST_ASSERT_EQUAL_UINT32(4, diagnostics.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.invalid_frames);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.old_sequences);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"applied\":false"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"type\":\"pong\""));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"valid_frames\":4"));
}

TEST_CASE("elapsed clock advances and freezes by state", "[saki][model]")
{
    saki_state_snapshot_t snapshot;

    saki_state_snapshot_init(&snapshot);
    snapshot.connected = true;
    snapshot.state = SAKI_AGENT_WORKING;
    snapshot.elapsed_ms = 1234;
    snapshot.received_at_ms = 1000;

    TEST_ASSERT_TRUE(saki_state_elapsed_is_running(&snapshot));
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 2000) == UINT64_C(2234));
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 999) == UINT64_C(1234));

    snapshot.state = SAKI_AGENT_WAITING_APPROVAL;
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 3000) == UINT64_C(3234));

    snapshot.state = SAKI_AGENT_COMPLETED;
    TEST_ASSERT_FALSE(saki_state_elapsed_is_running(&snapshot));
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 4000) == UINT64_C(1234));

    snapshot.state = SAKI_AGENT_WORKING;
    snapshot.connected = false;
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 5000) == UINT64_C(1234));

    snapshot.connected = true;
    snapshot.elapsed_ms = UINT64_MAX - 4;
    TEST_ASSERT_TRUE(saki_state_elapsed_at(&snapshot, 2000) == UINT64_MAX);
}

TEST_CASE("pong includes optional runtime safety metrics", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    static const saki_runtime_metrics_t runtime = {
        .heap_free_bytes = 8000000,
        .heap_min_bytes = 7900000,
        .internal_free_bytes = 180000,
        .internal_min_bytes = 160000,
        .app_stack_min_bytes = 1400,
        .ui_stack_min_bytes = 3200,
        .usb_stack_min_bytes = 2800,
    };
    static const char ping[] =
        "{\"v\":1,\"type\":\"ping\",\"id\":2,\"session\":\"" TEST_SESSION
        "\"}\n";

    initialize_engine(&engine, &capture);
    saki_protocol_engine_set_runtime_provider(
        &engine,
        capture_runtime_metrics,
        (void *)&runtime
    );
    feed_hello(&engine, 1);
    feed_text(&engine, ping);

    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"runtime\":{"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"heap_min_bytes\":7900000"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"internal_min_bytes\":160000"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"app_stack_min_bytes\":1400"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"ui_stack_min_bytes\":3200"));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"usb_stack_min_bytes\":2800"));
    TEST_ASSERT_EQUAL_UINT32(0, engine.diagnostics.tx_drops);
}

TEST_CASE("UTF-8 validation and truncation preserve code point boundaries", "[saki][model]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    static const char mixed[] = "中文abc";
    static const uint8_t emoji[] = {0xF0, 0x9F, 0x98, 0x80};
    static const uint8_t truncated_three_byte[] = {0xE4, 0xB8};
    static const uint8_t overlong[] = {0xC0, 0xAF, 0x00};
    static const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    static const uint8_t beyond_unicode[] = {0xF4, 0x90, 0x80, 0x80};
    static const uint8_t invalid_frame[] =
        "{\"v\":1,\"type\":\"status\",\"id\":2,\"session\":\"" TEST_SESSION
        "\",\"seq\":1,\"state\":\"working\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"\xC0\xAF\"}}\n";
    char destination[8];
    char chinese_only[4];

    TEST_ASSERT_TRUE(saki_utf8_validate((const uint8_t *)mixed, strlen(mixed)));
    TEST_ASSERT_TRUE(saki_utf8_validate(emoji, sizeof(emoji)));
    TEST_ASSERT_FALSE(saki_utf8_validate(
        truncated_three_byte,
        sizeof(truncated_three_byte)
    ));
    TEST_ASSERT_FALSE(saki_utf8_validate(overlong, sizeof(overlong) - 1));
    TEST_ASSERT_FALSE(saki_utf8_validate(surrogate, sizeof(surrogate)));
    TEST_ASSERT_FALSE(saki_utf8_validate(beyond_unicode, sizeof(beyond_unicode)));

    TEST_ASSERT_EQUAL(
        SAKI_UTF8_COPY_TRUNCATED,
        saki_utf8_copy(destination, sizeof(destination), mixed)
    );
    TEST_ASSERT_EQUAL_STRING("中文a", destination);
    TEST_ASSERT_EQUAL(
        SAKI_UTF8_COPY_TRUNCATED,
        saki_utf8_copy(chinese_only, sizeof(chinese_only), "中文")
    );
    TEST_ASSERT_EQUAL_STRING("中", chinese_only);
    TEST_ASSERT_EQUAL(
        SAKI_UTF8_COPY_INVALID,
        saki_utf8_copy(destination, sizeof(destination), (const char *)overlong)
    );
    TEST_ASSERT_EQUAL_STRING("", destination);

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    saki_protocol_engine_receive(&engine, invalid_frame, sizeof(invalid_frame) - 1);
    TEST_ASSERT_EQUAL_UINT32(0, capture.apply_count);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"invalid_json\""));
}

TEST_CASE("protocol accepts uint32 sequence wrap", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    static const char wrapped[] =
        "{\"v\":1,\"type\":\"clear\",\"id\":2,\"session\":\"" TEST_SESSION
        "\",\"seq\":0}\n";

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    engine.has_last_seq = true;
    engine.last_seq = UINT32_MAX - 1;
    feed_text(&engine, wrapped);

    TEST_ASSERT_EQUAL_UINT32(1, capture.apply_count);
    TEST_ASSERT_EQUAL_UINT32(0, engine.last_seq);
    TEST_ASSERT_EQUAL(SAKI_AGENT_IDLE, capture.latest_snapshot.state);
}

TEST_CASE("five invalid frames clear the active session", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    saki_protocol_diagnostics_t diagnostics;

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    for (size_t index = 0; index < 5; ++index) {
        feed_text(&engine, "{bad}\n");
    }
    saki_protocol_engine_get_diagnostics(&engine, &diagnostics);

    TEST_ASSERT_FALSE(engine.handshaken);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(5, diagnostics.invalid_frames);
    TEST_ASSERT_EQUAL_UINT8(0, engine.invalid_streak);
}

TEST_CASE("protocol returns stable errors for invalid requests", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    static uint8_t oversized[SAKI_PROTOCOL_MAX_FRAME + 2];
    static const char missing_state[] =
        "{\"v\":1,\"type\":\"status\",\"id\":2,\"session\":\"" TEST_SESSION
        "\",\"seq\":1}\n";
    static const char invalid_state[] =
        "{\"v\":1,\"type\":\"status\",\"id\":3,\"session\":\"" TEST_SESSION
        "\",\"seq\":2,\"state\":\"dancing\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"invalid\"}}\n";
    static const char invalid_progress[] =
        "{\"v\":1,\"type\":\"status\",\"id\":4,\"session\":\"" TEST_SESSION
        "\",\"seq\":3,\"state\":\"working\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"invalid\"},"
        "\"progress\":{\"mode\":\"determinate\",\"percent\":101}}\n";

    assert_fresh_engine_error("not-json\n", "invalid_json");
    assert_fresh_engine_error("{\"v\":1,\"id\":1}\n", "missing_field");
    assert_fresh_engine_error(
        "{\"v\":2,\"type\":\"hello\",\"id\":1}\n",
        "unsupported_version"
    );
    assert_fresh_engine_error(
        "{\"v\":1,\"type\":\"dance\",\"id\":1}\n",
        "invalid_field"
    );
    assert_fresh_engine_error(
        "{\"v\":1,\"type\":\"ping\",\"id\":1,\"session\":\"" TEST_SESSION
        "\"}\n",
        "not_handshaken"
    );

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    feed_text(&engine, missing_state);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"missing_field\""));
    feed_text(&engine, invalid_state);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"invalid_state\""));
    feed_text(&engine, invalid_progress);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"invalid_field\""));

    initialize_engine(&engine, &capture);
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\n';
    saki_protocol_engine_receive(&engine, oversized, sizeof(oversized));
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"frame_too_large\""));
}

TEST_CASE("busy display is valid and transmit failures are counted", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    saki_protocol_diagnostics_t diagnostics;
    static const char status[] =
        "{\"v\":1,\"type\":\"status\",\"id\":2,\"session\":\"" TEST_SESSION
        "\",\"seq\":1,\"state\":\"working\","
        "\"task\":{\"id\":\"task-1\",\"title\":\"busy\"}}\n";

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    capture.fail_apply = true;
    feed_text(&engine, status);
    TEST_ASSERT_NOT_NULL(strstr(capture.transmitted, "\"code\":\"busy\""));
    capture.fail_transmit = true;
    feed_text(&engine, "not-json\n");
    saki_protocol_engine_note_ui_overwrite(&engine);
    saki_protocol_engine_get_diagnostics(&engine, &diagnostics);

    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.invalid_frames);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.ui_queue_overwrites);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.tx_drops);
}

TEST_CASE("heartbeat timeout resets session and counters saturate", "[saki][protocol]")
{
    static saki_protocol_engine_t engine;
    static protocol_context_t capture;
    saki_protocol_diagnostics_t diagnostics;
    uint64_t last_activity;

    initialize_engine(&engine, &capture);
    feed_hello(&engine, 1);
    last_activity = engine.last_activity_ms;
    TEST_ASSERT_FALSE(saki_protocol_engine_check_timeout(
        &engine,
        last_activity + SAKI_PROTOCOL_HEARTBEAT_TIMEOUT_MS - 1
    ));
    TEST_ASSERT_TRUE(saki_protocol_engine_check_timeout(
        &engine,
        last_activity + SAKI_PROTOCOL_HEARTBEAT_TIMEOUT_MS
    ));
    TEST_ASSERT_FALSE(engine.handshaken);

    feed_hello(&engine, 2);
    engine.diagnostics.heartbeat_timeouts = UINT32_MAX;
    last_activity = engine.last_activity_ms;
    TEST_ASSERT_TRUE(saki_protocol_engine_check_timeout(
        &engine,
        last_activity + SAKI_PROTOCOL_HEARTBEAT_TIMEOUT_MS
    ));
    saki_protocol_engine_get_diagnostics(&engine, &diagnostics);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, diagnostics.heartbeat_timeouts);
}
