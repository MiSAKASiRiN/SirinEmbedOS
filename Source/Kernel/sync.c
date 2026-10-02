#include "SirinEmbedOS/Kernel/Sync.h"
#include "thread_internal.h"
#include "SirinEmbedOS/Config.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"
#include <string.h>
enum { SRNOS_SYNC_KIND_MUTEX=1, SRNOS_SYNC_KIND_SEMAPHORE, SRNOS_SYNC_KIND_QUEUE, SRNOS_SYNC_KIND_EVENT };
#define SRNOS_SYNC_CAPACITY (SRNOS_APP_MUTEX_LIMIT+SRNOS_APP_SEMAPHORE_LIMIT+SRNOS_APP_QUEUE_LIMIT+SRNOS_APP_EVENT_LIMIT)
_Static_assert(SRNOS_SYNC_CAPACITY>0 && SRNOS_SYNC_CAPACITY<65536,"sync object capacity");
typedef struct {
    void *native,*storage;
    uint32_t generation,refs,depth;
    TaskHandle_t owner;
    uint8_t kind;
    bool recursive,used;
} object_t;
static object_t objects[SRNOS_SYNC_CAPACITY];
static unsigned count[5];
static const unsigned limits[]={0,SRNOS_APP_MUTEX_LIMIT,SRNOS_APP_SEMAPHORE_LIMIT,SRNOS_APP_QUEUE_LIMIT,SRNOS_APP_EVENT_LIMIT};
static uint64_t token(object_t *o) {return ((uint64_t)o->generation<<32)|(uint32_t)(o-objects+1);}
static object_t *find(uint64_t h,unsigned kind)
{
    uint32_t n=(uint32_t)h;if(!n || n>SRNOS_SYNC_CAPACITY)return NULL;object_t *o=&objects[n-1];
    return o->used && o->native && o->kind==kind && o->generation==(uint32_t)(h>>32)?o:NULL;
}
static object_t *acquire(uint64_t h,unsigned kind,bool isr)
{
    UBaseType_t m=0;if(isr)m=taskENTER_CRITICAL_FROM_ISR();else taskENTER_CRITICAL();
    object_t *o=find(h,kind);if(o)++o->refs;
    if(isr)taskEXIT_CRITICAL_FROM_ISR(m);else taskEXIT_CRITICAL();return o;
}
static void release(object_t *o,bool isr)
{
    UBaseType_t m=0;if(isr)m=taskENTER_CRITICAL_FROM_ISR();else taskENTER_CRITICAL();--o->refs;
    if(isr)taskEXIT_CRITICAL_FROM_ISR(m);else taskEXIT_CRITICAL();
}
static object_t *reserve(unsigned kind)
{
    object_t *o=NULL;taskENTER_CRITICAL();
    if(count[kind]<limits[kind])for(unsigned i=0;i<SRNOS_SYNC_CAPACITY;i++)if(!objects[i].used){o=&objects[i];break;}
    if(o){uint32_t generation=o->generation+1;if(!generation)generation=1;memset(o,0,sizeof(*o));
        o->used=true;o->kind=(uint8_t)kind;o->generation=generation;++count[kind];}
    taskEXIT_CRITICAL();return o;
}
static void abandon(object_t *o)
{taskENTER_CRITICAL();--count[o->kind];o->used=false;taskEXIT_CRITICAL();}
static SRNOS_Result create(unsigned kind,uint32_t a,uint32_t b,bool recursive,uint64_t *out)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!out)return SRNOS_INVALID;*out=0;
    if(kind==SRNOS_SYNC_KIND_SEMAPHORE && (!a || b>a))return SRNOS_INVALID;
    if(kind==SRNOS_SYNC_KIND_QUEUE && (!a || !b || a>SRNOS_APP_OBJECT_POOL_BYTES/b))return SRNOS_INVALID;
    size_t cb=kind==SRNOS_SYNC_KIND_EVENT?sizeof(StaticEventGroup_t):sizeof(StaticQueue_t);
    cb=(cb+7u)&~(size_t)7u;
    size_t bytes=cb+(kind==SRNOS_SYNC_KIND_QUEUE?(size_t)a*b:0);
    object_t *o=reserve(kind);if(!o)return SRNOS_KernelCreateFailed();
    o->storage=SRNOS_KernelPoolAllocate(bytes);
    if(!o->storage){abandon(o);return SRNOS_KernelCreateFailed();}
    o->recursive=recursive;
    switch(kind){
    case SRNOS_SYNC_KIND_MUTEX:o->native=recursive?xSemaphoreCreateRecursiveMutexStatic(o->storage):xSemaphoreCreateMutexStatic(o->storage);break;
    case SRNOS_SYNC_KIND_SEMAPHORE:o->native=xSemaphoreCreateCountingStatic(a,b,o->storage);break;
    case SRNOS_SYNC_KIND_QUEUE:o->native=xQueueCreateStatic(a,b,(uint8_t *)o->storage+cb,o->storage);break;
    default:o->native=xEventGroupCreateStatic(o->storage);break;
    }
    if(!o->native){SRNOS_KernelPoolFree(o->storage);abandon(o);return SRNOS_KernelCreateFailed();}
    *out=token(o);return SRNOS_OK;
}
static SRNOS_Result destroy(uint64_t h,unsigned kind)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    taskENTER_CRITICAL();object_t *o=find(h,kind);
    if(!o){taskEXIT_CRITICAL();return SRNOS_STALE_HANDLE;}
    if(o->refs || o->depth){taskEXIT_CRITICAL();return SRNOS_BUSY;}
    void *native=o->native,*storage=o->storage;o->native=NULL;taskEXIT_CRITICAL();
    if(kind==SRNOS_SYNC_KIND_EVENT)vEventGroupDelete(native);else vQueueDelete(native);
    SRNOS_KernelPoolFree(storage);abandon(o);return SRNOS_OK;
}
SRNOS_Result SRNOS_MutexCreate(bool recursive,SRNOS_Mutex *out){return create(SRNOS_SYNC_KIND_MUTEX,0,0,recursive,out);}
SRNOS_Result SRNOS_MutexDelete(SRNOS_Mutex h){return destroy(h,SRNOS_SYNC_KIND_MUTEX);}
SRNOS_Result SRNOS_MutexLock(SRNOS_Mutex h,uint32_t timeout)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_MUTEX,false);if(!o)return SRNOS_STALE_HANDLE;
    BaseType_t ok=o->recursive?xSemaphoreTakeRecursive(o->native,timeout):xSemaphoreTake(o->native,timeout);
    if(ok){taskENTER_CRITICAL();o->owner=xTaskGetCurrentTaskHandle();++o->depth;taskEXIT_CRITICAL();}
    release(o,false);return ok?SRNOS_OK:SRNOS_TIMEOUT;
}
SRNOS_Result SRNOS_MutexUnlock(SRNOS_Mutex h)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_MUTEX,false);if(!o)return SRNOS_STALE_HANDLE;
    /* Keep ownership bookkeeping and the native release atomic with respect
     * to the next owner being scheduled after a priority-inheriting unlock. */
    taskENTER_CRITICAL();BaseType_t ok=pdFALSE;
    if(o->owner==xTaskGetCurrentTaskHandle() && o->depth){
        --o->depth;if(!o->depth)o->owner=NULL;
        ok=o->recursive?xSemaphoreGiveRecursive(o->native):xSemaphoreGive(o->native);
    }
    taskEXIT_CRITICAL();release(o,false);return ok?SRNOS_OK:SRNOS_INVALID;
}
SRNOS_Result SRNOS_SemaphoreCreate(uint32_t max,uint32_t initial,SRNOS_Semaphore *out){return create(SRNOS_SYNC_KIND_SEMAPHORE,max,initial,false,out);}
SRNOS_Result SRNOS_SemaphoreDelete(SRNOS_Semaphore h){return destroy(h,SRNOS_SYNC_KIND_SEMAPHORE);}
SRNOS_Result SRNOS_SemaphoreTake(SRNOS_Semaphore h,uint32_t timeout)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_SEMAPHORE,false);if(!o)return SRNOS_STALE_HANDLE;
    BaseType_t ok=xSemaphoreTake(o->native,timeout);release(o,false);return ok?SRNOS_OK:SRNOS_TIMEOUT;
}
static SRNOS_Result give(SRNOS_Semaphore h,bool isr)
{
    if(isr?!SRNOS_KernelISRContext():!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_SEMAPHORE,isr);if(!o)return SRNOS_STALE_HANDLE;BaseType_t higher=pdFALSE;
    BaseType_t ok=isr?xSemaphoreGiveFromISR(o->native,&higher):xSemaphoreGive(o->native);
    release(o,isr);if(isr)portYIELD_FROM_ISR(higher);return ok?SRNOS_OK:SRNOS_RESOURCE;
}
SRNOS_Result SRNOS_SemaphoreGive(SRNOS_Semaphore h){return give(h,false);}
SRNOS_Result SRNOS_SemaphoreGiveFromISR(SRNOS_Semaphore h){return give(h,true);}
SRNOS_Result SRNOS_QueueCreate(uint32_t n,uint32_t bytes,SRNOS_Queue *out){return create(SRNOS_SYNC_KIND_QUEUE,n,bytes,false,out);}
SRNOS_Result SRNOS_QueueDelete(SRNOS_Queue h){return destroy(h,SRNOS_SYNC_KIND_QUEUE);}
static SRNOS_Result queue_io(SRNOS_Queue h,void *item,uint32_t timeout,bool send,bool isr)
{
    if(isr?!SRNOS_KernelISRContext():!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    if(!item || !SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_QUEUE,isr);if(!o)return SRNOS_STALE_HANDLE;BaseType_t higher=pdFALSE,ok;
    if(isr)ok=send?xQueueSendFromISR(o->native,item,&higher):xQueueReceiveFromISR(o->native,item,&higher);
    else ok=send?xQueueSend(o->native,item,timeout):xQueueReceive(o->native,item,timeout);
    release(o,isr);if(isr)portYIELD_FROM_ISR(higher);return ok?SRNOS_OK:SRNOS_TIMEOUT;
}
SRNOS_Result SRNOS_QueueSend(SRNOS_Queue h,const void *p,uint32_t t){return queue_io(h,(void *)p,t,true,false);}
SRNOS_Result SRNOS_QueueReceive(SRNOS_Queue h,void *p,uint32_t t){return queue_io(h,p,t,false,false);}
SRNOS_Result SRNOS_QueueSendFromISR(SRNOS_Queue h,const void *p){return queue_io(h,(void *)p,0,true,true);}
SRNOS_Result SRNOS_QueueReceiveFromISR(SRNOS_Queue h,void *p){return queue_io(h,p,0,false,true);}
SRNOS_Result SRNOS_EventCreate(SRNOS_Event *out){return create(SRNOS_SYNC_KIND_EVENT,0,0,false,out);}
SRNOS_Result SRNOS_EventDelete(SRNOS_Event h){return destroy(h,SRNOS_SYNC_KIND_EVENT);}
static SRNOS_Result event_bits(SRNOS_Event h,uint32_t bits,bool set)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;if(!bits || bits&0xff000000u)return SRNOS_INVALID;
    object_t *o=acquire(h,SRNOS_SYNC_KIND_EVENT,false);if(!o)return SRNOS_STALE_HANDLE;
    if(set)xEventGroupSetBits(o->native,bits);else xEventGroupClearBits(o->native,bits);
    release(o,false);return SRNOS_OK;
}
SRNOS_Result SRNOS_EventSet(SRNOS_Event h,uint32_t bits){return event_bits(h,bits,true);}
SRNOS_Result SRNOS_EventClear(SRNOS_Event h,uint32_t bits){return event_bits(h,bits,false);}
SRNOS_Result SRNOS_EventWait(SRNOS_Event h,uint32_t bits,bool all,bool clear,uint32_t timeout,uint32_t *out)
{
    if(!SRNOS_KernelTaskContext())return SRNOS_CONTEXT;
    if(!out || !bits || bits&0xff000000u || !SRNOS_KernelTimeoutValid(timeout))return SRNOS_INVALID;
    *out=0;object_t *o=acquire(h,SRNOS_SYNC_KIND_EVENT,false);if(!o)return SRNOS_STALE_HANDLE;
    uint32_t found=xEventGroupWaitBits(o->native,bits,clear?pdTRUE:pdFALSE,all?pdTRUE:pdFALSE,timeout)&bits;
    release(o,false);*out=found;return (all?found==bits:found!=0)?SRNOS_OK:SRNOS_TIMEOUT;
}
