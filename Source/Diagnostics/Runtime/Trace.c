#include "Diagnostics/Runtime/trace_internal.h"
#include "runtime_internal.h"
#include "cycle_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "System/fault_platform.h"

void SRNOS_TraceTaskCreate(void *task, const char *name, void *low, void *high)
{
    SRNOS_STM32H7_TraceTaskCreate(task, name, low, high);
    SRNOS_RuntimeTraceCreate(task);
}
void SRNOS_TraceTaskDelete(void *task)
{
    SRNOS_STM32H7_TraceTaskDelete(task);
    SRNOS_RuntimeTraceDelete(task);
}
void SRNOS_TraceTaskReady(void *task) { SRNOS_RuntimeTraceReady(task); }
void SRNOS_TraceTaskSwitchedIn(void) { SRNOS_RuntimeTraceIn(); }
void SRNOS_TraceTaskSwitchedOut(void) { SRNOS_RuntimeTraceOut(); }
void SRNOS_RuntimeCounterInitialize(void) { SRNOS_RuntimeStatsInit(); }
uint64_t SRNOS_RuntimeCounterGet(void) { return SRNOS_RuntimeStatsCounter(); }
void SRNOS_DiagnosticsTick(void)
{
    /* Maintain CYCCNT wrap extension even when the sampling worker is disabled.
     * Frequencies are fixed after boot; a tick is much shorter than one wrap. */
    if (SRNOS_TimeIsReady()) SRNOS_CycleTick(SRNOS_TimeGetUptimeCycles64(),xTaskGetTickCountFromISR());
}
