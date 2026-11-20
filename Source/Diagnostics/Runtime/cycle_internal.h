#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "SirinEmbedOS/Diagnostics/Runtime/TaskStatistics.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A context belongs to the task's execution wrapper, never an App callback. */
typedef struct {
    uint64_t began,previous,release;
    uint32_t generation,period_ms,begin_cost;
    uint8_t slot;
    bool enabled,have_previous;
} SRNOS_CycleContext;
#ifdef SRNOS_MP257_M33
/* This target omits runtime sampling. The thread lifecycle and waits remain common. */
static inline bool SRNOS_CycleConfigure(bool enabled) { return !enabled; }
static inline void SRNOS_CycleAttach(SRNOS_CycleContext *c, uint32_t period) { (void)c; (void)period; }
static inline void SRNOS_CycleBegin(SRNOS_CycleContext *c, uint32_t tick) { (void)c; (void)tick; }
static inline void SRNOS_CycleEnd(SRNOS_CycleContext *c) { (void)c; }
static inline void SRNOS_CycleSkipped(SRNOS_CycleContext *c, uint32_t count) { (void)c; (void)count; }
#else
bool SRNOS_CycleConfigure(bool enabled);
uint32_t SRNOS_CycleMaxCriticalCycles(void);
void SRNOS_CycleTick(uint64_t cycles,uint32_t tick);
void SRNOS_CycleAttach(SRNOS_CycleContext *,uint32_t period_ms);
void SRNOS_CycleBegin(SRNOS_CycleContext *,uint32_t release_tick);
void SRNOS_CycleEnd(SRNOS_CycleContext *);
void SRNOS_CycleSkipped(SRNOS_CycleContext *,uint32_t count);
void SRNOS_CycleSample(uint8_t slot,uint32_t generation,SRNOS_MonitorCycle *out);
#endif
#ifdef __cplusplus
}
#endif
