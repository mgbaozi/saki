#include "saki_model.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void saki_state_snapshot_init(saki_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->state = SAKI_AGENT_IDLE;
    snapshot->progress_mode = SAKI_PROGRESS_NONE;
    snprintf(snapshot->agent_name, sizeof(snapshot->agent_name), "%s", "Agent");
    snprintf(snapshot->transport, sizeof(snapshot->transport), "%s", "OFFLINE");
}

void saki_state_snapshot_copy(
    saki_state_snapshot_t *destination,
    const saki_state_snapshot_t *source
)
{
    if (destination == NULL || source == NULL) {
        return;
    }
    *destination = *source;
}

bool saki_utf8_validate(const uint8_t *data, size_t length)
{
    const uint8_t *cursor = data;
    size_t remaining = length;

    if (data == NULL) {
        return false;
    }
    while (remaining > 0) {
        if (*cursor <= 0x7F) {
            ++cursor;
            --remaining;
        } else if (remaining >= 2 && *cursor >= 0xC2 && *cursor <= 0xDF &&
                   cursor[1] >= 0x80 && cursor[1] <= 0xBF) {
            cursor += 2;
            remaining -= 2;
        } else if (remaining >= 3 && *cursor == 0xE0 &&
                   cursor[1] >= 0xA0 && cursor[1] <= 0xBF &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF) {
            cursor += 3;
            remaining -= 3;
        } else if (remaining >= 3 && ((*cursor >= 0xE1 && *cursor <= 0xEC) ||
                    (*cursor >= 0xEE && *cursor <= 0xEF)) &&
                   cursor[1] >= 0x80 && cursor[1] <= 0xBF &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF) {
            cursor += 3;
            remaining -= 3;
        } else if (remaining >= 3 && *cursor == 0xED &&
                   cursor[1] >= 0x80 && cursor[1] <= 0x9F &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF) {
            cursor += 3;
            remaining -= 3;
        } else if (remaining >= 4 && *cursor == 0xF0 &&
                   cursor[1] >= 0x90 && cursor[1] <= 0xBF &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF &&
                   cursor[3] >= 0x80 && cursor[3] <= 0xBF) {
            cursor += 4;
            remaining -= 4;
        } else if (remaining >= 4 && *cursor >= 0xF1 && *cursor <= 0xF3 &&
                   cursor[1] >= 0x80 && cursor[1] <= 0xBF &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF &&
                   cursor[3] >= 0x80 && cursor[3] <= 0xBF) {
            cursor += 4;
            remaining -= 4;
        } else if (remaining >= 4 && *cursor == 0xF4 &&
                   cursor[1] >= 0x80 && cursor[1] <= 0x8F &&
                   cursor[2] >= 0x80 && cursor[2] <= 0xBF &&
                   cursor[3] >= 0x80 && cursor[3] <= 0xBF) {
            cursor += 4;
            remaining -= 4;
        } else {
            return false;
        }
    }
    return true;
}

saki_utf8_copy_result_t saki_utf8_copy(
    char *destination,
    size_t capacity,
    const char *source
)
{
    size_t source_length;
    size_t copy_length;

    if (destination == NULL || capacity == 0 || source == NULL) {
        return SAKI_UTF8_COPY_INVALID;
    }
    destination[0] = '\0';
    source_length = strlen(source);
    if (!saki_utf8_validate((const uint8_t *)source, source_length)) {
        return SAKI_UTF8_COPY_INVALID;
    }
    if (source_length < capacity) {
        memcpy(destination, source, source_length + 1);
        return SAKI_UTF8_COPY_OK;
    }

    copy_length = capacity - 1;
    while (copy_length > 0 &&
           ((uint8_t)source[copy_length] & 0xC0) == 0x80) {
        --copy_length;
    }
    memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
    return SAKI_UTF8_COPY_TRUNCATED;
}

bool saki_state_is_terminal(saki_agent_state_t state)
{
    return state == SAKI_AGENT_COMPLETED ||
           state == SAKI_AGENT_FAILED ||
           state == SAKI_AGENT_CANCELLED;
}

bool saki_state_elapsed_is_running(const saki_state_snapshot_t *snapshot)
{
    return snapshot != NULL &&
           snapshot->connected &&
           snapshot->state != SAKI_AGENT_IDLE &&
           !saki_state_is_terminal(snapshot->state);
}

uint64_t saki_state_elapsed_at(
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
)
{
    uint64_t delta_ms;

    if (snapshot == NULL) {
        return 0;
    }
    if (!saki_state_elapsed_is_running(snapshot) ||
        now_ms <= snapshot->received_at_ms) {
        return snapshot->elapsed_ms;
    }
    delta_ms = now_ms - snapshot->received_at_ms;
    if (UINT64_MAX - snapshot->elapsed_ms < delta_ms) {
        return UINT64_MAX;
    }
    return snapshot->elapsed_ms + delta_ms;
}

const char *saki_state_display_name(saki_agent_state_t state)
{
    switch (state) {
        case SAKI_AGENT_IDLE:
            return "IDLE";
        case SAKI_AGENT_STARTING:
            return "STARTING";
        case SAKI_AGENT_THINKING:
            return "THINKING";
        case SAKI_AGENT_WORKING:
            return "WORKING";
        case SAKI_AGENT_WAITING_USER:
            return "WAITING FOR YOU";
        case SAKI_AGENT_WAITING_APPROVAL:
            return "WAITING APPROVAL";
        case SAKI_AGENT_COMPLETED:
            return "COMPLETED";
        case SAKI_AGENT_FAILED:
            return "FAILED";
        case SAKI_AGENT_CANCELLED:
            return "CANCELLED";
        default:
            return "UNKNOWN";
    }
}
