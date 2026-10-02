#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SRNOS_RUNTIME_MAX_TASKS 64u
/* No task handle escapes the protected sample operation. */
typedef struct {
    uint64_t sampled_us, cycles, runtime;
    uint32_t generation, switch_count, ready_count, max_run_cycles, max_ready_cycles;
    uint8_t slot, state, priority, base_priority;
    bool active, idle, self;
    char name[16];
} SRNOS_RuntimeSample;
void SRNOS_RuntimeStatsInit(void);
uint64_t SRNOS_RuntimeStatsCounter(void);
void SRNOS_RuntimeTraceCreate(void *);
void SRNOS_RuntimeTraceDelete(void *);
void SRNOS_RuntimeTraceReady(void *);
void SRNOS_RuntimeTraceIn(void);
void SRNOS_RuntimeTraceOut(void);
void SRNOS_RuntimeSampleSlot(uint8_t slot, SRNOS_RuntimeSample *out);
uint32_t SRNOS_RuntimeOverflows(void);
uint32_t SRNOS_RuntimeMaxCriticalCycles(void);

bool SRNOS_RuntimeCurrentIdentity(uint8_t *slot,uint32_t *generation);
