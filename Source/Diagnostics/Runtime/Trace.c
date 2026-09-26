#include "SirinEmbedOS/Diagnostics/Runtime/Trace.h"
#include "SirinEmbedOS/Diagnostics/Runtime/tskptt_hskp.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "STM32H7Fault.h"

void SRNOS_TraceTaskCreate(void *task, const char *name, void *low, void *high)
{
    SRNOS_STM32H7_TraceTaskCreate(task, name, low, high);
    TSKPTT_HSKP_TraceTaskCreate(task);
}
void SRNOS_TraceTaskDelete(void *task)
{
    SRNOS_STM32H7_TraceTaskDelete(task);
    TSKPTT_HSKP_TraceTaskDelete(task);
}
void SRNOS_TraceTaskReady(void *task) { TSKPTT_HSKP_TraceTaskReady(task); }
void SRNOS_TraceTaskSwitchedIn(void) { TSKPTT_HSKP_TraceTaskSwitchedIn(); }
void SRNOS_TraceTaskSwitchedOut(void) { TSKPTT_HSKP_TraceTaskSwitchedOut(); }
void SRNOS_RuntimeCounterInitialize(void) { TSKPTT_HSKP_RuntimeStatsInit(); }
uint64_t SRNOS_RuntimeCounterGet(void) { return TSKPTT_HSKP_RuntimeCounterGet(); }
void SRNOS_DiagnosticsTick(void)
{
    /* Maintain CYCCNT wrap extension even when the sampling worker is disabled.
     * Frequencies are fixed after boot; a tick is much shorter than one wrap. */
    if (DWT_IsReady()) (void)DWT_GetUptimeCycles64();
}
