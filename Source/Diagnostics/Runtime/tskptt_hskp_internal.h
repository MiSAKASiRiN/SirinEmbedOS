#ifndef OMX_TSKPTT_HSKP_INTERNAL_H
#define OMX_TSKPTT_HSKP_INTERNAL_H

#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Runtime/tskptt_hskp.h"
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    TaskHandle_t handle;
    UBaseType_t task_number;
    char name[configMAX_TASK_NAME_LEN];
    uint32_t switch_count;
    uint32_t ready_count;
    uint32_t max_run_cycles;
    uint32_t max_ready_cycles;
} tskptt_hskp_trace_snapshot_t;

size_t tskptt_hskp_trace_snapshot(tskptt_hskp_trace_snapshot_t *out,
                                  size_t capacity,
                                  uint8_t reset_window);
uint32_t tskptt_hskp_trace_get_overflow_count(void);

#endif /* OMX_TSKPTT_HSKP_INTERNAL_H */
