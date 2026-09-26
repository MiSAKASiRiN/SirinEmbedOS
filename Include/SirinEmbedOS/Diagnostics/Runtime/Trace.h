#ifndef SRNOS_DIAGNOSTICS_TRACE_H
#define SRNOS_DIAGNOSTICS_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_TraceTaskCreate(void *task, const char *name, void *low, void *high);
void SRNOS_TraceTaskDelete(void *task);
void SRNOS_TraceTaskReady(void *task);
void SRNOS_TraceTaskSwitchedIn(void);
void SRNOS_TraceTaskSwitchedOut(void);
void SRNOS_RuntimeCounterInitialize(void);
uint64_t SRNOS_RuntimeCounterGet(void);
void SRNOS_DiagnosticsTick(void);
#ifdef __cplusplus
}
#endif

/* One owner for FreeRTOS trace macros: fan out to independent consumers here. */
#define traceTASK_CREATE(tcb) SRNOS_TraceTaskCreate((void *)(tcb), (tcb)->pcTaskName, (void *)(tcb)->pxStack, (void *)(tcb)->pxEndOfStack)
#define traceTASK_DELETE(tcb) SRNOS_TraceTaskDelete((void *)(tcb))
#define traceMOVED_TASK_TO_READY_STATE(tcb) SRNOS_TraceTaskReady((void *)(tcb))
#define traceTASK_SWITCHED_IN() SRNOS_TraceTaskSwitchedIn()
#define traceTASK_SWITCHED_OUT() SRNOS_TraceTaskSwitchedOut()
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() SRNOS_RuntimeCounterInitialize()
#define portGET_RUN_TIME_COUNTER_VALUE() SRNOS_RuntimeCounterGet()
#endif
