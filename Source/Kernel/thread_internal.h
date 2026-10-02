#pragma once
#include "SirinEmbedOS/Kernel/Thread.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#ifdef __cplusplus
extern "C" {
#endif
/* SDK-only bootstrap adapter. All SDK task creation goes through this gate.
 * Entry initializes service-local state then calls RunLoop, which owns its
 * outer loop. Native driver waits remain private to the service. */
osThreadId_t SRNOS_KernelThreadNew(osThreadFunc_t entry, void *arg, const osThreadAttr_t *attr);
bool SRNOS_KernelThreadAbortStartup(osThreadId_t thread);
typedef struct {
    void (*step)(void *context);
    bool (*wait)(void *context); /* false ends loop; outside cycle measurement */
    void *context;
    uint32_t period_ms; /* zero => event/relative cadence, jitter not applicable */
    bool wait_first;
} SRNOS_InternalLoop;
void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop);
/* Strictly future fixed-phase release; finite periods use half-range ticks. */
static inline uint32_t SRNOS_KernelNextRelease(uint32_t release,uint32_t period,uint32_t now,uint32_t *skipped)
{
    uint32_t next=release+period;
    *skipped=(int32_t)(now-next)>=0?(now-next)/period+1u:0u;
    return next+*skipped*period;
}
bool SRNOS_KernelThreadPrepare(bool cycle_statistics);
void SRNOS_KernelThreadReleaseApplications(void);
bool SRNOS_KernelThreadConfigValid(const SRNOS_ThreadConfig *);
SRNOS_Result SRNOS_KernelThreadCreateConfigured(const SRNOS_ThreadConfig *, SRNOS_Thread *);
/* Pool operations are task-only, protected by their own priority-inheritance
 * mutex. Bitmap scanning/clearing never globally masks interrupts. */
SRNOS_Result SRNOS_KernelCreateFailed(void);
void *SRNOS_KernelPoolAllocate(size_t bytes);
void SRNOS_KernelPoolFree(void *memory);
bool SRNOS_KernelTaskContext(void);
bool SRNOS_KernelISRContext(void);
bool SRNOS_KernelTimeoutValid(uint32_t ms);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
/* Stack-owned captures preserve existing service state and buffers; the
 * actual loop and hooks still live in the C kernel implementation. */
template<class Step,class Wait>
static inline void SRNOS_KernelRunCpp(Step step,Wait wait,uint32_t period_ms=0,bool wait_first=true)
{
    struct Context {Step *step;Wait *wait;};
    Context context{&step,&wait};
    const SRNOS_InternalLoop loop={
        [](void *p){auto *c=static_cast<Context *>(p);(*c->step)();},
        [](void *p)->bool{auto *c=static_cast<Context *>(p);return (*c->wait)();},
        &context,period_ms,wait_first};
    SRNOS_KernelRunLoop(&loop);
}
#endif
