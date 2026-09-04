#ifndef SAKI_MODEL_H
#define SAKI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAKI_TASK_ID_CAPACITY       65
#define SAKI_TASK_TITLE_CAPACITY    161
#define SAKI_ACTIVITY_CAPACITY      241
#define SAKI_DETAIL_CAPACITY        513
#define SAKI_PROGRESS_LABEL_CAPACITY 65
#define SAKI_AGENT_NAME_CAPACITY    33
#define SAKI_MODEL_NAME_CAPACITY    65
#define SAKI_TRANSPORT_CAPACITY     9

typedef enum {
    SAKI_AGENT_IDLE = 0,
    SAKI_AGENT_STARTING,
    SAKI_AGENT_THINKING,
    SAKI_AGENT_WORKING,
    SAKI_AGENT_WAITING_USER,
    SAKI_AGENT_WAITING_APPROVAL,
    SAKI_AGENT_COMPLETED,
    SAKI_AGENT_FAILED,
    SAKI_AGENT_CANCELLED,
} saki_agent_state_t;

typedef enum {
    SAKI_PROGRESS_NONE = 0,
    SAKI_PROGRESS_INDETERMINATE,
    SAKI_PROGRESS_DETERMINATE,
} saki_progress_mode_t;

typedef enum {
    SAKI_UTF8_COPY_OK = 0,
    SAKI_UTF8_COPY_TRUNCATED,
    SAKI_UTF8_COPY_INVALID,
} saki_utf8_copy_result_t;

typedef struct {
    saki_agent_state_t state;
    bool connected;
    char transport[SAKI_TRANSPORT_CAPACITY];
    char task_id[SAKI_TASK_ID_CAPACITY];
    char task_title[SAKI_TASK_TITLE_CAPACITY];
    char activity[SAKI_ACTIVITY_CAPACITY];
    char detail[SAKI_DETAIL_CAPACITY];
    saki_progress_mode_t progress_mode;
    uint8_t progress_percent;
    char progress_label[SAKI_PROGRESS_LABEL_CAPACITY];
    uint64_t elapsed_ms;
    uint64_t received_at_ms;
    char agent_name[SAKI_AGENT_NAME_CAPACITY];
    char model_name[SAKI_MODEL_NAME_CAPACITY];
} saki_state_snapshot_t;

void saki_state_snapshot_init(saki_state_snapshot_t *snapshot);
void saki_state_snapshot_copy(
    saki_state_snapshot_t *destination,
    const saki_state_snapshot_t *source
);
bool saki_utf8_validate(const uint8_t *data, size_t length);
saki_utf8_copy_result_t saki_utf8_copy(
    char *destination,
    size_t capacity,
    const char *source
);
bool saki_state_is_terminal(saki_agent_state_t state);
bool saki_state_elapsed_is_running(const saki_state_snapshot_t *snapshot);
uint64_t saki_state_elapsed_at(
    const saki_state_snapshot_t *snapshot,
    uint64_t now_ms
);
const char *saki_state_display_name(saki_agent_state_t state);

#ifdef __cplusplus
}
#endif

#endif
