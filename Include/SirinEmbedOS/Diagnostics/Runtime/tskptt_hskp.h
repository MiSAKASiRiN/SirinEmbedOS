#ifndef OMX_TSKPTT_HSKP_H
#define OMX_TSKPTT_HSKP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSKPTT_HSKP_MAX_TASKS (64u)

typedef int32_t (*tskptt_hskp_publish_fn)(void *user,
                                         uint32_t sequence,
                                         uint32_t device_time_us,
                                         const uint8_t *payload,
                                         size_t payload_len);

typedef struct
{
    uint32_t sample_period_ms;
    uint32_t summary_period_ms;
    uint32_t stack_scan_period_ms;
    uint32_t warning_repeat_ms;
    uint32_t late_warning_ms;
    uint16_t stack_warning_words;
    uint16_t stack_critical_words;
    uint8_t detail_rows_per_report;
    uint8_t max_warnings_per_report;
    uint8_t reserved[2];
    tskptt_hskp_publish_fn publish;
    void *publish_user;
} tskptt_hskp_config_t;

extern const tskptt_hskp_config_t TSKPTT_HSKP_DEFAULT_CONFIG;

void TSKPTT_HSKP_Task(void *argument);
void TSKPTT_HSKP_TelemetryConfigure(bool enabled, uint32_t period_us);

void TSKPTT_HSKP_RuntimeStatsInit(void);
uint64_t TSKPTT_HSKP_RuntimeCounterGet(void);

void TSKPTT_HSKP_TraceTaskCreate(void *task_handle);
void TSKPTT_HSKP_TraceTaskDelete(void *task_handle);
void TSKPTT_HSKP_TraceTaskReady(void *task_handle);
void TSKPTT_HSKP_TraceTaskSwitchedIn(void);
void TSKPTT_HSKP_TraceTaskSwitchedOut(void);

#ifdef __cplusplus
}
#endif

#endif /* OMX_TSKPTT_HSKP_H */
