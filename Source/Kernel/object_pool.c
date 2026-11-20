#include "thread_internal.h"
#include "SirinEmbedOS/Config.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "semphr.h"
#include "device_internal.h"
#include <string.h>
#define SRNOS_POOL_BLOCK_BYTES 256u
#define SRNOS_POOL_BLOCK_COUNT (SRNOS_APP_OBJECT_POOL_BYTES/SRNOS_POOL_BLOCK_BYTES)
static uint8_t *arena;
static uint16_t spans[SRNOS_POOL_BLOCK_COUNT]; /* 0 free, UINT16_MAX continuation. */
static StaticSemaphore_t gate_storage;
static SemaphoreHandle_t gate;
static uint32_t used,peak,failures,creation_failures;
SRNOS_Result SRNOS_KernelCreateFailed(void)
{taskENTER_CRITICAL();++creation_failures;taskEXIT_CRITICAL();return SRNOS_RESOURCE;}
_Static_assert(SRNOS_POOL_BLOCK_COUNT<UINT16_MAX,"pool bitmap capacity");
bool SRNOS_KernelTaskContext(void) { return !__get_IPSR() && osKernelGetState()==osKernelRunning; }
bool SRNOS_KernelISRContext(void)
{
    uint32_t exception=__get_IPSR();
    if(exception<16u) return false;
    uint32_t priority=NVIC_GetPriority((IRQn_Type)(exception-16u));
    return priority>=configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
}
bool SRNOS_KernelTimeoutValid(uint32_t ms) { return ms<=INT32_MAX || ms==SRNOS_WAIT_FOREVER; }
bool SRNOS_KernelPoolPrepare(void)
{
    if(arena) return false;
    arena=pvPortMalloc(SRNOS_APP_OBJECT_POOL_BYTES);
    gate=xSemaphoreCreateMutexStatic(&gate_storage);
    return arena && gate;
}
void *SRNOS_KernelPoolAllocate(size_t bytes)
{
    if(!arena || !bytes || bytes>SRNOS_APP_OBJECT_POOL_BYTES) {
        taskENTER_CRITICAL();++failures;taskEXIT_CRITICAL();return NULL;
    }
    unsigned need=(unsigned)((bytes+SRNOS_POOL_BLOCK_BYTES-1)/SRNOS_POOL_BLOCK_BYTES),run=0,start=0;
    if(osKernelGetState()==osKernelRunning) (void)xSemaphoreTake(gate,portMAX_DELAY);
    for(unsigned i=0;i<SRNOS_POOL_BLOCK_COUNT;i++) {
        if(spans[i]) {run=0;continue;}
        if(!run) start=i;
        if(++run==need) break;
    }
    void *result=NULL;
    if(run==need) {
        spans[start]=(uint16_t)need;
        for(unsigned i=1;i<need;i++) spans[start+i]=UINT16_MAX;
        used+=need*SRNOS_POOL_BLOCK_BYTES;if(used>peak)peak=used;result=arena+start*SRNOS_POOL_BLOCK_BYTES;
    } else {taskENTER_CRITICAL();++failures;taskEXIT_CRITICAL();}
    if(osKernelGetState()==osKernelRunning) (void)xSemaphoreGive(gate);
    if(result) memset(result,0,need*SRNOS_POOL_BLOCK_BYTES);
    return result;
}
void SRNOS_KernelPoolFree(void *memory)
{
    if(!memory) return;
    configASSERT(arena && (uint8_t *)memory>=arena && (uint8_t *)memory<arena+SRNOS_APP_OBJECT_POOL_BYTES);
    size_t offset=(uint8_t *)memory-arena;configASSERT(offset%SRNOS_POOL_BLOCK_BYTES==0);
    if(osKernelGetState()==osKernelRunning) (void)xSemaphoreTake(gate,portMAX_DELAY);
    unsigned start=(unsigned)(offset/SRNOS_POOL_BLOCK_BYTES),count=spans[start];
    configASSERT(count && count!=UINT16_MAX && start+count<=SRNOS_POOL_BLOCK_COUNT);
    for(unsigned i=0;i<count;i++)spans[start+i]=0;
    used-=count*SRNOS_POOL_BLOCK_BYTES;
    if(osKernelGetState()==osKernelRunning) (void)xSemaphoreGive(gate);
}
void SRNOS_KernelPoolStatistics(SRNOS_KernelResources *s)
{
    (void)xSemaphoreTake(gate,portMAX_DELAY);
    s->capacity_bytes=SRNOS_APP_OBJECT_POOL_BYTES;s->used_bytes=used;
    s->peak_bytes=peak;taskENTER_CRITICAL();s->allocation_failures=failures;s->creation_failures=creation_failures;taskEXIT_CRITICAL();
    (void)xSemaphoreGive(gate);
}
uint32_t SRNOS_TimeMs(void) {return __get_IPSR()?(uint32_t)xTaskGetTickCountFromISR():(uint32_t)xTaskGetTickCount();}
uint64_t SRNOS_TimeUs(void) {return SRNOS_TimeGetUptimeUs();}
