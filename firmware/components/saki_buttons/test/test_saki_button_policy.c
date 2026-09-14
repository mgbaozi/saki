#include "unity.h"

#include "saki_button_policy.h"

static void settle_press(saki_button_policy_t *policy, uint64_t pressed_at_ms)
{
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(policy, true, pressed_at_ms)
    );
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(
            policy,
            true,
            pressed_at_ms + SAKI_BUTTON_DEBOUNCE_MS
        )
    );
}

static saki_button_event_t release_at(
    saki_button_policy_t *policy,
    uint64_t released_at_ms
)
{
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(policy, false, released_at_ms)
    );
    return saki_button_policy_update(
        policy,
        false,
        released_at_ms + SAKI_BUTTON_DEBOUNCE_MS
    );
}

TEST_CASE("K2 release boundaries classify short and pairing actions", "[saki][buttons]")
{
    saki_button_policy_t policy;

    saki_button_policy_init(&policy, false, 0);
    settle_press(&policy, 100);
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_SHORT_RELEASE,
        release_at(&policy, 100 + SAKI_BUTTON_PAIR_HOLD_MS - 1)
    );

    settle_press(&policy, 3000);
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_PAIR_RELEASE,
        release_at(&policy, 3000 + SAKI_BUTTON_PAIR_HOLD_MS)
    );

    settle_press(&policy, 9000);
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_PAIR_RELEASE,
        release_at(&policy, 9000 + SAKI_BUTTON_CLEAR_HOLD_MS - 1)
    );
}

TEST_CASE("K2 five second hold emits clear once and never pairing", "[saki][buttons]")
{
    saki_button_policy_t policy;
    uint64_t pressed_at_ms = 100;

    saki_button_policy_init(&policy, false, 0);
    settle_press(&policy, pressed_at_ms);
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(
            &policy,
            true,
            pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS - 1
        )
    );
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_CLEAR_THRESHOLD,
        saki_button_policy_update(
            &policy,
            true,
            pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS
        )
    );
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(
            &policy,
            true,
            pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS + 1000
        )
    );
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        release_at(&policy, pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS + 1100)
    );
}

TEST_CASE("K2 debounce ignores bouncing edges", "[saki][buttons]")
{
    saki_button_policy_t policy;

    saki_button_policy_init(&policy, false, 0);
    TEST_ASSERT_EQUAL(SAKI_BUTTON_EVENT_NONE, saki_button_policy_update(&policy, true, 10));
    TEST_ASSERT_EQUAL(SAKI_BUTTON_EVENT_NONE, saki_button_policy_update(&policy, false, 20));
    TEST_ASSERT_EQUAL(SAKI_BUTTON_EVENT_NONE, saki_button_policy_update(&policy, true, 25));
    TEST_ASSERT_EQUAL(SAKI_BUTTON_EVENT_NONE, saki_button_policy_update(&policy, true, 54));
    TEST_ASSERT_FALSE(policy.stable_pressed);
    TEST_ASSERT_EQUAL(SAKI_BUTTON_EVENT_NONE, saki_button_policy_update(&policy, true, 55));
    TEST_ASSERT_TRUE(policy.stable_pressed);
    TEST_ASSERT_EQUAL_UINT32(1000, saki_button_policy_held_ms(&policy, 1025));
}

TEST_CASE("K2 release just before five seconds does not clear during debounce", "[saki][buttons]")
{
    saki_button_policy_t policy;
    uint64_t pressed_at_ms = 100;

    saki_button_policy_init(&policy, false, 0);
    settle_press(&policy, pressed_at_ms);
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_NONE,
        saki_button_policy_update(
            &policy,
            false,
            pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS - 1
        )
    );
    TEST_ASSERT_EQUAL(
        SAKI_BUTTON_EVENT_PAIR_RELEASE,
        saki_button_policy_update(
            &policy,
            false,
            pressed_at_ms + SAKI_BUTTON_CLEAR_HOLD_MS - 1 + SAKI_BUTTON_DEBOUNCE_MS
        )
    );
}
