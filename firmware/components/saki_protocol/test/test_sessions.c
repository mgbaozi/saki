/* Shared native/Unity tests execute the production parser, model and arbiter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "saki_protocol.h"
#ifndef SAKI_NATIVE_TEST
#include "unity.h"
#endif

#define SESSION "00000000-0000-4000-8000-000000000001"
#define HELLO "{\"v\":1,\"type\":\"hello\",\"id\":1,\"role\":\"host\",\"session\":\"" SESSION "\",\"mode\":\"multi-session\"}\n"
#define EMPTY "{\"v\":1,\"type\":\"sessions\",\"id\":2,\"session\":\"" SESSION "\",\"seq\":1,\"sessions\":[],\"total\":0,\"hidden_attention\":0,\"capacity_rejected\":0}\n"
#define GENERIC "{\"v\":1,\"type\":\"sessions\",\"id\":2,\"session\":\"" SESSION "\",\"seq\":1,\"sessions\":[{\"source\":\"demo_agent\",\"run_id\":\"local-1\",\"revision\":1,\"fresh\":true,\"state\":\"working\",\"agent\":{\"name\":\"Demo Agent\"},\"task\":{\"id\":\"0123456789abcdef0123456789abcdef\",\"title\":\"Generic source\"},\"activity\":{\"kind\":\"shell\",\"summary\":\"Building\"}}],\"total\":1,\"hidden_attention\":0,\"capacity_rejected\":0}\n"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static saki_protocol_engine_t usb, ble;
static saki_transport_manager_t manager;
static saki_display_snapshot_t current;
static char response[2048];
static unsigned commits;
static bool fail_apply;
#ifdef SAKI_NATIVE_TEST
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
#endif
static esp_err_t tx(const uint8_t *data, size_t length, void *context)
{
    (void)context;
    if (length >= sizeof(response)) return ESP_FAIL;
    memcpy(response, data, length); response[length] = 0; return ESP_OK;
}
static esp_err_t apply(const saki_display_snapshot_t *value, void *context)
{
    (void)context;
    if (fail_apply) return ESP_FAIL;
    current = *value; ++commits; return ESP_OK;
}
static esp_err_t legacy_apply(const saki_state_snapshot_t *value, void *context)
{
    (void)value; (void)context; ++commits; return ESP_OK;
}
static saki_transport_outcome_t submit(const saki_display_snapshot_t *value,
    saki_transport_id_t transport, const char *session, uint32_t seq, uint64_t now, void *context)
{
    (void)context;
    return saki_transport_manager_submit_display(&manager, transport, session, seq, value, now);
}
static saki_transport_outcome_t legacy(const saki_state_snapshot_t *value,
    saki_transport_id_t transport, const char *session, uint32_t seq, uint64_t now, void *context)
{
    (void)context;
    return saki_transport_manager_submit(&manager, transport, session, seq, value, now);
}
static void feed(saki_protocol_engine_t *peer, const char *frame)
{
    response[0] = 0;
    saki_protocol_engine_receive(peer, (const uint8_t *)frame, strlen(frame));
}
static void reset(void)
{
    commits = 0; fail_apply = false; memset(&current, 0, sizeof(current));
    saki_transport_manager_init(&manager, legacy_apply, NULL); manager.apply_display = apply;
    saki_protocol_engine_init_peer(&usb, "0123456789ab", "0.4.0-dev", SAKI_TRANSPORT_USB, 0, tx, legacy, NULL);
    saki_protocol_engine_init_peer(&ble, "0123456789ab", "0.4.0-dev", SAKI_TRANSPORT_BLE, 0, tx, legacy, NULL);
    usb.submit_display = submit; ble.submit_display = submit;
    feed(&usb, HELLO); feed(&ble, HELLO);
}
static int test_commit(void)
{
    reset();
    CHECK(strstr(response, "\"mode\":\"multi-session\"") != NULL);
    /* A partial frame never reaches the UI. */
    saki_protocol_engine_receive(&usb, (const uint8_t *)EMPTY, 15);
    CHECK(commits == 0);
    saki_protocol_engine_receive(&usb, (const uint8_t *)EMPTY + 15, strlen(EMPTY) - 15);
    CHECK(commits == 1 && current.count == 0 && manager.last_seq == 1);
    CHECK(strstr(response, "\"applied\":true") != NULL);
    feed(&usb, EMPTY); /* Exact retry recovers a lost ACK without reapplying. */
    CHECK(commits == 1 && strstr(response, "\"committed\":true") != NULL);
    char altered[sizeof(EMPTY) + 20];
    snprintf(altered, sizeof(altered), " %s", EMPTY);
    feed(&usb, altered);
    CHECK(commits == 1 && strstr(response, "\"committed\":false") != NULL);
    feed(&ble, EMPTY);
    CHECK(commits == 1 && strstr(response, "busy") != NULL);
    /* A new legacy hello on either peer cannot bypass a committed multi session. */
    feed(&usb, "{\"v\":1,\"type\":\"hello\",\"id\":3,\"role\":\"host\",\"session\":\"" SESSION "\"}\n");
    feed(&usb, "{\"v\":1,\"type\":\"clear\",\"id\":4,\"seq\":2,\"session\":\"" SESSION "\"}\n");
    CHECK(commits == 1 && manager.last_seq == 1);
    reset(); fail_apply = true; feed(&usb, EMPTY);
    CHECK(commits == 0 && !manager.has_last_seq);
    fail_apply = false; feed(&usb, EMPTY);
    CHECK(commits == 1);
    saki_transport_manager_link_down(&manager, SAKI_TRANSPORT_USB, 0);
    feed(&ble, EMPTY); CHECK(commits == 1);
    saki_transport_manager_tick(&manager, SAKI_TRANSPORT_USB_GRACE_MS);
    char next[sizeof(EMPTY)]; strcpy(next, EMPTY);
    char *seq = strstr(next, "\"seq\":1"); CHECK(seq != NULL); seq[6] = '2';
    feed(&ble, next);
    CHECK(commits == 2 && manager.active == SAKI_TRANSPORT_BLE);
    feed(&usb, EMPTY); CHECK(commits == 2);
    return 0;
}
static int test_invalid(void)
{
    reset(); feed(&usb, EMPTY);
    feed(&usb, "{\"v\":1,\"type\":\"sessions\",\"id\":3,\"session\":\"" SESSION "\",\"seq\":2,\"sessions\":[{}],\"total\":1,\"hidden_attention\":0,\"capacity_rejected\":0}\n");
    CHECK(commits == 1 && manager.last_seq == 1);
    feed(&usb, "{\"v\":1,\"type\":\"clear\\u0000ignored\",\"id\":4}\n");
    CHECK(strstr(response, "invalid_json") != NULL && commits == 1);
    feed(&usb, "{\"v\":1,\"type\":\"ping\",\"id\":5}garbage\n");
    CHECK(strstr(response, "invalid_json") != NULL);
    char deep[160]; memset(deep, '[', 40); memset(deep + 40, ']', 40); deep[80] = '\n'; deep[81] = 0;
    feed(&usb, deep); CHECK(strstr(response, "invalid_json") != NULL);
    char large[SAKI_PROTOCOL_MAX_FRAME + 3]; memset(large, ' ', sizeof(large));
    large[sizeof(large)-2] = '\n'; large[sizeof(large)-1] = 0;
    feed(&usb, large); CHECK(strstr(response, "frame_too_large") != NULL && commits == 1);
    return 0;
}
static int test_generic_source(void)
{
    reset();
    usb.capability_flags |= SAKI_PROTOCOL_CAPABILITY_GENERIC_SOURCE;
    feed(&usb, HELLO);
    CHECK(strstr(response, "\"generic-source\"") != NULL);
    feed(&usb, GENERIC);
    CHECK(commits == 1 && current.count == 1);
    CHECK(strcmp(current.items[0].agent_name, "Demo Agent") == 0);
    CHECK(strcmp(current.items[0].activity_kind, "shell") == 0);
    feed(&usb, "{\"v\":1,\"type\":\"sessions\",\"id\":3,\"session\":\"" SESSION "\",\"seq\":2,\"sessions\":[{\"source\":\"Bad-Agent\",\"run_id\":\"local-1\",\"revision\":2,\"fresh\":true,\"state\":\"working\",\"agent\":{\"name\":\"Bad\"},\"task\":{\"id\":\"0123456789abcdef0123456789abcdef\",\"title\":\"Bad\"}}],\"total\":1,\"hidden_attention\":0,\"capacity_rejected\":0}\n");
    CHECK(commits == 1 && manager.last_seq == 1);
    feed(&usb, "{\"v\":1,\"type\":\"sessions\",\"id\":4,\"session\":\"" SESSION "\",\"seq\":2,\"sessions\":[{\"source\":\"demo_agent\",\"run_id\":\"local-1\",\"revision\":2,\"fresh\":true,\"state\":\"working\",\"agent\":{\"name\":\"Demo Agent\"},\"task\":{\"id\":\"0123456789abcdef0123456789abcdef\",\"title\":\"Bad activity\"},\"activity\":{\"kind\":\"unknown\"}}],\"total\":1,\"hidden_attention\":0,\"capacity_rejected\":0}\n");
    CHECK(commits == 1 && manager.last_seq == 1);
    return 0;
}
#ifdef SAKI_NATIVE_TEST
int main(int argc, char **argv)
{
    int result = test_commit(); if (!result) result = test_invalid();
    if (!result) result = test_generic_source();
    if (result) { fprintf(stderr, "contract failed at line %d\n", result); return 1; }
    if (argc >= 2) {
        char frame[4096]; FILE *file = fopen(argv[1], "rb"); if (!file) return 2;
        size_t length = fread(frame, 1, sizeof(frame)-2, file); fclose(file);
        frame[length++] = '\n'; frame[length] = 0;
        reset();
        if (argc == 3 && strcmp(argv[2], "reject") == 0) {
            feed(&usb, EMPTY); feed(&usb, frame);
            return commits == 1 && manager.last_seq == 1 ? 0 : 5;
        }
        if (argc == 3 && strcmp(argv[2], "generic") == 0) {
            usb.capability_flags |= SAKI_PROTOCOL_CAPABILITY_GENERIC_SOURCE;
            feed(&usb, HELLO); feed(&usb, frame);
            if (commits == 1 && current.count == 1 &&
                strcmp(current.items[0].agent_name, "演示 Agent") == 0) return 0;
            fprintf(stderr, "generic commits=%u count=%u label=%s response=%s\n",
                commits, current.count, current.items[0].agent_name, response);
            return 6;
        }
        if (argc == 3 && strcmp(argv[2], "generic-known") == 0) {
            usb.capability_flags |= SAKI_PROTOCOL_CAPABILITY_GENERIC_SOURCE;
            feed(&usb, HELLO); feed(&usb, frame);
            return commits == 1 && current.count == 4 &&
                strcmp(current.items[0].agent_name, "Codex") == 0 ? 0 : 7;
        }
        feed(&usb, frame);
        if (commits != 1 || current.count != 4 || strcmp(current.items[0].agent_name, "Codex") != 0) return 3;
        if (saki_state_elapsed_is_running(&current.items[0]) == current.items[0].stale) return 4;
        /* Parser preserves Host order even when a sidebar item needs approval. */
        if (current.items[0].state != SAKI_AGENT_STARTING || current.items[1].state != SAKI_AGENT_WAITING_APPROVAL) return 5;
    }
    puts("native multi-session contracts passed"); return 0;
}
#else
TEST_CASE("multi-session commits are atomic and isolated across peers", "[saki_protocol]")
{ TEST_ASSERT_EQUAL_INT(0, test_commit()); }
TEST_CASE("multi-session rejects invalid bounded input without applying", "[saki_protocol]")
{ TEST_ASSERT_EQUAL_INT(0, test_invalid()); }
TEST_CASE("generic source requires capability and bounded metadata", "[saki_protocol]")
{ TEST_ASSERT_EQUAL_INT(0, test_generic_source()); }
#endif
