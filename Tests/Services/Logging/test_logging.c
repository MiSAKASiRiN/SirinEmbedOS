#include "Services/Logging/log_internal.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static osKernelState_t kernel_state=osKernelInactive;
static bool in_isr,pool_disabled,force_queue_full;
static uint32_t irq_mask,alloc_calls,put_calls,free_calls,usb_calls,uart_calls,boot_calls;
static uint32_t queued;
static SRNOS_LogMsg blocks[3];static bool used[3];static uintptr_t messages[2];
struct SRNOS_SerialChannel { int unused; };static SRNOS_SerialChannel uart;
uint32_t HAL_GetTick(void) { return 123; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask=1; }
void __set_PRIMASK(uint32_t value) { irq_mask=value; }
BaseType_t xPortIsInsideInterrupt(void) { return in_isr?pdTRUE:pdFALSE; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
const char *pcTaskGetName(TaskHandle_t task) { (void)task;return "AppTask"; }
osKernelState_t osKernelGetState(void) { return kernel_state; }
void SRNOS_LogCaptureBoot(const void *data,size_t length)
{ assert(data && length && irq_mask==0);++boot_calls; }
SRNOS_SerialStatus SRNOS_SerialClaim(SRNOS_SerialChannel *channel,const void *owner)
{ assert(channel==&uart && owner);return SRNOS_SERIAL_OK; }
SRNOS_SerialStatus SRNOS_SerialWrite(SRNOS_SerialChannel *channel,const void *data,size_t size,uint32_t timeout)
{ assert(channel==&uart && data && size && !timeout);++uart_calls;return SRNOS_SERIAL_OK; }
static bool usb_submit(void *user,const uint8_t *data,size_t size)
{ (void)user;assert(data && size);++usb_calls;return true; }
void *osMemoryPoolAlloc(osMemoryPoolId_t pool,uint32_t timeout)
{
    assert(pool && !timeout);++alloc_calls;
    if(pool_disabled)return NULL;
    for(unsigned i=0;i<3;i++)if(!used[i]) {used[i]=true;return &blocks[i];}
    return NULL;
}
osStatus_t osMemoryPoolFree(osMemoryPoolId_t pool,void *ptr)
{
    assert(pool);++free_calls;
    for(unsigned i=0;i<3;i++)if(ptr==&blocks[i]){assert(used[i]);used[i]=false;return osOK;}
    assert(false);return osError;
}
osStatus_t osMessageQueuePut(osMessageQueueId_t queue,const void *message,uint8_t priority,uint32_t timeout)
{
    assert(queue && !priority && !timeout);++put_calls;
    if(force_queue_full || queued==2)return osErrorResource;
    messages[queued++]=*(const uintptr_t *)message;return osOK;
}
osStatus_t osMessageQueueGet(osMessageQueueId_t queue,void *message,uint8_t *priority,uint32_t timeout)
{
    (void)priority;assert(queue && !timeout);
    if(!queued)return osErrorResource;
    *(uintptr_t *)message=messages[0];messages[0]=messages[1];--queued;return osOK;
}
static void clear_queue(void)
{
    uintptr_t message;
    while(osMessageQueueGet((void *)1,&message,NULL,0)==osOK)assert(osMemoryPoolFree((void *)2,(void *)message)==osOK);
}
static void no_runtime_io(uint32_t boot_before)
{ assert(!usb_calls && !uart_calls && boot_calls==boot_before && irq_mask==0); }
int main(void)
{
    SRNOS_LogInit();
    SRNOS_LogSetBackendMask(SRNOS_LOG_BACKEND_USB_CDC|SRNOS_LOG_BACKEND_UART);
    const SRNOS_LogUSBTransport transport={.submit=usb_submit};
    SRNOS_LogRegisterUsbTransport(&transport);assert(SRNOS_LogBindUart(&uart));
    assert(SRNOS_LogPrintf(SRNOS_LOG_INFO,NULL,"before scheduler %u",1u)==SRNOS_LOG_FULL);
    assert(boot_calls==1 && !usb_calls && !uart_calls && !alloc_calls && !put_calls);
    kernel_state=osKernelRunning;uint32_t boot_before=boot_calls;
    assert(SRNOS_LOGI("not configured")==SRNOS_LOG_FULL);
    no_runtime_io(boot_before);
    SRNOS_LogBindQueue((void *)1);SRNOS_LogBindPool((void *)2);
    pool_disabled=true;
    assert(SRNOS_LogPrintf(SRNOS_LOG_WARN,NULL,"pool unavailable")==SRNOS_LOG_NO_MEMORY);
    assert(alloc_calls==2 && !put_calls);no_runtime_io(boot_before);
    pool_disabled=false;
    for(unsigned i=0;i<3;i++)assert(SRNOS_LogPrintf(SRNOS_LOG_INFO,NULL,"record %u",i)==SRNOS_LOG_OK);
    assert(queued==2 && free_calls==1); /* drop exactly one oldest queued record */
    assert(strcmp(((SRNOS_LogMsg *)messages[0])->body,"record 1")==0);
    assert(strcmp(((SRNOS_LogMsg *)messages[1])->body,"record 2")==0);no_runtime_io(boot_before);
    pool_disabled=true;
    assert(SRNOS_LogPrintf(SRNOS_LOG_ERROR,NULL,"still exhausted")==SRNOS_LOG_NO_MEMORY);
    assert(queued==1);no_runtime_io(boot_before);pool_disabled=false;clear_queue();
    force_queue_full=true;
    assert(SRNOS_LogPrintf(SRNOS_LOG_WARN,NULL,"concurrent queue refill")==SRNOS_LOG_FULL);
    assert(!used[0] && !used[1] && !used[2]);no_runtime_io(boot_before);force_queue_full=false;
    uint32_t old_alloc=alloc_calls;in_isr=true;
    assert(SRNOS_LogPrintf(SRNOS_LOG_INFO,NULL,"ISR")==SRNOS_LOG_ISR_FORBIDDEN);
    in_isr=false;
    assert(SRNOS_LogPrintf(SRNOS_LOG_INFO,NULL,NULL)==SRNOS_LOG_INVALID);
    assert(SRNOS_LogPrintf((SRNOS_LogLevel)99,NULL,"bad level")==SRNOS_LOG_INVALID);
    assert(old_alloc==alloc_calls);no_runtime_io(boot_before);
    kernel_state=osKernelLocked;
    assert(SRNOS_LOGI_SRC("App", "scheduler locked")==SRNOS_LOG_OK);
    kernel_state=osKernelRunning;no_runtime_io(boot_before);
    /* Only the worker's private output boundary invokes configured sinks. */
    uintptr_t message;assert(osMessageQueueGet((void *)1,&message,NULL,0)==osOK);
    char line[SRNOS_LOG_LINE_MAX];bool truncated;
    size_t length=SRNOS_LogFormatLine(line,sizeof(line),(SRNOS_LogMsg *)message,&truncated);
    assert(!truncated && length && SRNOS_LogOutputLineEx(line,length,SRNOS_LOG_INFO,false)==SRNOS_LOG_OUTPUT_OK);
    assert(usb_calls==1 && uart_calls==1 && boot_calls==boot_before);
    assert(osMemoryPoolFree((void *)2,(void *)message)==osOK);
    SRNOS_LogStats stats;SRNOS_LogGetStats(&stats);
    assert(stats.dropped_queue>=4 && stats.direct_printed==0 && stats.uart_writes==1);
    puts("PASS: public SRNOS results, startup capture, ISR rejection, bounded replacement, exhausted/unbound queues never call I/O");
    return 0;
}
