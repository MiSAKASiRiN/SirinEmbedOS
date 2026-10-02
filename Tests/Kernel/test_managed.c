#include "SirinEmbedOS/Kernel/Sync.h"
#include "thread_internal.h"
#include "fake_runtime.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
typedef struct {atomic_uint init,steps,cleanup,entered;unsigned behavior;SRNOS_Queue queue;SRNOS_Mutex mutex;uint32_t flags;} context_t;
static SRNOS_Result init(void *p){context_t *c=p;++c->init;if(c->behavior==6)assert(SRNOS_Delay(SRNOS_WAIT_FOREVER)==SRNOS_STOP_REQUESTED);return c->behavior==1?SRNOS_RESOURCE:SRNOS_OK;}
static void cleanup(void *p){++((context_t *)p)->cleanup;}
static SRNOS_StepResult step(void *p,const SRNOS_ThreadWake *wake)
{
 context_t *c=p;++c->steps;c->flags=wake->flags;
 if(c->behavior==2){assert(SRNOS_ThreadDelete(SRNOS_ThreadGetCurrent(),0)==SRNOS_INVALID);assert(SRNOS_ThreadJoin(SRNOS_ThreadGetCurrent(),0)==SRNOS_INVALID);return SRNOS_STEP_FAILED;}
 if(c->behavior==3){c->entered=1;unsigned value;assert(SRNOS_QueueReceive(c->queue,&value,SRNOS_WAIT_FOREVER)==SRNOS_OK);assert(value==123);return SRNOS_STEP_CONTINUE;}
 if(c->behavior==4){assert(SRNOS_MutexLock(c->mutex,0)==SRNOS_OK);c->entered=1;assert(SRNOS_Delay(SRNOS_WAIT_FOREVER)==SRNOS_STOP_REQUESTED);assert(SRNOS_MutexUnlock(c->mutex)==SRNOS_OK);return SRNOS_STEP_STOP;}
 if(c->behavior==5){assert(SRNOS_Delay(4)==SRNOS_OK);return c->steps>=3?SRNOS_STEP_STOP:SRNOS_STEP_CONTINUE;}
 return SRNOS_STEP_STOP;
}
static SRNOS_ThreadConfig config(context_t *c)
{return (SRNOS_ThreadConfig){.name="Task",.init=init,.step=step,.cleanup=cleanup,.argument=c,.stack_bytes=2048,.priority=24,.mode=SRNOS_TASK_ONCE};}
static void wait_entered(context_t *c)
{uint32_t start=SRNOS_TimeMs();while(!c->entered){assert(SRNOS_TimeMs()-start<2000);SRNOS_Delay(1);}}
static void wait_init(context_t *c)
{uint32_t start=SRNOS_TimeMs();while(!c->init){assert(SRNOS_TimeMs()-start<2000);SRNOS_Delay(1);}}
static uint32_t pool_used(void){SRNOS_KernelResources r;assert(SRNOS_KernelGetResources(&r)==SRNOS_OK);return r.used_bytes;}
static void *joiner(void *p){SRNOS_Thread h=*(SRNOS_Thread *)p;assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);return NULL;}
static _Atomic uint64_t sdk_token;
static atomic_bool sdk_finish;
static void sdk_entry(void *arg) {
 (void)arg;sdk_token=SRNOS_ThreadGetCurrent();
 while(!sdk_finish)SRNOS_Delay(1);
}
int main(void)
{
 test_runtime_init();assert(SRNOS_KernelThreadPrepare(false));assert(!SRNOS_KernelThreadPrepare(false));
 context_t c={0};SRNOS_ThreadConfig d=config(&c);SRNOS_Thread h=99;
 assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_CONTEXT && !h);
 test_kernel_state=osKernelRunning;
 assert(SRNOS_KernelThreadCreateConfigured(&d,&h)==SRNOS_OK);SRNOS_Delay(2);assert(!c.init);
 assert(SRNOS_ThreadRequestStop(h)==SRNOS_OK);assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);
 assert(!c.init && !c.steps && c.cleanup==1);assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c=(context_t){0};SRNOS_KernelThreadReleaseApplications();
 d.name="SRNOS_Bad";assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_INVALID);d.name="Task";
 d.priority=40;assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_INVALID);d.priority=24;
 d.mode=SRNOS_TASK_PERIODIC;d.period_ms=0;assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_INVALID);d.mode=SRNOS_TASK_ONCE;
 test_fail_create=true;assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_RESOURCE && pool_used()==0);
 char name[16]="OwnedName";d.name=name;
 assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);memset(name,'X',9);
 assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);SRNOS_ThreadInfo info;assert(SRNOS_ThreadGetInfo(h,&info)==SRNOS_OK && !strcmp(info.name,"OwnedName"));
 assert(c.init==1 && c.steps==1 && c.cleanup==1 && info.state==SRNOS_THREAD_EXITED);
 SRNOS_Thread old=h;assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK && pool_used()==0);assert(SRNOS_ThreadDelete(old,0)==SRNOS_STALE_HANDLE);
 c=(context_t){.behavior=1};d=config(&c);assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK && h!=old);
 assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);assert(SRNOS_ThreadGetInfo(h,&info)==SRNOS_OK && info.state==SRNOS_THREAD_FAILED && info.init_result==SRNOS_RESOURCE);
 assert(c.steps==0 && c.cleanup==1);assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c=(context_t){.behavior=6};d=config(&c);assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);wait_init(&c);
 assert(SRNOS_ThreadDelete(h,2000)==SRNOS_OK && !c.steps && c.cleanup==1);
 c=(context_t){.behavior=2};d=config(&c);assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);
 assert(SRNOS_ThreadGetInfo(h,&info)==SRNOS_OK && info.state==SRNOS_THREAD_FAILED);assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c=(context_t){0};d=config(&c);d.mode=SRNOS_TASK_EVENT;d.event_mask=1;d.event_timeout_ms=SRNOS_WAIT_FOREVER;
 assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);wait_init(&c);assert(SRNOS_ThreadNotify(h,2)==SRNOS_OK);SRNOS_Delay(2);assert(!c.steps);
 test_irq=16;test_irq_priority=4;assert(SRNOS_ThreadNotifyFromISR(h,1)==SRNOS_CONTEXT);
 test_irq_priority=5;assert(SRNOS_ThreadNotifyFromISR(h,1)==SRNOS_OK);test_irq=0;
 assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK && c.flags==1 && c.steps==1);assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c=(context_t){0};d.argument=&c;assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);wait_init(&c);
 assert(SRNOS_ThreadDelete(h,2000)==SRNOS_OK && !c.steps && c.cleanup==1);
 c=(context_t){.behavior=3};d=config(&c);assert(SRNOS_QueueCreate(1,sizeof(unsigned),&c.queue)==SRNOS_OK);
 assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);wait_entered(&c);SRNOS_Delay(2);
 assert(SRNOS_QueueDelete(c.queue)==SRNOS_BUSY);assert(SRNOS_ThreadDelete(h,2)==SRNOS_TIMEOUT);
 pthread_t reader;assert(!pthread_create(&reader,NULL,joiner,&h));SRNOS_Delay(2);unsigned value=123;
 assert(SRNOS_QueueSend(c.queue,&value,0)==SRNOS_OK);assert(!pthread_join(reader,NULL));
 assert(SRNOS_ThreadDelete(h,2000)==SRNOS_OK && c.cleanup==1);assert(SRNOS_QueueDelete(c.queue)==SRNOS_OK);
 c=(context_t){.behavior=4};d=config(&c);assert(SRNOS_MutexCreate(false,&c.mutex)==SRNOS_OK);
 assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);wait_entered(&c);assert(SRNOS_MutexDelete(c.mutex)==SRNOS_BUSY);
 assert(SRNOS_ThreadDelete(h,2000)==SRNOS_OK && c.cleanup==1);assert(SRNOS_MutexDelete(c.mutex)==SRNOS_OK);
 c=(context_t){.behavior=5};d=config(&c);d.mode=SRNOS_TASK_PERIODIC;d.period_ms=1;
 uint32_t start=SRNOS_TimeMs();assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);assert(SRNOS_ThreadJoin(h,2000)==SRNOS_OK);
 assert(c.steps==3 && SRNOS_TimeMs()-start>=12);assert(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 SRNOS_Mutex mutex;assert(SRNOS_MutexCreate(true,&mutex)==SRNOS_OK);assert(SRNOS_MutexLock(mutex,0)==SRNOS_OK);assert(SRNOS_MutexLock(mutex,0)==SRNOS_OK);
 assert(SRNOS_MutexDelete(mutex)==SRNOS_BUSY);assert(SRNOS_MutexUnlock(mutex)==SRNOS_OK);assert(SRNOS_MutexUnlock(mutex)==SRNOS_OK);assert(SRNOS_MutexDelete(mutex)==SRNOS_OK);
 assert(SRNOS_MutexLock(mutex,0)==SRNOS_STALE_HANDLE);
 SRNOS_Semaphore sem;assert(SRNOS_SemaphoreCreate(2,0,&sem)==SRNOS_OK);assert(SRNOS_SemaphoreTake(sem,0)==SRNOS_TIMEOUT);
 test_irq=16;assert(SRNOS_SemaphoreGiveFromISR(sem)==SRNOS_OK);assert(SRNOS_SemaphoreDelete(sem)==SRNOS_CONTEXT);test_irq=0;
 assert(SRNOS_SemaphoreTake(sem,0)==SRNOS_OK);assert(SRNOS_SemaphoreDelete(sem)==SRNOS_OK);
 SRNOS_Queue queue;assert(SRNOS_QueueCreate(2,sizeof(unsigned),&queue)==SRNOS_OK);
 test_irq=16;assert(SRNOS_QueueSendFromISR(queue,&value)==SRNOS_OK);unsigned output=0;assert(SRNOS_QueueReceiveFromISR(queue,&output)==SRNOS_OK && output==123);test_irq=0;
 assert(SRNOS_QueueDelete(queue)==SRNOS_OK);
 SRNOS_Event event;assert(SRNOS_EventCreate(&event)==SRNOS_OK);assert(SRNOS_EventSet(event,0x1000000)==SRNOS_INVALID);
 assert(SRNOS_EventSet(event,3)==SRNOS_OK);assert(SRNOS_EventWait(event,3,true,true,0,&output)==SRNOS_OK && output==3);
 assert(SRNOS_EventWait(event,1,false,false,1,&output)==SRNOS_TIMEOUT);assert(SRNOS_EventDelete(event)==SRNOS_OK);
 assert(SRNOS_QueueCreate(32000,1,&queue)==SRNOS_OK);d=config(&c);assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_RESOURCE);assert(SRNOS_QueueDelete(queue)==SRNOS_OK);
 SRNOS_Thread handles[8];context_t states[8]={0};char names[8][16];
 for(unsigned i=0;i<8;i++){d=config(&states[i]);snprintf(names[i],16,"Event%u",i);d.name=names[i];d.stack_bytes=1024;d.mode=SRNOS_TASK_EVENT;d.event_mask=1;d.event_timeout_ms=UINT32_MAX;assert(SRNOS_ThreadCreate(&d,&handles[i])==SRNOS_OK);}
 d.name="Overflow";assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_RESOURCE);
 for(unsigned i=0;i<8;i++)assert(SRNOS_ThreadDelete(handles[i],2000)==SRNOS_OK);
 assert(pool_used()==0);assert(SRNOS_DelayUntil(SRNOS_TimeMs())==SRNOS_DEADLINE_PASSED);assert(SRNOS_Delay(0)==SRNOS_OK);
 const osThreadAttr_t sdk_attr={.name="SRNOS_Test",.priority=24,.stack_size=1024};
 TaskHandle_t native=SRNOS_KernelThreadNew(sdk_entry,NULL,&sdk_attr);assert(native);
 while(!sdk_token)SRNOS_Delay(1);
 assert(SRNOS_ThreadRequestStop(sdk_token)==SRNOS_PROTECTED);
 assert(SRNOS_ThreadDelete(sdk_token,0)==SRNOS_PROTECTED);
 assert(SRNOS_ThreadJoin(sdk_token,0)==SRNOS_PROTECTED);
 assert(SRNOS_ThreadNotify(sdk_token,1)==SRNOS_PROTECTED);
 assert(SRNOS_ThreadGetInfo(sdk_token,&info)==SRNOS_OK && info.system_owned);
 sdk_finish=true;assert(!pthread_join(native->thread,NULL));
 assert(SRNOS_ThreadGetInfo(sdk_token,&info)==SRNOS_STALE_HANDLE);
 SRNOS_KernelResources resources;assert(SRNOS_KernelGetResources(&resources)==SRNOS_OK);
 assert(resources.allocation_failures && resources.creation_failures>=resources.allocation_failures);
 uint32_t skipped;
 assert(SRNOS_KernelNextRelease(0,1,0,&skipped)==1 && skipped==0);
 assert(SRNOS_KernelNextRelease(0,1,1,&skipped)==2 && skipped==1);
 assert(SRNOS_KernelNextRelease(100,10,1137,&skipped)==1140 && skipped==103);
 assert(SRNOS_KernelNextRelease(UINT32_MAX-2u,2,1,&skipped)==3 && skipped==2);
 test_manual_clock=true;test_cycles=(uint64_t)(UINT32_MAX-2u)*1000;
 assert(SRNOS_DelayUntil(1)==SRNOS_OK && SRNOS_TimeMs()==1);
 assert(SRNOS_DelayUntil(UINT32_MAX)==SRNOS_DEADLINE_PASSED);test_manual_clock=false;
 test_irq=16;assert(SRNOS_Delay(1)==SRNOS_CONTEXT);assert(SRNOS_ThreadCreate(&d,&h)==SRNOS_CONTEXT);test_irq=0;
 puts("PASS: real managed lifecycle, copied descriptors, init/step failure, cooperative stop, timeout, joins, generation reuse, pool exhaustion, sync ownership and ISR boundaries");
}
