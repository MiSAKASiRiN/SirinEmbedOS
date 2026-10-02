#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_WAIT_FOREVER UINT32_MAX
#define SRNOS_NO_WAIT 0u
typedef enum {
    SRNOS_OK=0, SRNOS_INVALID=-1, SRNOS_CONTEXT=-2, SRNOS_RESOURCE=-3,
    SRNOS_TIMEOUT=-4, SRNOS_BUSY=-5, SRNOS_STALE_HANDLE=-6,
    SRNOS_STOP_REQUESTED=-7, SRNOS_DEADLINE_PASSED=-8, SRNOS_PROTECTED=-9
} SRNOS_Result;
/* Value tokens, never RTOS pointers. Zero is invalid. Do not inspect encoding. */
typedef uint64_t SRNOS_Thread;
typedef enum { SRNOS_TASK_ONCE, SRNOS_TASK_PERIODIC, SRNOS_TASK_EVENT } SRNOS_TaskMode;
typedef enum { SRNOS_STEP_CONTINUE, SRNOS_STEP_STOP, SRNOS_STEP_FAILED } SRNOS_StepResult;
typedef enum { SRNOS_WAKE_START, SRNOS_WAKE_PERIOD, SRNOS_WAKE_FLAGS, SRNOS_WAKE_TIMEOUT } SRNOS_WakeReason;
typedef struct { SRNOS_WakeReason reason; uint32_t flags; } SRNOS_ThreadWake;
typedef struct {
    const char *name; /* Copied, 1..15 visible bytes; SRNOS_ reserved for SDK. */
    SRNOS_Result (*init)(void *argument);
    SRNOS_StepResult (*step)(void *argument, const SRNOS_ThreadWake *wake);
    void (*cleanup)(void *argument); /* Called once, including init failure. */
    void *argument; /* Caller-owned until Delete succeeds. */
    uint32_t stack_bytes; /* 1024..32768, multiple of 8; includes TLS. */
    uint32_t priority; /* 1..39; fixed SDK workers retain higher priorities. */
    SRNOS_TaskMode mode;
    uint32_t period_ms; /* PERIODIC: 1..INT32_MAX. */
    uint32_t event_mask; /* EVENT: nonzero, bits 0..30. */
    uint32_t event_timeout_ms; /* EVENT: positive finite or WAIT_FOREVER. */
} SRNOS_ThreadConfig;
typedef enum { SRNOS_THREAD_STARTING, SRNOS_THREAD_RUNNING, SRNOS_THREAD_STOPPING,
               SRNOS_THREAD_EXITED, SRNOS_THREAD_FAILED } SRNOS_ThreadState;
typedef struct {
    char name[16];
    SRNOS_ThreadState state;
    SRNOS_Result init_result;
    bool stop_requested, system_owned;
    uint32_t stack_bytes, priority;
} SRNOS_ThreadInfo;
typedef struct {
    uint32_t capacity_bytes, used_bytes, peak_bytes, allocation_failures;
    uint32_t app_threads, thread_limit, creation_failures;
    uint32_t kernel_free_bytes, kernel_minimum_free_bytes;
} SRNOS_KernelResources;

/* Task context, after application startup readiness. Errors do not panic.
 * Create copies config/name. Callbacks may begin before Create returns.
 * Exit is cooperative; cleanup must release owned resources. Join keeps the
 * object; Delete reclaims only after exit. Self Delete/Join are rejected. */
SRNOS_Result SRNOS_ThreadCreate(const SRNOS_ThreadConfig *, SRNOS_Thread *out);
SRNOS_Result SRNOS_ThreadRequestStop(SRNOS_Thread);
SRNOS_Result SRNOS_ThreadJoin(SRNOS_Thread, uint32_t timeout_ms);
SRNOS_Result SRNOS_ThreadDelete(SRNOS_Thread, uint32_t timeout_ms);
SRNOS_Thread SRNOS_ThreadGetCurrent(void);
SRNOS_Result SRNOS_ThreadGetInfo(SRNOS_Thread, SRNOS_ThreadInfo *out);
bool SRNOS_ThreadShouldStop(void);
SRNOS_Result SRNOS_ThreadYield(void);
SRNOS_Result SRNOS_ThreadNotify(SRNOS_Thread, uint32_t flags);
SRNOS_Result SRNOS_ThreadNotifyFromISR(SRNOS_Thread, uint32_t flags);
/* Current managed task, bits 0..30; matched bits are consumed. */
SRNOS_Result SRNOS_ThreadWaitFlags(uint32_t mask, bool wait_all, uint32_t timeout_ms, uint32_t *received);
/* Tick-based scheduling clock wraps at 2^32 ms. Finite waits <= INT32_MAX.
 * TimeUs is a separate DWT observation clock; do not mix clock epochs. */
uint32_t SRNOS_TimeMs(void);
uint64_t SRNOS_TimeUs(void);
SRNOS_Result SRNOS_Delay(uint32_t ms); /* Zero yields; stop interrupts delay. */
SRNOS_Result SRNOS_DelayUntil(uint32_t deadline_ms);
SRNOS_Result SRNOS_KernelGetResources(SRNOS_KernelResources *out);
#ifdef __cplusplus
}
#endif
