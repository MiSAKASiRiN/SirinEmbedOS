#include "thread_internal.h"
#include "SirinEmbedOS/Config.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "cycle_internal.h"
#include "event_groups.h"
#include <string.h>
#define SRNOS_THREAD_RECORDS 62u /* trace also reserves slots for native idle/timer */
#define SRNOS_THREAD_CONTROL_INDEX 1u
#define SRNOS_THREAD_CONTROL_WAKE 1u
#define SRNOS_THREAD_CONTROL_START 2u
#define SRNOS_THREAD_CONTROL_STOP 4u
#define SRNOS_THREAD_FLAGS_MASK 0x7fffffffu
#define SRNOS_THREAD_EXIT_BIT 1u

typedef struct {
    StaticTask_t tcb;
    StaticEventGroup_t exit_event;
    uint64_t align_stack;
} app_storage_t;
typedef struct {
    TaskHandle_t task;
    uint32_t generation,refs,flags;
    bool allocated,system,deleting;
    volatile bool stop;
    SRNOS_ThreadState state;
    SRNOS_Result init_result;
    char name[16];
    SRNOS_ThreadConfig config;
    osThreadFunc_t entry;
    void *argument;
    app_storage_t *storage;
    EventGroupHandle_t exited;
} thread_record_t;
static thread_record_t *records;
_Static_assert(sizeof(void *)!=4 || sizeof(thread_record_t)*SRNOS_THREAD_RECORDS<=8192, "update registry startup budget");
static unsigned app_count;
static bool prepared,applications_ready;
extern bool SRNOS_KernelPoolPrepare(void);
extern void SRNOS_KernelPoolStatistics(SRNOS_KernelResources *);
static SRNOS_Thread token(thread_record_t *r) {return ((uint64_t)r->generation<<32)|(uint32_t)(r-records+1);}
static thread_record_t *lookup(SRNOS_Thread h)
{
    uint32_t n=(uint32_t)h;
    if(!n || n>SRNOS_THREAD_RECORDS)return NULL;
    thread_record_t *r=&records[n-1];
    return r->allocated && !r->deleting && r->generation==(uint32_t)(h>>32)?r:NULL;
}
static thread_record_t *current(void)
{
    TaskHandle_t t=xTaskGetCurrentTaskHandle();
    /* Only lifecycle/wait APIs use this small registry; cycle hooks do not. */
    for(unsigned i=0;i<SRNOS_THREAD_RECORDS;i++)if(records[i].allocated && records[i].task==t)return &records[i];
    return NULL;
}
static thread_record_t *pin(SRNOS_Thread h)
{
    taskENTER_CRITICAL();thread_record_t *r=lookup(h);if(r)++r->refs;taskEXIT_CRITICAL();return r;
}
static void unpin(thread_record_t *r) {taskENTER_CRITICAL();--r->refs;taskEXIT_CRITICAL();}
static bool ended(thread_record_t *r) {return r->state==SRNOS_THREAD_EXITED || r->state==SRNOS_THREAD_FAILED;}
bool SRNOS_KernelThreadPrepare(bool stats)
{
    if(prepared || osKernelGetState()!=osKernelReady)return false;
    if(!SRNOS_KernelPoolPrepare())return false;
    records=pvPortMalloc(sizeof(*records)*SRNOS_THREAD_RECORDS);
    if(!records || !SRNOS_CycleConfigure(stats))return false;
    memset(records,0,sizeof(*records)*SRNOS_THREAD_RECORDS);prepared=true;return true;
}
static void signal_control(thread_record_t *r,uint32_t bits)
{ if(r->task)(void)xTaskNotifyIndexed(r->task,SRNOS_THREAD_CONTROL_INDEX,bits,eSetBits); }
void SRNOS_KernelThreadReleaseApplications(void)
{
    taskENTER_CRITICAL();applications_ready=true;
    for(unsigned i=0;i<SRNOS_THREAD_RECORDS;i++)if(records[i].allocated && !records[i].system)signal_control(&records[i],SRNOS_THREAD_CONTROL_START);
    taskEXIT_CRITICAL();
}
static SRNOS_Result delay_record(thread_record_t *r,uint32_t ticks)
{
    if(r && !r->system) {
        uint32_t began=xTaskGetTickCount(),bits;
        for(;;) {
            if(r->stop)return SRNOS_STOP_REQUESTED;
            uint32_t elapsed=(uint32_t)xTaskGetTickCount()-began;
            if(ticks!=UINT32_MAX && elapsed>=ticks)return SRNOS_OK;
            (void)xTaskNotifyWaitIndexed(SRNOS_THREAD_CONTROL_INDEX,0,UINT32_MAX,&bits,
                ticks==UINT32_MAX?portMAX_DELAY:ticks-elapsed);
        }
    }
    if(ticks==SRNOS_WAIT_FOREVER)for(;;)vTaskDelay(portMAX_DELAY);
    vTaskDelay(ticks);return SRNOS_OK;
}
SRNOS_Result SRNOS_ThreadYield(void)
{if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;taskYIELD();return SRNOS_OK;}
SRNOS_Result SRNOS_Delay(uint32_t ms)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    if(!SRNOS_KernelTimeoutValid(ms))return SRNOS_INVALID;
    if(!ms)return SRNOS_ThreadYield();return delay_record(current(),ms);
}
SRNOS_Result SRNOS_DelayUntil(uint32_t deadline)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    int32_t remaining=(int32_t)(deadline-(uint32_t)xTaskGetTickCount());
    return remaining<=0?SRNOS_DEADLINE_PASSED:delay_record(current(),(uint32_t)remaining);
}
SRNOS_Result SRNOS_ThreadWaitFlags(uint32_t mask,bool all,uint32_t timeout,uint32_t *out)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    if(!out || !mask || (mask&~SRNOS_THREAD_FLAGS_MASK) || !SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    *out=0;thread_record_t *r=current();if(!r || r->system)return SRNOS_PROTECTED;
    uint32_t start=xTaskGetTickCount();
    for(;;) {
        taskENTER_CRITICAL();bool stop=r->stop;uint32_t flags=r->flags&mask;
        bool ready=all?flags==mask:flags!=0;
        if(ready && !stop)r->flags&=~flags;taskEXIT_CRITICAL();
        if(stop)return SRNOS_STOP_REQUESTED;
        if(ready){*out=flags;return SRNOS_OK;}
        uint32_t elapsed=(uint32_t)xTaskGetTickCount()-start;
        if(timeout!=UINT32_MAX && elapsed>=timeout)return SRNOS_TIMEOUT;
        uint32_t bits;(void)xTaskNotifyWaitIndexed(SRNOS_THREAD_CONTROL_INDEX,0,UINT32_MAX,&bits,
            timeout==UINT32_MAX?portMAX_DELAY:timeout-elapsed);
    }
}
static void trampoline(void *argument)
{
    thread_record_t *r=argument;uint32_t bits;
    /* Creator publishes native identity before unblocking the wrapper. */
    (void)xTaskNotifyWaitIndexed(SRNOS_THREAD_CONTROL_INDEX,0,UINT32_MAX,&bits,portMAX_DELAY);
    if(r->system) {
        r->state=SRNOS_THREAD_RUNNING;r->entry(r->argument);
        taskENTER_CRITICAL();r->allocated=false;taskEXIT_CRITICAL();
        vTaskDelete(NULL);return;
    }
    while(!applications_ready && !r->stop)
        (void)xTaskNotifyWaitIndexed(SRNOS_THREAD_CONTROL_INDEX,0,UINT32_MAX,&bits,portMAX_DELAY);
    bool failed=false;
    if(!r->stop) {
        r->init_result=r->config.init?r->config.init(r->config.argument):SRNOS_OK;
        failed=r->init_result!=SRNOS_OK;
    }
    uint32_t release=xTaskGetTickCount();
    SRNOS_CycleContext cycle;
    SRNOS_CycleAttach(&cycle,r->config.mode==SRNOS_TASK_PERIODIC?r->config.period_ms:0);
    r->state=SRNOS_THREAD_RUNNING;
    while(!r->stop && !failed) {
        SRNOS_ThreadWake wake={SRNOS_WAKE_START,0};
        if(r->config.mode==SRNOS_TASK_EVENT) {
            SRNOS_Result result=SRNOS_ThreadWaitFlags(r->config.event_mask,false,r->config.event_timeout_ms,&wake.flags);
            if(result==SRNOS_STOP_REQUESTED)break;
            wake.reason=result==SRNOS_OK?SRNOS_WAKE_FLAGS:SRNOS_WAKE_TIMEOUT;
        } else if(r->config.mode==SRNOS_TASK_PERIODIC) {
            if((int32_t)(release-(uint32_t)xTaskGetTickCount())>0 && SRNOS_DelayUntil(release)==SRNOS_STOP_REQUESTED)break;
            wake.reason=SRNOS_WAKE_PERIOD;
        }
        if(r->stop)break;
        SRNOS_CycleBegin(&cycle,release);
        SRNOS_StepResult result=r->config.step(r->config.argument,&wake);
        SRNOS_CycleEnd(&cycle);
        if(result!=SRNOS_STEP_CONTINUE || r->config.mode==SRNOS_TASK_ONCE) {failed=result==SRNOS_STEP_FAILED;break;}
        if(r->config.mode==SRNOS_TASK_PERIODIC) {
            uint32_t skips;
            uint32_t next=SRNOS_KernelNextRelease(release,r->config.period_ms,xTaskGetTickCount(),&skips);
            SRNOS_CycleSkipped(&cycle,skips);release=next;
        }
    }
    r->state=SRNOS_THREAD_STOPPING;
    if(r->config.cleanup)r->config.cleanup(r->config.argument);
    taskENTER_CRITICAL();r->state=failed?SRNOS_THREAD_FAILED:SRNOS_THREAD_EXITED;
    /* After publishing exit, no application memory or record is accessed.
     * Delete may preempt here and delete this non-current task safely. */
    xEventGroupSetBits(r->exited,SRNOS_THREAD_EXIT_BIT);
    vTaskSuspend(NULL);
    taskEXIT_CRITICAL();
    for(;;)vTaskSuspend(NULL);
}
static thread_record_t *reserve(const char *name,bool system)
{
    thread_record_t *found=NULL;
    taskENTER_CRITICAL();
    if(!system && app_count>=SRNOS_APP_THREAD_LIMIT) {taskEXIT_CRITICAL();return NULL;}
    for(unsigned i=0;i<SRNOS_THREAD_RECORDS;i++)if(records[i].allocated && !strcmp(records[i].name,name)) {taskEXIT_CRITICAL();return NULL;}
    for(unsigned i=0;i<SRNOS_THREAD_RECORDS;i++)if(!records[i].allocated) {found=&records[i];break;}
    if(found) {
        uint32_t generation=found->generation+1;if(!generation)generation=1;
        memset(found,0,sizeof(*found));found->generation=generation;found->allocated=true;found->system=system;
        memcpy(found->name,name,strlen(name)+1);found->state=SRNOS_THREAD_STARTING;found->init_result=SRNOS_BUSY;
        if(!system)++app_count;
    }
    taskEXIT_CRITICAL();return found;
}
static void abandon(thread_record_t *r)
{taskENTER_CRITICAL();r->allocated=false;if(!r->system)--app_count;taskEXIT_CRITICAL();}
static SRNOS_Result create(const SRNOS_ThreadConfig *c,SRNOS_Thread *out,bool configured)
{
    if(!out)return SRNOS_INVALID;*out=0;
    if(!prepared || (!configured && (!SRNOS_KernelTaskContext() || !applications_ready)))return SRNOS_CONTEXT;
    if(!SRNOS_KernelThreadConfigValid(c))return SRNOS_INVALID;
    thread_record_t *r=reserve(c->name,false);if(!r)return SRNOS_KernelCreateFailed();
    r->config=*c;r->config.name=r->name;
    r->storage=SRNOS_KernelPoolAllocate(sizeof(app_storage_t)+c->stack_bytes);
    if(!r->storage){abandon(r);return SRNOS_KernelCreateFailed();}
    r->exited=xEventGroupCreateStatic(&r->storage->exit_event);
    osThreadAttr_t attr={.name=r->name,.priority=(osPriority_t)c->priority,
        .stack_size=c->stack_bytes,.stack_mem=(void *)(r->storage+1),
        .cb_mem=&r->storage->tcb,.cb_size=sizeof(r->storage->tcb)};
    r->task=osThreadNew(trampoline,r,&attr);
    if(!r->task){vEventGroupDelete(r->exited);SRNOS_KernelPoolFree(r->storage);abandon(r);return SRNOS_KernelCreateFailed();}
    *out=token(r);signal_control(r,SRNOS_THREAD_CONTROL_START);return SRNOS_OK;
}
SRNOS_Result SRNOS_ThreadCreate(const SRNOS_ThreadConfig *c,SRNOS_Thread *out) {return create(c,out,false);}
SRNOS_Result SRNOS_KernelThreadCreateConfigured(const SRNOS_ThreadConfig *c,SRNOS_Thread *out) {return create(c,out,true);}
osThreadId_t SRNOS_KernelThreadNew(osThreadFunc_t entry,void *arg,const osThreadAttr_t *attr)
{
    if(!prepared || !entry || !attr || !attr->name || strncmp(attr->name,"SRNOS_",6) || strnlen(attr->name,16)>=16)return NULL;
    thread_record_t *r=reserve(attr->name,true);if(!r)return NULL;
    r->entry=entry;r->argument=arg;r->config.stack_bytes=attr->stack_size;r->config.priority=attr->priority;
    r->task=osThreadNew(trampoline,r,attr);
    if(!r->task){abandon(r);return NULL;}signal_control(r,SRNOS_THREAD_CONTROL_START);return r->task;
}
bool SRNOS_KernelThreadAbortStartup(osThreadId_t task)
{
    if(osKernelGetState()!=osKernelReady || !task)return false;
    for(unsigned i=0;i<SRNOS_THREAD_RECORDS;i++) {
        thread_record_t *r=&records[i];
        if(r->allocated && r->system && r->task==task){vTaskDelete(task);abandon(r);return true;}
    }
    return false;
}
SRNOS_Thread SRNOS_ThreadGetCurrent(void)
{if(!SRNOS_KernelTaskContext())return 0;thread_record_t *r=current();return r?token(r):0;}
bool SRNOS_ThreadShouldStop(void)
{if(!SRNOS_KernelTaskContext())return false;thread_record_t *r=current();return r && r->stop;}
SRNOS_Result SRNOS_ThreadGetInfo(SRNOS_Thread h,SRNOS_ThreadInfo *out)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!out)return SRNOS_INVALID;
    taskENTER_CRITICAL();thread_record_t *r=lookup(h);
    if(r){memcpy(out->name,r->name,16);out->state=r->state;out->init_result=r->init_result;
        out->stop_requested=r->stop;out->system_owned=r->system;out->stack_bytes=r->config.stack_bytes;out->priority=r->config.priority;}
    taskEXIT_CRITICAL();return r?SRNOS_OK:SRNOS_STALE_HANDLE;
}
SRNOS_Result SRNOS_ThreadRequestStop(SRNOS_Thread h)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    thread_record_t *r=pin(h);if(!r)return SRNOS_STALE_HANDLE;
    if(r->system){unpin(r);return SRNOS_PROTECTED;}
    taskENTER_CRITICAL();r->stop=true;signal_control(r,SRNOS_THREAD_CONTROL_STOP);taskEXIT_CRITICAL();unpin(r);return SRNOS_OK;
}
static SRNOS_Result wait_exit(thread_record_t *r,uint32_t timeout)
{
    if(r->system)return SRNOS_PROTECTED;
    if(r->task==xTaskGetCurrentTaskHandle())return SRNOS_INVALID;
    if(ended(r))return SRNOS_OK;
    return (xEventGroupWaitBits(r->exited,SRNOS_THREAD_EXIT_BIT,pdFALSE,pdTRUE,timeout)&SRNOS_THREAD_EXIT_BIT)?SRNOS_OK:SRNOS_TIMEOUT;
}
SRNOS_Result SRNOS_ThreadJoin(SRNOS_Thread h,uint32_t timeout)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    thread_record_t *r=pin(h);if(!r)return SRNOS_STALE_HANDLE;
    SRNOS_Result result=wait_exit(r,timeout);unpin(r);return result;
}
SRNOS_Result SRNOS_ThreadDelete(SRNOS_Thread h,uint32_t timeout)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    thread_record_t *r=pin(h);if(!r)return SRNOS_STALE_HANDLE;
    if(r->system || r->task==xTaskGetCurrentTaskHandle()){SRNOS_Result e=r->system?SRNOS_PROTECTED:SRNOS_INVALID;unpin(r);return e;}
    taskENTER_CRITICAL();r->stop=true;signal_control(r,SRNOS_THREAD_CONTROL_STOP);taskEXIT_CRITICAL();
    SRNOS_Result result=wait_exit(r,timeout);if(result!=SRNOS_OK){unpin(r);return result;}
    taskENTER_CRITICAL();if(r->refs!=1){taskEXIT_CRITICAL();unpin(r);return SRNOS_BUSY;}r->deleting=true;taskEXIT_CRITICAL();
    vTaskDelete(r->task);vEventGroupDelete(r->exited);SRNOS_KernelPoolFree(r->storage);abandon(r);return SRNOS_OK;
}
SRNOS_Result SRNOS_ThreadNotify(SRNOS_Thread h,uint32_t flags)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!flags || (flags&~SRNOS_THREAD_FLAGS_MASK))return SRNOS_INVALID;
    thread_record_t *r=pin(h);if(!r)return SRNOS_STALE_HANDLE;
    SRNOS_Result result=SRNOS_OK;
    taskENTER_CRITICAL();if(r->system)result=SRNOS_PROTECTED;else if(ended(r))result=SRNOS_BUSY;
    else {r->flags|=flags;signal_control(r,SRNOS_THREAD_CONTROL_WAKE);}taskEXIT_CRITICAL();unpin(r);return result;
}
SRNOS_Result SRNOS_ThreadNotifyFromISR(SRNOS_Thread h,uint32_t flags)
{
    if(!SRNOS_KernelISRContext())return SRNOS_CONTEXT;if(!flags || (flags&~SRNOS_THREAD_FLAGS_MASK))return SRNOS_INVALID;
    UBaseType_t saved=taskENTER_CRITICAL_FROM_ISR();thread_record_t *r=lookup(h);BaseType_t higher=pdFALSE;
    SRNOS_Result result=!r?SRNOS_STALE_HANDLE:r->system?SRNOS_PROTECTED:ended(r)?SRNOS_BUSY:SRNOS_OK;
    if(result==SRNOS_OK){r->flags|=flags;(void)xTaskNotifyIndexedFromISR(r->task,SRNOS_THREAD_CONTROL_INDEX,SRNOS_THREAD_CONTROL_WAKE,eSetBits,&higher);}
    taskEXIT_CRITICAL_FROM_ISR(saved);portYIELD_FROM_ISR(higher);return result;
}
SRNOS_Result SRNOS_KernelGetResources(SRNOS_KernelResources *out)
{
    if(!SRNOS_KernelTaskContext() || !prepared)return SRNOS_CONTEXT;if(!out)return SRNOS_INVALID;
    SRNOS_KernelPoolStatistics(out);out->kernel_free_bytes=(uint32_t)xPortGetFreeHeapSize();
    out->kernel_minimum_free_bytes=(uint32_t)xPortGetMinimumEverFreeHeapSize();taskENTER_CRITICAL();out->app_threads=app_count;taskEXIT_CRITICAL();out->thread_limit=SRNOS_APP_THREAD_LIMIT;return SRNOS_OK;
}
void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop)
{
    configASSERT(loop && loop->step);
    SRNOS_CycleContext cycle;SRNOS_CycleAttach(&cycle,loop->period_ms);
    bool first=true;uint32_t release=xTaskGetTickCount();
    for(;;) {
        if((!first || loop->wait_first) && loop->wait && !loop->wait(loop->context))break;
        if(loop->period_ms && !first) {
            uint32_t now=xTaskGetTickCount(),next=release+loop->period_ms;
            if((int32_t)(now-next)>=(int32_t)loop->period_ms) {
                uint32_t skipped=(now-next)/loop->period_ms;SRNOS_CycleSkipped(&cycle,skipped);next+=skipped*loop->period_ms;
            }
            release=next;
        }
        SRNOS_CycleBegin(&cycle,release);loop->step(loop->context);SRNOS_CycleEnd(&cycle);first=false;
    }
}
