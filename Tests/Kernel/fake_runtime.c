#define _POSIX_C_SOURCE 200809L
#include "fake_runtime.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
static pthread_mutex_t gate;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static _Thread_local unsigned critical;
static _Thread_local TaskHandle_t self;
_Thread_local uint32_t test_irq,test_irq_priority=5;
osKernelState_t test_kernel_state=osKernelReady;
bool test_fail_create,test_manual_clock;
uint64_t test_cycles;
uint32_t test_identity_generation=1,test_yields;
static uint64_t epoch;
static uint64_t clock_us(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000+(uint64_t)ts.tv_nsec/1000;}
void test_runtime_init(void)
{
 pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);
 pthread_mutex_init(&gate,&a);pthread_mutexattr_destroy(&a);epoch=clock_us();
}
static void leave_if_deleted(void)
{if(self && self->deleted){pthread_mutex_unlock(&gate);pthread_exit(NULL);}}
void test_enter(void){if(!critical++)pthread_mutex_lock(&gate);}
void test_exit(void)
{
 assert(critical);if(--critical)return;
 while(self && self->suspended && !self->deleted)pthread_cond_wait(&changed,&gate);
 leave_if_deleted();pthread_mutex_unlock(&gate);
}
void test_yield(void){++test_yields;sched_yield();}
static bool wait_until(uint64_t deadline)
{
 leave_if_deleted();
 if(deadline==UINT64_MAX){pthread_cond_wait(&changed,&gate);leave_if_deleted();return true;}
 uint64_t now=clock_us();if(now>=deadline)return false;
 struct timespec abs;clock_gettime(CLOCK_REALTIME,&abs);
 uint64_t ns=(uint64_t)abs.tv_nsec+(deadline-now)*1000;
 abs.tv_sec+=(time_t)(ns/1000000000);abs.tv_nsec=(long)(ns%1000000000);
 int r=pthread_cond_timedwait(&changed,&gate,&abs);leave_if_deleted();return r!=ETIMEDOUT;
}
static uint64_t end_time(TickType_t ticks){return ticks==UINT32_MAX?UINT64_MAX:clock_us()+(uint64_t)ticks*1000;}
void *pvPortMalloc(size_t n){return calloc(1,n);}
void vPortFree(void *p){free(p);}
size_t xPortGetFreeHeapSize(void){return 100000;}
size_t xPortGetMinimumEverFreeHeapSize(void){return 90000;}
uint64_t SRNOS_TimeGetUptimeCycles64(void){return test_manual_clock?test_cycles:clock_us()-epoch;}
uint64_t SRNOS_TimeGetUptimeUs(void){return SRNOS_TimeGetUptimeCycles64();}
uint32_t SRNOS_TimeGetCounter(void){return (uint32_t)SRNOS_TimeGetUptimeCycles64();}
uint32_t SRNOS_TimeGetCpuHz(void){return 1000000;}
bool SRNOS_RuntimeCurrentIdentity(uint8_t *slot,uint32_t *gen){*slot=1;*gen=test_identity_generation;return true;}
TickType_t xTaskGetTickCount(void){return (TickType_t)(SRNOS_TimeGetUptimeCycles64()/1000);}
TickType_t xTaskGetTickCountFromISR(void){return xTaskGetTickCount();}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return self;}
osKernelState_t osKernelGetState(void){return test_kernel_state;}
static void *run(void *arg){self=arg;self->entry(self->arg);return NULL;}
osThreadId_t osThreadNew(osThreadFunc_t entry,void *arg,const osThreadAttr_t *attr)
{
 if(test_fail_create){test_fail_create=false;return NULL;}
 TaskHandle_t t=attr->cb_mem?attr->cb_mem:calloc(1,sizeof(*t));assert(t);
 memset(t,0,sizeof(*t));t->entry=entry;t->arg=arg;strncpy(t->name,attr->name,15);
 assert(!pthread_create(&t->thread,NULL,run,t));return t;
}
BaseType_t xTaskNotifyIndexed(TaskHandle_t t,unsigned index,uint32_t bits,eNotifyAction action)
{(void)action;assert(t && index<2);pthread_mutex_lock(&gate);t->notify[index]|=bits;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);return pdTRUE;}
BaseType_t xTaskNotifyIndexedFromISR(TaskHandle_t t,unsigned i,uint32_t b,eNotifyAction a,BaseType_t *higher)
{*higher=pdTRUE;return xTaskNotifyIndexed(t,i,b,a);}
BaseType_t xTaskNotifyWaitIndexed(unsigned i,uint32_t enter,uint32_t exit,uint32_t *bits,TickType_t timeout)
{
 assert(self && i<2);pthread_mutex_lock(&gate);uint64_t until=end_time(timeout);
 if(!self->notify[i])self->notify[i]&=~enter;
 while(!self->notify[i])if(!wait_until(until)){*bits=0;pthread_mutex_unlock(&gate);return pdFALSE;}
 *bits=self->notify[i];self->notify[i]&=~exit;pthread_mutex_unlock(&gate);return pdTRUE;
}
void vTaskDelay(TickType_t ticks)
{if(test_manual_clock){test_cycles+=(uint64_t)ticks*1000;return;}pthread_mutex_lock(&gate);uint64_t until=end_time(ticks);while(wait_until(until)){}pthread_mutex_unlock(&gate);}
void vTaskSuspend(TaskHandle_t t)
{if(!t)t=self;assert(t);pthread_mutex_lock(&gate);t->suspended=true;pthread_mutex_unlock(&gate);if(!critical){test_enter();test_exit();}}
void vTaskDelete(TaskHandle_t t)
{
 if(!t || t==self)pthread_exit(NULL);
 pthread_mutex_lock(&gate);t->deleted=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);assert(!pthread_join(t->thread,NULL));
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){memset(s,0,sizeof(*s));s->kind=1;s->max=s->count=1;return s;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *s){xSemaphoreCreateMutexStatic(s);s->kind=2;return s;}
SemaphoreHandle_t xSemaphoreCreateCountingStatic(unsigned max,unsigned n,StaticSemaphore_t *s){memset(s,0,sizeof(*s));s->kind=3;s->max=max;s->count=n;return s;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t ticks)
{
 pthread_mutex_lock(&gate);uint64_t until=end_time(ticks);
 if(s->kind==2 && s->depth && s->owner==self){++s->depth;pthread_mutex_unlock(&gate);return pdTRUE;}
 while(!s->count)if(!wait_until(until)){pthread_mutex_unlock(&gate);return pdFALSE;}
 --s->count;if(s->kind<3){s->owner=self;s->depth=1;}pthread_mutex_unlock(&gate);return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s,TickType_t t){return xSemaphoreTake(s,t);}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
 pthread_mutex_lock(&gate);BaseType_t ok=pdFALSE;
 if(s->kind<3){if(s->depth && s->owner==self){if(!--s->depth){s->owner=NULL;s->count=1;}ok=pdTRUE;}}
 else if(s->count<s->max){++s->count;ok=pdTRUE;}
 pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);return ok;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s){return xSemaphoreGive(s);}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *h){*h=pdTRUE;return xSemaphoreGive(s);}
QueueHandle_t xQueueCreateStatic(unsigned n,unsigned size,uint8_t *bytes,StaticQueue_t *q)
{memset(q,0,sizeof(*q));q->kind=4;q->max=n;q->size=size;q->bytes=bytes;return q;}
static BaseType_t queue_io(QueueHandle_t q,void *p,TickType_t ticks,bool send)
{
 pthread_mutex_lock(&gate);uint64_t until=end_time(ticks);
 while(send?q->count==q->max:!q->count)if(!wait_until(until)){pthread_mutex_unlock(&gate);return pdFALSE;}
 if(send){memcpy(q->bytes+q->tail*q->size,p,q->size);q->tail=(q->tail+1)%q->max;++q->count;}
 else {memcpy(p,q->bytes+q->head*q->size,q->size);q->head=(q->head+1)%q->max;--q->count;}
 pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t q,const void *p,TickType_t t){return queue_io(q,(void *)p,t,true);}
BaseType_t xQueueReceive(QueueHandle_t q,void *p,TickType_t t){return queue_io(q,p,t,false);}
BaseType_t xQueueSendFromISR(QueueHandle_t q,const void *p,BaseType_t *h){*h=pdTRUE;return xQueueSend(q,p,0);}
BaseType_t xQueueReceiveFromISR(QueueHandle_t q,void *p,BaseType_t *h){*h=pdTRUE;return xQueueReceive(q,p,0);}
void vQueueDelete(QueueHandle_t q){(void)q;}
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *e){e->bits=0;return e;}
uint32_t xEventGroupSetBits(EventGroupHandle_t e,uint32_t bits)
{pthread_mutex_lock(&gate);e->bits|=bits;uint32_t value=e->bits;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);return value;}
uint32_t xEventGroupClearBits(EventGroupHandle_t e,uint32_t bits)
{pthread_mutex_lock(&gate);uint32_t value=e->bits;e->bits&=~bits;pthread_mutex_unlock(&gate);return value;}
uint32_t xEventGroupWaitBits(EventGroupHandle_t e,uint32_t bits,BaseType_t clear,BaseType_t all,TickType_t ticks)
{
 pthread_mutex_lock(&gate);uint64_t until=end_time(ticks);
 while(all?(e->bits&bits)!=bits:!(e->bits&bits))if(!wait_until(until)){uint32_t v=e->bits;pthread_mutex_unlock(&gate);return v;}
 uint32_t v=e->bits;if(clear)e->bits&=~bits;pthread_mutex_unlock(&gate);return v;
}
void vEventGroupDelete(EventGroupHandle_t e){(void)e;}
