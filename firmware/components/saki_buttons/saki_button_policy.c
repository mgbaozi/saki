#include "saki_button_policy.h"

#include <limits.h>
#include <string.h>

static uint64_t saki_elapsed(uint64_t now_ms, uint64_t started_ms)
{
    return now_ms >= started_ms ? now_ms - started_ms : 0;
}

static uint32_t saki_elapsed_u32(uint64_t now_ms, uint64_t started_ms)
{
    uint64_t elapsed = saki_elapsed(now_ms, started_ms);
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

void saki_button_policy_init(
    saki_button_policy_t *policy,
    bool pressed,
    uint64_t now_ms
)
{
    if (policy == NULL) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->initialized = true;
    policy->raw_pressed = pressed;
    policy->stable_pressed = pressed;
    policy->raw_changed_ms = now_ms;
    policy->pressed_ms = pressed ? now_ms : 0;
}

saki_button_event_t saki_button_policy_update(
    saki_button_policy_t *policy,
    bool pressed,
    uint64_t now_ms
)
{
    uint64_t held_ms;

    if (policy == NULL) {
        return SAKI_BUTTON_EVENT_NONE;
    }
    if (!policy->initialized) {
        saki_button_policy_init(policy, pressed, now_ms);
        return SAKI_BUTTON_EVENT_NONE;
    }

    if (pressed != policy->raw_pressed) {
        policy->raw_pressed = pressed;
        policy->raw_changed_ms = now_ms;
    } else if (now_ms < policy->raw_changed_ms) {
        policy->raw_changed_ms = now_ms;
    }

    if (policy->raw_pressed != policy->stable_pressed &&
        saki_elapsed(now_ms, policy->raw_changed_ms) >= SAKI_BUTTON_DEBOUNCE_MS) {
        policy->stable_pressed = policy->raw_pressed;
        if (policy->stable_pressed) {
            policy->pressed_ms = policy->raw_changed_ms;
            policy->clear_emitted = false;
        } else {
            held_ms = saki_elapsed(policy->raw_changed_ms, policy->pressed_ms);
            if (policy->clear_emitted) {
                return SAKI_BUTTON_EVENT_NONE;
            }
            if (held_ms >= SAKI_BUTTON_CLEAR_HOLD_MS) {
                policy->clear_emitted = true;
                return SAKI_BUTTON_EVENT_CLEAR_THRESHOLD;
            }
            if (held_ms >= SAKI_BUTTON_PAIR_HOLD_MS) {
                return SAKI_BUTTON_EVENT_PAIR_RELEASE;
            }
            return SAKI_BUTTON_EVENT_SHORT_RELEASE;
        }
    }

    if (policy->stable_pressed && policy->raw_pressed && !policy->clear_emitted &&
        saki_elapsed(now_ms, policy->pressed_ms) >= SAKI_BUTTON_CLEAR_HOLD_MS) {
        policy->clear_emitted = true;
        return SAKI_BUTTON_EVENT_CLEAR_THRESHOLD;
    }
    return SAKI_BUTTON_EVENT_NONE;
}

uint32_t saki_button_policy_held_ms(
    const saki_button_policy_t *policy,
    uint64_t now_ms
)
{
    uint64_t effective_now;

    if (policy == NULL || !policy->initialized || !policy->stable_pressed) {
        return 0;
    }
    effective_now = policy->raw_pressed ? now_ms : policy->raw_changed_ms;
    return saki_elapsed_u32(effective_now, policy->pressed_ms);
}
