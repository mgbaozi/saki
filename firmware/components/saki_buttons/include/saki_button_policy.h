#ifndef SAKI_BUTTON_POLICY_H
#define SAKI_BUTTON_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAKI_BUTTON_DEBOUNCE_MS 30U
#define SAKI_BUTTON_PAIR_HOLD_MS 2000U
#define SAKI_BUTTON_CLEAR_HOLD_MS 5000U

typedef enum {
    SAKI_BUTTON_EVENT_NONE = 0,
    SAKI_BUTTON_EVENT_SHORT_RELEASE,
    SAKI_BUTTON_EVENT_PAIR_RELEASE,
    SAKI_BUTTON_EVENT_CLEAR_THRESHOLD,
} saki_button_event_t;

typedef struct {
    bool initialized;
    bool raw_pressed;
    bool stable_pressed;
    bool clear_emitted;
    uint64_t raw_changed_ms;
    uint64_t pressed_ms;
} saki_button_policy_t;

void saki_button_policy_init(
    saki_button_policy_t *policy,
    bool pressed,
    uint64_t now_ms
);

saki_button_event_t saki_button_policy_update(
    saki_button_policy_t *policy,
    bool pressed,
    uint64_t now_ms
);

uint32_t saki_button_policy_held_ms(
    const saki_button_policy_t *policy,
    uint64_t now_ms
);

#ifdef __cplusplus
}
#endif

#endif
