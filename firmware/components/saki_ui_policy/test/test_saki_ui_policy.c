#include <stdint.h>
#include <string.h>

#include "saki_ui_policy.h"
#include "unity.h"

static const saki_ui_policy_config_t test_config = {
    .active_percent = 80,
    .idle_percent = 35,
    .disconnected_percent = 20,
    .idle_timeout_ms = 1000,
    .disconnected_timeout_ms = 200,
    .detail_timeout_ms = 150,
};

static void initialize_snapshot(saki_state_snapshot_t *snapshot)
{
    saki_state_snapshot_init(snapshot);
    snapshot->connected = true;
    snapshot->state = SAKI_AGENT_WORKING;
    snprintf(snapshot->detail, sizeof(snapshot->detail), "%s", "command detail");
}

TEST_CASE("idle and disconnected states dim after their own deadlines", "[saki][ui-policy]")
{
    saki_ui_policy_t policy;
    saki_state_snapshot_t snapshot;

    initialize_snapshot(&snapshot);
    saki_ui_policy_init(&policy, &test_config, 100);
    snapshot.state = SAKI_AGENT_IDLE;
    TEST_ASSERT_EQUAL_UINT32(SAKI_UI_POLICY_NO_CHANGE, saki_ui_policy_tick(&policy, &snapshot, 1099));
    TEST_ASSERT_BITS_HIGH(SAKI_UI_POLICY_BACKLIGHT_CHANGED, saki_ui_policy_tick(&policy, &snapshot, 1100));
    TEST_ASSERT_EQUAL_UINT8(35, policy.backlight_percent);

    snapshot.connected = false;
    TEST_ASSERT_BITS_HIGH(
        SAKI_UI_POLICY_BACKLIGHT_CHANGED,
        saki_ui_policy_on_snapshot(&policy, &snapshot, 2000)
    );
    TEST_ASSERT_EQUAL_UINT8(80, policy.backlight_percent);
    TEST_ASSERT_EQUAL_UINT32(SAKI_UI_POLICY_NO_CHANGE, saki_ui_policy_tick(&policy, &snapshot, 2199));
    TEST_ASSERT_BITS_HIGH(SAKI_UI_POLICY_BACKLIGHT_CHANGED, saki_ui_policy_tick(&policy, &snapshot, 2200));
    TEST_ASSERT_EQUAL_UINT8(20, policy.backlight_percent);
}

TEST_CASE("first tap on a dim screen only wakes the backlight", "[saki][ui-policy]")
{
    saki_ui_policy_t policy;
    saki_state_snapshot_t snapshot;

    initialize_snapshot(&snapshot);
    snapshot.connected = false;
    saki_ui_policy_init(&policy, &test_config, 0);
    (void)saki_ui_policy_tick(&policy, &snapshot, 200);

    TEST_ASSERT_EQUAL_UINT32(
        SAKI_UI_POLICY_BACKLIGHT_CHANGED,
        saki_ui_policy_on_tap(&policy, &snapshot, true, 250)
    );
    TEST_ASSERT_EQUAL_UINT8(80, policy.backlight_percent);
    TEST_ASSERT_FALSE(policy.detail_visible);
}

TEST_CASE("bright screen taps toggle detail and detail times out", "[saki][ui-policy]")
{
    saki_ui_policy_t policy;
    saki_state_snapshot_t snapshot;

    initialize_snapshot(&snapshot);
    saki_ui_policy_init(&policy, &test_config, 0);

    TEST_ASSERT_EQUAL_UINT32(
        SAKI_UI_POLICY_VIEW_CHANGED,
        saki_ui_policy_on_tap(&policy, &snapshot, true, 10)
    );
    TEST_ASSERT_TRUE(policy.detail_visible);
    TEST_ASSERT_EQUAL_UINT32(SAKI_UI_POLICY_NO_CHANGE, saki_ui_policy_tick(&policy, &snapshot, 159));
    TEST_ASSERT_BITS_HIGH(
        SAKI_UI_POLICY_VIEW_CHANGED,
        saki_ui_policy_tick(&policy, &snapshot, 160)
    );
    TEST_ASSERT_FALSE(policy.detail_visible);
}

TEST_CASE("detail requires content and the task activity region", "[saki][ui-policy]")
{
    saki_ui_policy_t policy;
    saki_state_snapshot_t snapshot;

    initialize_snapshot(&snapshot);
    saki_ui_policy_init(&policy, &test_config, 0);
    TEST_ASSERT_EQUAL_UINT32(
        SAKI_UI_POLICY_NO_CHANGE,
        saki_ui_policy_on_tap(&policy, &snapshot, false, 10)
    );

    snapshot.detail[0] = '\0';
    TEST_ASSERT_EQUAL_UINT32(
        SAKI_UI_POLICY_NO_CHANGE,
        saki_ui_policy_on_tap(&policy, &snapshot, true, 20)
    );
    TEST_ASSERT_FALSE(policy.detail_visible);
}

TEST_CASE("new snapshot wakes the display and closes missing detail", "[saki][ui-policy]")
{
    saki_ui_policy_config_t config = test_config;
    saki_ui_policy_t policy;
    saki_state_snapshot_t snapshot;
    uint32_t changes;

    initialize_snapshot(&snapshot);
    config.detail_timeout_ms = 1000;
    saki_ui_policy_init(&policy, &config, 0);
    (void)saki_ui_policy_on_tap(&policy, &snapshot, true, 10);
    snapshot.connected = false;
    (void)saki_ui_policy_on_snapshot(&policy, &snapshot, 20);
    (void)saki_ui_policy_tick(&policy, &snapshot, 220);
    snapshot.detail[0] = '\0';

    changes = saki_ui_policy_on_snapshot(&policy, &snapshot, 300);
    TEST_ASSERT_BITS_HIGH(SAKI_UI_POLICY_BACKLIGHT_CHANGED, changes);
    TEST_ASSERT_BITS_HIGH(SAKI_UI_POLICY_VIEW_CHANGED, changes);
    TEST_ASSERT_EQUAL_UINT8(80, policy.backlight_percent);
    TEST_ASSERT_FALSE(policy.detail_visible);
}

TEST_CASE("brightness percentages map to stable dimming opacity", "[saki][ui-policy]")
{
    TEST_ASSERT_EQUAL_UINT8(0, saki_ui_policy_dimming_opacity(100));
    TEST_ASSERT_EQUAL_UINT8(51, saki_ui_policy_dimming_opacity(80));
    TEST_ASSERT_EQUAL_UINT8(166, saki_ui_policy_dimming_opacity(35));
    TEST_ASSERT_EQUAL_UINT8(204, saki_ui_policy_dimming_opacity(20));
    TEST_ASSERT_EQUAL_UINT8(0, saki_ui_policy_dimming_opacity(255));
}
