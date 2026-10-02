#include "thread_internal.h"
/* Fixed I2C4: periodic LIS/SPA traffic and bounded background storage traffic. */
#include "IO/I2C/i2c_internal.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "IO/i2c_board.h"
#include <string.h>

_Static_assert(SRNOS_I2C_DMA_STAGING_SIZE >= SRNOS_I2C_MAX_TRANSFER_BYTES, "I2C staging too small");
enum { SRNOS_I2C_SLOT_FREE, SRNOS_I2C_SLOT_RESERVED, SRNOS_I2C_SLOT_QUEUED, SRNOS_I2C_SLOT_ACTIVE, SRNOS_I2C_SLOT_DONE };
typedef enum { SRNOS_I2C_OP_TX, SRNOS_I2C_OP_RX, SRNOS_I2C_OP_MEM_TX, SRNOS_I2C_OP_MEM_RX, SRNOS_I2C_OP_PROBE } SRNOS_I2COperation;
typedef struct {
    unsigned state;
    SRNOS_I2COperation operation;
    SRNOS_I2CDevice *device;
    SRNOS_I2CCallback callback;
    void *context;
    osThreadId_t waiter;
    osSemaphoreId_t done;
    uint32_t order, queue_us, cancel_reason;
    uint64_t enqueued, started;
    bool cancel;
    uint16_t address, address_size;
    size_t length;
    uint8_t tx[SRNOS_I2C_MAX_TRANSFER_BYTES];
    uint8_t *rx;
    SRNOS_I2CStatus result;
} SRNOS_I2CRequest;
struct SRNOS_I2CBus {
    I2C_HandleTypeDef *hal;
    SRNOS_I2CRequest slots[4]; /* LIS, SPA, background, background. */
    SRNOS_I2CRequest *active;
    SRNOS_I2CDevice *callback_device;
    volatile unsigned completion;
    volatile bool accepting_irq, recovering, abort_done;
    bool quarantined;
    unsigned periodic_run;
    uint32_t order, high_water;
    SRNOS_I2CClassStatistics stats[2];
};
static struct SRNOS_I2CBus bus;
static osThreadId_t worker;
static uint8_t s_i2c_tx_stage[SRNOS_I2C_DMA_STAGING_SIZE] SRNOS_I2C_DMA_SEC;
static uint8_t s_i2c_rx_stage[SRNOS_I2C_DMA_STAGING_SIZE] SRNOS_I2C_DMA_SEC;
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgI2C SRNOS_DebugDbgI2C[SRNOS_I2C_BUS_MAX];
#define SRNOS_I2C_DBG SRNOS_DebugDbgI2C[0]
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static void wake(void) { if(worker) (void)osThreadFlagsSet(worker,1U); }
static uint32_t ticks(uint32_t ms) {
    return ms==SRNOS_WAIT_FOREVER?osWaitForever:(uint32_t)(((uint64_t)ms*osKernelGetTickFreq()+999U)/1000U);
}
static bool timeout_valid(uint32_t ms) { return ms<=INT32_MAX || ms==SRNOS_WAIT_FOREVER; }
static bool task_context(void) {
    return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI() && worker && osKernelGetState()==osKernelRunning;
}
static bool valid(const SRNOS_I2CDevice *dev) { return dev && dev->bus==&bus && dev->addr_7bit<=0x7FU; }
static unsigned device_slot(uint16_t address) { return address==0x1EU?0U:address==0x76U?1U:2U; }
static unsigned category(const SRNOS_I2CRequest *r) { return device_slot(r->device->addr_7bit)<2?0:1; }
static unsigned queued(void) {
    unsigned n=0; for(unsigned i=0;i<4;i++) if(bus.slots[i].state==SRNOS_I2C_SLOT_QUEUED) ++n; return n;
}
static SRNOS_I2CRequest *find_device(const SRNOS_I2CDevice *dev) {
    for(unsigned i=0;i<4;i++) if(bus.slots[i].state!=SRNOS_I2C_SLOT_FREE && bus.slots[i].device==dev) return &bus.slots[i];
    return NULL;
}
static SRNOS_I2CRequest *oldest(unsigned first,unsigned last) {
    SRNOS_I2CRequest *best=NULL;
    for(unsigned i=first;i<last;i++) {
        SRNOS_I2CRequest *r=&bus.slots[i];
        if(r->state==SRNOS_I2C_SLOT_QUEUED && (!best || (int32_t)(r->order-best->order)<0)) best=r;
    }
    return best;
}
static void finish(SRNOS_I2CRequest *r,SRNOS_I2CStatus result) {
    uint32_t m=enter();
    SRNOS_I2CDevice *dev=r->device;
    SRNOS_I2CCallback cb=r->callback; void *ctx=r->context;
    bool waiting=r->waiter!=NULL;
    SRNOS_I2CClassStatistics *stats=&bus.stats[category(r)];
    if(result==SRNOS_I2C_OK) { ++stats->completed; ++SRNOS_I2C_DBG.xfer_done; }
    else if(result==SRNOS_I2C_CANCELLED) ++stats->cancelled;
    else { ++stats->errors; ++SRNOS_I2C_DBG.xfer_error; }
    if(result==SRNOS_I2C_TIMEOUT) { ++stats->timeouts; ++SRNOS_I2C_DBG.xfer_timeout; }
    r->result=result;
    if(bus.active==r) {
        bus.active=NULL;
        bus.completion=0;
        SRNOS_I2C_DBG.state=0; SRNOS_I2C_DBG.current_dev=0; SRNOS_I2C_DBG.lock_held=0; SRNOS_I2C_DBG.lock_owner=0;
    }
    dev->completed_status=result;
    dev->pending=0;
    r->state=waiting?SRNOS_I2C_SLOT_DONE:SRNOS_I2C_SLOT_FREE;
    bus.callback_device=cb?dev:NULL;
    SRNOS_I2C_DBG.h.last_status=result; SRNOS_I2C_DBG.h.last_tick=HAL_GetTick(); SRNOS_I2C_DBG.cb_status=result;
    leave(m);
    if(cb) { ++SRNOS_I2C_DBG.cb_queued; cb(dev,ctx,result); }
    m=enter(); bus.callback_device=NULL; leave(m);
    /* DONE slot cannot be reused until its waiter consumes this token. */
    if(waiting) (void)osSemaphoreRelease(r->done);
}
static bool abort_active(void) {
    bus.accepting_irq=false; bus.recovering=true; bus.abort_done=false;
    ++SRNOS_I2C_DBG.recover_count;
    HAL_StatusTypeDef status=HAL_I2C_Master_Abort_IT(bus.hal,(uint16_t)(bus.active->device->addr_7bit<<1));
    uint32_t start=HAL_GetTick();
    while(status==HAL_OK && !bus.abort_done && HAL_GetTick()-start<5U) osDelay(1U);
    bool stopped=bus.abort_done && SRNOS_BoardI2CQuiescent(bus.hal);
    bus.quarantined=!stopped; bus.recovering=false; bus.completion=0;
    if(!stopped) ++SRNOS_I2C_DBG.recover_error;
    return stopped;
}
static void start_request(SRNOS_I2CRequest *r) {
    r->started=SRNOS_TimeGetUptimeUs();
    SRNOS_I2CClassStatistics *stats=&bus.stats[category(r)];
    uint32_t delay=(uint32_t)(r->started-r->enqueued);
    if(delay>stats->max_queue_us) stats->max_queue_us=delay;
    SRNOS_I2C_DBG.state=1; SRNOS_I2C_DBG.lock_held=1; SRNOS_I2C_DBG.lock_owner=(uintptr_t)worker;
    SRNOS_I2C_DBG.current_dev=(uintptr_t)r->device; SRNOS_I2C_DBG.last_dev=(uintptr_t)r->device;
    SRNOS_I2C_DBG.last_len=(uint32_t)r->length; SRNOS_I2C_DBG.mem_addr=r->address; SRNOS_I2C_DBG.mem_addr_size=r->address_size;
    SRNOS_I2C_DBG.xfer_type=(uint8_t)r->operation; SRNOS_I2C_DBG.rx_copy_back=r->rx!=NULL; SRNOS_I2C_DBG.direct_xfer=0;
    ++SRNOS_I2C_DBG.xfer_start;
    if(r->operation==SRNOS_I2C_OP_PROBE) {
        HAL_StatusTypeDef st=HAL_I2C_IsDeviceReady(bus.hal,(uint16_t)(r->device->addr_7bit<<1),1U,1U);
        bus.completion=st==HAL_OK?1:2;
        return;
    }
    if(!r->rx) (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(s_i2c_tx_stage,sizeof(s_i2c_tx_stage)),SRNOS_Memory_Bytes(r->tx,r->length));
    bus.completion=0; bus.accepting_irq=true;
    uint16_t address=(uint16_t)(r->device->addr_7bit<<1);
    HAL_StatusTypeDef st;
    switch(r->operation) {
    case SRNOS_I2C_OP_TX: st=HAL_I2C_Master_Transmit_DMA(bus.hal,address,s_i2c_tx_stage,(uint16_t)r->length); break;
    case SRNOS_I2C_OP_RX: st=HAL_I2C_Master_Receive_DMA(bus.hal,address,s_i2c_rx_stage,(uint16_t)r->length); break;
    case SRNOS_I2C_OP_MEM_TX: st=HAL_I2C_Mem_Write_DMA(bus.hal,address,r->address,r->address_size,s_i2c_tx_stage,(uint16_t)r->length); break;
    default: st=HAL_I2C_Mem_Read_DMA(bus.hal,address,r->address,r->address_size,s_i2c_rx_stage,(uint16_t)r->length); break;
    }
    if(st!=HAL_OK) { bus.accepting_irq=false; bus.completion=2; }
}
/* Only the dispatcher owns HAL and DMA staging; ISR merely publishes an event.
 * One bounded pass is also exercised by host tests. */
static void service_once(void) {
    SRNOS_I2CRequest *r=bus.active;
    if(r) {
        unsigned completion=bus.completion;
        bool expired=SRNOS_TimeGetUptimeUs()-r->started>=10000U;
        if(completion || r->cancel || expired) {
            SRNOS_I2CStatus result=r->cancel?(r->cancel_reason==SRNOS_I2C_RECOVER_REASON_TIMEOUT?SRNOS_I2C_TIMEOUT:SRNOS_I2C_CANCELLED):
                completion==1?SRNOS_I2C_OK:completion==2?SRNOS_I2C_ERROR:SRNOS_I2C_TIMEOUT;
            bus.accepting_irq=false;
            /* On ordinary NACK HAL already quiesces DMA. Abort only when
             * hardware still owns it, including cancellation without STOP. */
            if(r->operation!=SRNOS_I2C_OP_PROBE && (!completion || !SRNOS_BoardI2CQuiescent(bus.hal))) {
                SRNOS_I2C_DBG.last_recover_reason=r->cancel?r->cancel_reason:SRNOS_I2C_RECOVER_REASON_TIMEOUT;
                if(!abort_active()) result=SRNOS_I2C_ERROR;
            }
            if(result==SRNOS_I2C_OK && r->rx)
                (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(r->rx,r->length),SRNOS_Memory_Bytes(s_i2c_rx_stage,r->length));
            uint32_t elapsed=(uint32_t)(SRNOS_TimeGetUptimeUs()-r->started);
            SRNOS_I2CClassStatistics *stats=&bus.stats[category(r)];
            if(elapsed>stats->max_transfer_us) stats->max_transfer_us=elapsed;
            finish(r,result);
        }
    }
    for(unsigned i=0;i<4;i++) {
        r=&bus.slots[i];
        if(r->state==SRNOS_I2C_SLOT_QUEUED && (r->cancel || bus.quarantined || SRNOS_TimeGetUptimeUs()-r->enqueued>=r->queue_us))
            finish(r,bus.quarantined?SRNOS_I2C_ERROR:r->cancel && r->cancel_reason!=SRNOS_I2C_RECOVER_REASON_TIMEOUT?SRNOS_I2C_CANCELLED:SRNOS_I2C_TIMEOUT);
    }
    if(bus.active || bus.quarantined) return;
    uint32_t m=enter();
    SRNOS_I2CRequest *periodic=oldest(0,2), *background=oldest(2,4);
    r=periodic && (bus.periodic_run<4 || !background)?periodic:background;
    if(r) {
        if(r==periodic) { if(bus.periodic_run<4) ++bus.periodic_run; } else bus.periodic_run=0;
        r->state=SRNOS_I2C_SLOT_ACTIVE; bus.active=r;
    }
    leave(m);
    if(r) start_request(r);
}
static void dispatch_step(void *arg) {(void)arg;service_once();}
static uint32_t next_wait(void) {
    uint64_t now=SRNOS_TimeGetUptimeUs(), deadline=UINT64_MAX;
    uint32_t m=enter();
    if(bus.completion) { leave(m);return 0; }
    for(unsigned i=0;i<4;i++) {
        SRNOS_I2CRequest *r=&bus.slots[i];
        if(r->state==SRNOS_I2C_SLOT_ACTIVE) {
            uint64_t end=r->started+10000U;
            if(r->cancel) { leave(m);return 0; }
            if(end<deadline) deadline=end;
        } else if(r->state==SRNOS_I2C_SLOT_QUEUED) {
            if(!bus.active || r->cancel || bus.quarantined) { leave(m);return 0; }
            uint64_t end=r->enqueued+r->queue_us;
            if(end<deadline) deadline=end;
        }
        /* The producer owns RESERVED and notifies after publishing QUEUED. */
    }
    leave(m);
    if(deadline==UINT64_MAX) return osWaitForever;
    if(now>=deadline) return 0;
    uint64_t delay=((deadline-now)*osKernelGetTickFreq()+999999U)/1000000U;
    return delay<osWaitForever?(uint32_t)delay:osWaitForever-1U;
}
static bool dispatch_wait(void *arg) {
    (void)arg;uint32_t delay=next_wait();
    if(delay) (void)osThreadFlagsWait(1U,osFlagsWaitAny,delay);
    return true;
}
static void dispatch(void *argument) {
    (void)argument;const SRNOS_InternalLoop loop={dispatch_step,dispatch_wait,NULL,0,false};
    SRNOS_KernelRunLoop(&loop);
}
void SRNOS_I2CInit(void) {
    memset(&bus,0,sizeof(bus)); worker=NULL;
    bus.hal=SRNOS_BoardI2CHandle(SRNOS_I2C_BUS_4);
    SRNOS_TimeInit(0);
    SRNOS_DbgZero(&SRNOS_I2C_DBG,sizeof(SRNOS_I2C_DBG)); SRNOS_DbgInit(&SRNOS_I2C_DBG.h,SRNOS_DBG_MODULE_I2C,0,sizeof(SRNOS_I2C_DBG));
}
void SRNOS_I2CInitRtos(void) {
    for(unsigned i=0;i<4;i++) {
        bus.slots[i].done=osSemaphoreNew(1U,0U,NULL);
        configASSERT(bus.slots[i].done);
    }
    const osThreadAttr_t attr={.name="SRNOS_I2C4",.stack_size=3072,.priority=osPriorityHigh6};
    worker=SRNOS_KernelThreadNew(dispatch,NULL,&attr); configASSERT(worker);
}
SRNOS_I2CStatus SRNOS_I2CDeviceInit(SRNOS_I2CDevice *dev,SRNOS_I2CBusId id,uint16_t address) {
    if(!dev || id!=SRNOS_I2C_BUS_4 || address>0x7FU || !bus.hal || __get_IPSR()) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter();
    if(find_device(dev) || bus.callback_device==dev) { leave(m); return SRNOS_I2C_BUSY; }
    *dev=(SRNOS_I2CDevice){.bus=&bus,.addr_7bit=address,.completed_status=SRNOS_I2C_OK};
    leave(m); return SRNOS_I2C_OK;
}
static SRNOS_I2CStatus submit(SRNOS_I2CDevice *dev,SRNOS_I2COperation operation,uint16_t address,uint16_t width,
    const uint8_t *tx,uint8_t *rx,size_t length,SRNOS_I2CCallback callback,void *context) {
    if(!task_context() || !valid(dev)) return SRNOS_I2C_INVALID_PARAM;
    if(operation!=SRNOS_I2C_OP_PROBE && (!length || length>SRNOS_I2C_MAX_TRANSFER_BYTES || (!tx&&!rx))) return SRNOS_I2C_INVALID_PARAM;
    if((operation==SRNOS_I2C_OP_MEM_TX || operation==SRNOS_I2C_OP_MEM_RX) && width!=I2C_MEMADD_SIZE_8BIT && width!=I2C_MEMADD_SIZE_16BIT) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter();
    if(bus.quarantined) { leave(m); return SRNOS_I2C_ERROR; }
    if(bus.callback_device && bus.callback_device->addr_7bit==dev->addr_7bit && osThreadGetId()!=worker) { leave(m); return SRNOS_I2C_BUSY; }
    for(unsigned i=0;i<4;i++) if(bus.slots[i].state!=SRNOS_I2C_SLOT_FREE && bus.slots[i].device->addr_7bit==dev->addr_7bit) { leave(m); return SRNOS_I2C_BUSY; }
    unsigned first=device_slot(dev->addr_7bit), last=first<2?first+1:4;
    SRNOS_I2CRequest *r=NULL;
    for(unsigned i=first;i<last;i++) if(bus.slots[i].state==SRNOS_I2C_SLOT_FREE) { r=&bus.slots[i];break; }
    SRNOS_I2CClassStatistics *stats=&bus.stats[first<2?0:1];
    if(!r) { ++stats->queue_full; ++SRNOS_I2C_DBG.xfer_busy; leave(m); return SRNOS_I2C_BUSY; }
    r->device=dev; r->operation=operation; r->callback=callback; r->context=context;
    r->rx=rx; r->length=length; r->address=address; r->address_size=width;
    r->waiter=NULL; r->cancel=false; r->cancel_reason=0;
    r->order=++bus.order; r->enqueued=SRNOS_TimeGetUptimeUs(); r->queue_us=first<2?20000U:100000U;
    r->state=SRNOS_I2C_SLOT_RESERVED;
    dev->pending=1; dev->completed_status=SRNOS_I2C_BUSY;
    leave(m);
    if(tx) (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(r->tx,sizeof(r->tx)),SRNOS_Memory_Bytes(tx,length));
    m=enter();
    r->state=SRNOS_I2C_SLOT_QUEUED; ++stats->accepted;
    unsigned depth=queued(); if(depth>bus.high_water) bus.high_water=depth;
    leave(m); wake(); return SRNOS_I2C_OK;
}
SRNOS_I2CStatus SRNOS_I2CMasterTransmit(SRNOS_I2CDevice *d,const uint8_t *p,size_t n,SRNOS_I2CCallback c,void *x) { return submit(d,SRNOS_I2C_OP_TX,0,0,p,NULL,n,c,x); }
SRNOS_I2CStatus SRNOS_I2CMasterReceive(SRNOS_I2CDevice *d,uint8_t *p,size_t n,SRNOS_I2CCallback c,void *x) { return submit(d,SRNOS_I2C_OP_RX,0,0,NULL,p,n,c,x); }
SRNOS_I2CStatus SRNOS_I2CMemRead(SRNOS_I2CDevice *d,uint16_t a,uint16_t w,uint8_t *p,size_t n,SRNOS_I2CCallback c,void *x) { return submit(d,SRNOS_I2C_OP_MEM_RX,a,w,NULL,p,n,c,x); }
SRNOS_I2CStatus SRNOS_I2CMemWrite(SRNOS_I2CDevice *d,uint16_t a,uint16_t w,const uint8_t *p,size_t n,SRNOS_I2CCallback c,void *x) { return submit(d,SRNOS_I2C_OP_MEM_TX,a,w,p,NULL,n,c,x); }





static void request_cancel(SRNOS_I2CRequest *r,uint32_t reason) { r->cancel=true; r->cancel_reason=reason; }
SRNOS_I2CStatus SRNOS_I2CWaitComplete(SRNOS_I2CDevice *dev,uint32_t timeout_ms) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker || !timeout_valid(timeout_ms)) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter(); SRNOS_I2CRequest *r=find_device(dev);
    if(!r) { SRNOS_I2CStatus result=bus.callback_device==dev?SRNOS_I2C_BUSY:dev->completed_status; leave(m); return result; }
    if(r->waiter || timeout_ms==SRNOS_NO_WAIT) { leave(m); return SRNOS_I2C_BUSY; }
    r->waiter=osThreadGetId(); leave(m);
    if(osSemaphoreAcquire(r->done,ticks(timeout_ms))!=osOK) {
        m=enter(); if(r->state!=SRNOS_I2C_SLOT_DONE) request_cancel(r,SRNOS_I2C_RECOVER_REASON_TIMEOUT); leave(m); wake();
        (void)osSemaphoreAcquire(r->done,osWaitForever);
    }
    m=enter(); SRNOS_I2CStatus result=r->result; r->state=SRNOS_I2C_SLOT_FREE; r->waiter=NULL; leave(m);
    return result;
}
static SRNOS_I2CStatus cancel_ticket(SRNOS_I2CRequest *r,uint32_t order,SRNOS_I2CDevice *dev) {
    wake();
    for(;;) {
        uint32_t m=enter(); bool pending=r && r->order==order && (r->state==SRNOS_I2C_SLOT_RESERVED || r->state==SRNOS_I2C_SLOT_QUEUED || r->state==SRNOS_I2C_SLOT_ACTIVE);
        bool callback=dev && bus.callback_device==dev;
        bool faulted=bus.quarantined; leave(m);
        if(!pending && !callback) return faulted?SRNOS_I2C_ERROR:SRNOS_I2C_OK;
        osDelay(1U);
    }
}
SRNOS_I2CStatus SRNOS_I2CCancelDevice(SRNOS_I2CDevice *dev,uint32_t reason) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter(); SRNOS_I2CRequest *r=find_device(dev); uint32_t order=r?r->order:0;
    if(r && r->state!=SRNOS_I2C_SLOT_DONE) request_cancel(r,reason);
    bool callback=bus.callback_device==dev; leave(m);
    if(!r && callback) { while(bus.callback_device==dev) osDelay(1); }
    return cancel_ticket(r,order,dev);
}
SRNOS_I2CStatus SRNOS_I2CRecoverBus(SRNOS_I2CBusId id,uint32_t reason) {
    if(id!=SRNOS_I2C_BUS_4 || !task_context() || osThreadGetId()==worker) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter(); SRNOS_I2CRequest *r=bus.active; uint32_t order=r?r->order:0;
    SRNOS_I2CDevice *dev=r?r->device:NULL;
    if(r) request_cancel(r,reason);
    leave(m);
    return cancel_ticket(r,order,dev);
}
SRNOS_I2CStatus SRNOS_I2CDeviceReady(SRNOS_I2CDevice *dev,uint32_t trials,uint32_t timeout_ms) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker || !trials || !timeout_valid(timeout_ms)) return SRNOS_I2C_INVALID_PARAM;
    if(timeout_ms==SRNOS_NO_WAIT) return SRNOS_I2C_TIMEOUT;
    const bool forever=timeout_ms==SRNOS_WAIT_FOREVER;
    uint32_t start=HAL_GetTick(); SRNOS_I2CStatus result=SRNOS_I2C_TIMEOUT;
    for(uint32_t i=0;i<trials && (forever || HAL_GetTick()-start<timeout_ms);i++) {
        result=submit(dev,SRNOS_I2C_OP_PROBE,0,0,NULL,NULL,0,NULL,NULL);
        if(result!=SRNOS_I2C_OK) return result;
        uint32_t elapsed=HAL_GetTick()-start;
        if(!forever && elapsed>=timeout_ms) {
            /* NO_WAIT is a query, not cancellation. Confirm release explicitly
             * when preemption during submit consumed the complete budget. */
            result=SRNOS_I2CCancelDevice(dev,SRNOS_I2C_RECOVER_REASON_TIMEOUT);
            if(result==SRNOS_I2C_OK) result=SRNOS_I2C_TIMEOUT;
            break;
        }
        result=SRNOS_I2CWaitComplete(dev,forever?SRNOS_WAIT_FOREVER:timeout_ms-elapsed);
        if(result==SRNOS_I2C_OK || result==SRNOS_I2C_TIMEOUT) break;
    }
    return result;
}
SRNOS_I2CStatus SRNOS_I2CIsBusBusy(const SRNOS_I2CDevice *dev) {
    if(!valid(dev)) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter();
    SRNOS_I2CStatus result=bus.quarantined?SRNOS_I2C_ERROR:SRNOS_I2C_OK;
    for(unsigned i=0;result==SRNOS_I2C_OK && i<4;i++) {
        unsigned state=bus.slots[i].state;
        if(state==SRNOS_I2C_SLOT_RESERVED || state==SRNOS_I2C_SLOT_QUEUED || state==SRNOS_I2C_SLOT_ACTIVE) result=SRNOS_I2C_BUSY;
    }
    leave(m); return result;
}
SRNOS_I2CStatus SRNOS_I2CGetSnapshot(SRNOS_I2CBusId id,SRNOS_I2CSnapshot *out) {
    if(id!=SRNOS_I2C_BUS_4 || !out || !bus.hal) return SRNOS_I2C_INVALID_PARAM;
    uint32_t m=enter();
    *out=(SRNOS_I2CSnapshot){.state=bus.active!=NULL,.hal_state=HAL_I2C_GetState(bus.hal),.hal_error=HAL_I2C_GetError(bus.hal),
        .lock_held=bus.active!=NULL,.lock_owner=bus.active?(uintptr_t)worker:0,.current_dev=bus.active?(uintptr_t)bus.active->device:0,
        .current_len=bus.active?(uint32_t)bus.active->length:0,.last_status=SRNOS_I2C_DBG.h.last_status,.last_tick=HAL_GetTick(),
        .recover_count=SRNOS_I2C_DBG.recover_count,.queued=queued(),.high_water=bus.high_water,.quarantined=bus.quarantined,
        .periodic=bus.stats[0],.background=bus.stats[1]};
    leave(m); return SRNOS_I2C_OK;
}
static void completed(I2C_HandleTypeDef *hal,unsigned status) {
    if(hal!=bus.hal || !bus.accepting_irq || bus.recovering || !bus.active) return;
    bus.accepting_irq=false; bus.completion=status; wake();
}
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hal) { completed(hal,1); }
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hal) { completed(hal,1); }
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hal) { completed(hal,1); }
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hal) { completed(hal,1); }
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hal) { completed(hal,2); }
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *hal) { if(hal==bus.hal && bus.recovering) bus.abort_done=true; }
