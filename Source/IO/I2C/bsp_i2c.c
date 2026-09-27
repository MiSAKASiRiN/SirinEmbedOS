/* Fixed I2C4: periodic LIS/SPA traffic and bounded background storage traffic. */
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "fixed_buses.h"
#include <string.h>

_Static_assert(BSP_I2C_DMA_STAGING_SIZE >= BSP_I2C_MAX_TRANSFER_BYTES, "I2C staging too small");
enum { FREE, QUEUED, ACTIVE, DONE };
typedef enum { TX, RX, MEM_TX, MEM_RX, PROBE } Operation;
typedef struct {
    unsigned state;
    Operation operation;
    bsp_i2c_device_t *device;
    bsp_i2c_callback_t callback;
    void *context;
    osThreadId_t waiter;
    osSemaphoreId_t done;
    uint32_t order, queue_us, cancel_reason;
    uint64_t enqueued, started;
    bool cancel;
    uint16_t address, address_size;
    size_t length;
    uint8_t tx[BSP_I2C_MAX_TRANSFER_BYTES];
    uint8_t *rx;
    bsp_i2c_status_t result;
} Request;
struct bsp_i2c_bus {
    I2C_HandleTypeDef *hal;
    Request slots[4]; /* LIS, SPA, background, background. */
    Request *active;
    bsp_i2c_device_t *callback_device;
    volatile unsigned completion;
    volatile bool accepting_irq, recovering, abort_done;
    bool quarantined;
    unsigned periodic_run;
    uint32_t order, high_water;
    bsp_i2c_class_statistics_t stats[2];
};
static struct bsp_i2c_bus bus;
static osThreadId_t worker;
static uint8_t s_i2c_tx_stage[BSP_I2C_DMA_STAGING_SIZE] BSP_I2C_DMA_SEC;
static uint8_t s_i2c_rx_stage[BSP_I2C_DMA_STAGING_SIZE] BSP_I2C_DMA_SEC;
OMX_DEBUG_WATCH_BSS volatile omx_dbg_i2c_t g_omx_dbg_i2c[BSP_I2C_BUS_MAX];
#define DBG g_omx_dbg_i2c[0]
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static void wake(void) { if(worker) (void)osThreadFlagsSet(worker,1U); }
static uint32_t ticks(uint32_t ms) { return (uint32_t)(((uint64_t)ms*osKernelGetTickFreq()+999U)/1000U); }
static bool task_context(void) {
    return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI() && worker && osKernelGetState()==osKernelRunning;
}
static bool valid(const bsp_i2c_device_t *dev) { return dev && dev->bus==&bus && dev->addr_7bit<=0x7FU; }
static unsigned device_slot(uint16_t address) { return address==0x1EU?0U:address==0x76U?1U:2U; }
static unsigned category(const Request *r) { return device_slot(r->device->addr_7bit)<2?0:1; }
static unsigned queued(void) {
    unsigned n=0; for(unsigned i=0;i<4;i++) if(bus.slots[i].state==QUEUED) ++n; return n;
}
static Request *find_device(const bsp_i2c_device_t *dev) {
    for(unsigned i=0;i<4;i++) if(bus.slots[i].state!=FREE && bus.slots[i].device==dev) return &bus.slots[i];
    return NULL;
}
static Request *oldest(unsigned first,unsigned last) {
    Request *best=NULL;
    for(unsigned i=first;i<last;i++) {
        Request *r=&bus.slots[i];
        if(r->state==QUEUED && (!best || (int32_t)(r->order-best->order)<0)) best=r;
    }
    return best;
}
static void finish(Request *r,bsp_i2c_status_t result) {
    uint32_t m=enter();
    bsp_i2c_device_t *dev=r->device;
    bsp_i2c_callback_t cb=r->callback; void *ctx=r->context;
    bool waiting=r->waiter!=NULL;
    bsp_i2c_class_statistics_t *stats=&bus.stats[category(r)];
    if(result==BSP_I2C_OK) { ++stats->completed; ++DBG.xfer_done; }
    else if(result==BSP_I2C_CANCELLED) ++stats->cancelled;
    else { ++stats->errors; ++DBG.xfer_error; }
    if(result==BSP_I2C_TIMEOUT) { ++stats->timeouts; ++DBG.xfer_timeout; }
    r->result=result;
    if(bus.active==r) {
        bus.active=NULL;
        bus.completion=0;
        DBG.state=0; DBG.current_dev=0; DBG.lock_held=0; DBG.lock_owner=0;
    }
    dev->completed_status=result;
    dev->pending=0;
    r->state=waiting?DONE:FREE;
    bus.callback_device=cb?dev:NULL;
    DBG.h.last_status=result; DBG.h.last_tick=HAL_GetTick(); DBG.cb_status=result;
    leave(m);
    if(cb) { ++DBG.cb_queued; cb(dev,ctx,result); }
    m=enter(); bus.callback_device=NULL; leave(m);
    /* DONE slot cannot be reused until its waiter consumes this token. */
    if(waiting) (void)osSemaphoreRelease(r->done);
}
static bool abort_active(void) {
    bus.accepting_irq=false; bus.recovering=true; bus.abort_done=false;
    ++DBG.recover_count;
    HAL_StatusTypeDef status=HAL_I2C_Master_Abort_IT(bus.hal,(uint16_t)(bus.active->device->addr_7bit<<1));
    uint32_t start=HAL_GetTick();
    while(status==HAL_OK && !bus.abort_done && HAL_GetTick()-start<5U) osDelay(1U);
    bool stopped=bus.abort_done && SRNOS_BoardI2CQuiescent(bus.hal);
    bus.quarantined=!stopped; bus.recovering=false; bus.completion=0;
    if(!stopped) ++DBG.recover_error;
    return stopped;
}
static void start_request(Request *r) {
    r->started=DWT_GetUptimeUs();
    bsp_i2c_class_statistics_t *stats=&bus.stats[category(r)];
    uint32_t delay=(uint32_t)(r->started-r->enqueued);
    if(delay>stats->max_queue_us) stats->max_queue_us=delay;
    DBG.state=1; DBG.lock_held=1; DBG.lock_owner=(uintptr_t)worker;
    DBG.current_dev=(uintptr_t)r->device; DBG.last_dev=(uintptr_t)r->device;
    DBG.last_len=(uint32_t)r->length; DBG.mem_addr=r->address; DBG.mem_addr_size=r->address_size;
    DBG.xfer_type=(uint8_t)r->operation; DBG.rx_copy_back=r->rx!=NULL; DBG.direct_xfer=0;
    ++DBG.xfer_start;
    if(r->operation==PROBE) {
        HAL_StatusTypeDef st=HAL_I2C_IsDeviceReady(bus.hal,(uint16_t)(r->device->addr_7bit<<1),1U,1U);
        bus.completion=st==HAL_OK?1:2;
        return;
    }
    if(!r->rx) (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(s_i2c_tx_stage,sizeof(s_i2c_tx_stage)),SRNOS_Memory_Bytes(r->tx,r->length));
    bus.completion=0; bus.accepting_irq=true;
    uint16_t address=(uint16_t)(r->device->addr_7bit<<1);
    HAL_StatusTypeDef st;
    switch(r->operation) {
    case TX: st=HAL_I2C_Master_Transmit_DMA(bus.hal,address,s_i2c_tx_stage,(uint16_t)r->length); break;
    case RX: st=HAL_I2C_Master_Receive_DMA(bus.hal,address,s_i2c_rx_stage,(uint16_t)r->length); break;
    case MEM_TX: st=HAL_I2C_Mem_Write_DMA(bus.hal,address,r->address,r->address_size,s_i2c_tx_stage,(uint16_t)r->length); break;
    default: st=HAL_I2C_Mem_Read_DMA(bus.hal,address,r->address,r->address_size,s_i2c_rx_stage,(uint16_t)r->length); break;
    }
    if(st!=HAL_OK) { bus.accepting_irq=false; bus.completion=2; }
}
/* Only the dispatcher owns HAL and DMA staging; ISR merely publishes an event.
 * One bounded pass is also exercised by host tests. */
static void service_once(void) {
    Request *r=bus.active;
    if(r) {
        unsigned completion=bus.completion;
        bool expired=DWT_GetUptimeUs()-r->started>=10000U;
        if(completion || r->cancel || expired) {
            bsp_i2c_status_t result=r->cancel?(r->cancel_reason==BSP_I2C_RECOVER_REASON_TIMEOUT?BSP_I2C_TIMEOUT:BSP_I2C_CANCELLED):
                completion==1?BSP_I2C_OK:completion==2?BSP_I2C_ERROR:BSP_I2C_TIMEOUT;
            bus.accepting_irq=false;
            /* On ordinary NACK HAL already quiesces DMA. Abort only when
             * hardware still owns it, including cancellation without STOP. */
            if(r->operation!=PROBE && (!completion || !SRNOS_BoardI2CQuiescent(bus.hal))) {
                DBG.last_recover_reason=r->cancel?r->cancel_reason:BSP_I2C_RECOVER_REASON_TIMEOUT;
                if(!abort_active()) result=BSP_I2C_ERROR;
            }
            if(result==BSP_I2C_OK && r->rx)
                (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(r->rx,r->length),SRNOS_Memory_Bytes(s_i2c_rx_stage,r->length));
            uint32_t elapsed=(uint32_t)(DWT_GetUptimeUs()-r->started);
            bsp_i2c_class_statistics_t *stats=&bus.stats[category(r)];
            if(elapsed>stats->max_transfer_us) stats->max_transfer_us=elapsed;
            finish(r,result);
        }
    }
    for(unsigned i=0;i<4;i++) {
        r=&bus.slots[i];
        if(r->state==QUEUED && (r->cancel || bus.quarantined || DWT_GetUptimeUs()-r->enqueued>=r->queue_us))
            finish(r,bus.quarantined?BSP_I2C_ERROR:r->cancel && r->cancel_reason!=BSP_I2C_RECOVER_REASON_TIMEOUT?BSP_I2C_CANCELLED:BSP_I2C_TIMEOUT);
    }
    if(bus.active || bus.quarantined) return;
    uint32_t m=enter();
    Request *periodic=oldest(0,2), *background=oldest(2,4);
    r=periodic && (bus.periodic_run<4 || !background)?periodic:background;
    if(r) {
        if(r==periodic) { if(bus.periodic_run<4) ++bus.periodic_run; } else bus.periodic_run=0;
        r->state=ACTIVE; bus.active=r;
    }
    leave(m);
    if(r) start_request(r);
}
static void dispatch(void *argument) {
    (void)argument;
    for(;;) {
        service_once();
        /* Probe/start failure has no later IRQ to wake us. */
        if(bus.completion) continue;
        (void)osThreadFlagsWait(1U,osFlagsWaitAny,bus.active || queued()?1U:osWaitForever);
    }
}
void bsp_i2c_init(void) {
    memset(&bus,0,sizeof(bus)); worker=NULL;
    bus.hal=SRNOS_BoardI2CHandle(BSP_I2C_BUS_4);
    DWT_Init(0);
    omx_dbg_zero(&DBG,sizeof(DBG)); omx_dbg_init(&DBG.h,OMX_DBG_MODULE_I2C,0,sizeof(DBG));
}
void bsp_i2c_init_rtos(void) {
    for(unsigned i=0;i<4;i++) {
        bus.slots[i].done=osSemaphoreNew(1U,0U,NULL);
        configASSERT(bus.slots[i].done);
    }
    const osThreadAttr_t attr={.name="I2C4",.stack_size=3072,.priority=osPriorityHigh6};
    worker=osThreadNew(dispatch,NULL,&attr); configASSERT(worker);
}
bsp_i2c_status_t bsp_i2c_device_init(bsp_i2c_device_t *dev,bsp_i2c_bus_id_t id,uint16_t address) {
    if(!dev || id!=BSP_I2C_BUS_4 || address>0x7FU || !bus.hal || __get_IPSR()) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter();
    if(find_device(dev) || bus.callback_device==dev) { leave(m); return BSP_I2C_BUSY; }
    *dev=(bsp_i2c_device_t){.bus=&bus,.addr_7bit=address,.completed_status=BSP_I2C_OK};
    leave(m); return BSP_I2C_OK;
}
static bsp_i2c_status_t submit(bsp_i2c_device_t *dev,Operation operation,uint16_t address,uint16_t width,
    const uint8_t *tx,uint8_t *rx,size_t length,bsp_i2c_callback_t callback,void *context) {
    if(!task_context() || !valid(dev)) return BSP_I2C_INVALID_PARAM;
    if(operation!=PROBE && (!length || length>BSP_I2C_MAX_TRANSFER_BYTES || (!tx&&!rx))) return BSP_I2C_INVALID_PARAM;
    if((operation==MEM_TX || operation==MEM_RX) && width!=I2C_MEMADD_SIZE_8BIT && width!=I2C_MEMADD_SIZE_16BIT) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter();
    if(bus.quarantined) { leave(m); return BSP_I2C_ERROR; }
    if(bus.callback_device && bus.callback_device->addr_7bit==dev->addr_7bit && osThreadGetId()!=worker) { leave(m); return BSP_I2C_BUSY; }
    for(unsigned i=0;i<4;i++) if(bus.slots[i].state!=FREE && bus.slots[i].device->addr_7bit==dev->addr_7bit) { leave(m); return BSP_I2C_BUSY; }
    unsigned first=device_slot(dev->addr_7bit), last=first<2?first+1:4;
    Request *r=NULL;
    for(unsigned i=first;i<last;i++) if(bus.slots[i].state==FREE) { r=&bus.slots[i];break; }
    bsp_i2c_class_statistics_t *stats=&bus.stats[first<2?0:1];
    if(!r) { ++stats->queue_full; ++DBG.xfer_busy; leave(m); return BSP_I2C_BUSY; }
    r->device=dev; r->operation=operation; r->callback=callback; r->context=context;
    r->rx=rx; r->length=length; r->address=address; r->address_size=width;
    r->waiter=NULL; r->cancel=false; r->cancel_reason=0;
    r->order=++bus.order; r->enqueued=DWT_GetUptimeUs(); r->queue_us=first<2?20000U:100000U;
    if(tx) (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(r->tx,sizeof(r->tx)),SRNOS_Memory_Bytes(tx,length));
    dev->pending=1; dev->completed_status=BSP_I2C_BUSY;
    r->state=QUEUED; ++stats->accepted;
    unsigned depth=queued(); if(depth>bus.high_water) bus.high_water=depth;
    leave(m); wake(); return BSP_I2C_OK;
}
bsp_i2c_status_t bsp_i2c_master_transmit(bsp_i2c_device_t *d,const uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return submit(d,TX,0,0,p,NULL,n,c,x); }
bsp_i2c_status_t bsp_i2c_master_receive(bsp_i2c_device_t *d,uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return submit(d,RX,0,0,NULL,p,n,c,x); }
bsp_i2c_status_t bsp_i2c_mem_read(bsp_i2c_device_t *d,uint16_t a,uint16_t w,uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return submit(d,MEM_RX,a,w,NULL,p,n,c,x); }
bsp_i2c_status_t bsp_i2c_mem_write(bsp_i2c_device_t *d,uint16_t a,uint16_t w,const uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return submit(d,MEM_TX,a,w,p,NULL,n,c,x); }
bsp_i2c_status_t bsp_i2c_master_transmit_direct(bsp_i2c_device_t *d,const uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return bsp_i2c_master_transmit(d,p,n,c,x); }
bsp_i2c_status_t bsp_i2c_master_receive_direct(bsp_i2c_device_t *d,uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return bsp_i2c_master_receive(d,p,n,c,x); }
bsp_i2c_status_t bsp_i2c_mem_read_direct(bsp_i2c_device_t *d,uint16_t a,uint16_t w,uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return bsp_i2c_mem_read(d,a,w,p,n,c,x); }
bsp_i2c_status_t bsp_i2c_mem_write_direct(bsp_i2c_device_t *d,uint16_t a,uint16_t w,const uint8_t *p,size_t n,bsp_i2c_callback_t c,void *x) { return bsp_i2c_mem_write(d,a,w,p,n,c,x); }

static void request_cancel(Request *r,uint32_t reason) { r->cancel=true; r->cancel_reason=reason; }
bsp_i2c_status_t bsp_i2c_wait_complete(bsp_i2c_device_t *dev,uint32_t timeout_ms) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker || timeout_ms>INT32_MAX) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter(); Request *r=find_device(dev);
    if(!r) { bsp_i2c_status_t result=dev->completed_status; leave(m); return result; }
    if(r->waiter) { leave(m); return BSP_I2C_BUSY; }
    r->waiter=osThreadGetId(); leave(m);
    if(osSemaphoreAcquire(r->done,ticks(timeout_ms))!=osOK) {
        m=enter(); if(r->state!=DONE) request_cancel(r,BSP_I2C_RECOVER_REASON_TIMEOUT); leave(m); wake();
        (void)osSemaphoreAcquire(r->done,osWaitForever);
    }
    m=enter(); bsp_i2c_status_t result=r->result; r->state=FREE; r->waiter=NULL; leave(m);
    return result;
}
static bsp_i2c_status_t cancel_ticket(Request *r,uint32_t order,bsp_i2c_device_t *dev) {
    wake();
    for(;;) {
        uint32_t m=enter(); bool pending=r && r->order==order && (r->state==QUEUED || r->state==ACTIVE);
        bool callback=dev && bus.callback_device==dev;
        bool faulted=bus.quarantined; leave(m);
        if(!pending && !callback) return faulted?BSP_I2C_ERROR:BSP_I2C_OK;
        osDelay(1U);
    }
}
bsp_i2c_status_t bsp_i2c_cancel_device(bsp_i2c_device_t *dev,uint32_t reason) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter(); Request *r=find_device(dev); uint32_t order=r?r->order:0;
    if(r && r->state!=DONE) request_cancel(r,reason);
    bool callback=bus.callback_device==dev; leave(m);
    if(!r && callback) { while(bus.callback_device==dev) osDelay(1); }
    return cancel_ticket(r,order,dev);
}
bsp_i2c_status_t bsp_i2c_recover_bus(bsp_i2c_bus_id_t id,uint32_t reason) {
    if(id!=BSP_I2C_BUS_4 || !task_context() || osThreadGetId()==worker) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter(); Request *r=bus.active; uint32_t order=r?r->order:0;
    bsp_i2c_device_t *dev=r?r->device:NULL;
    if(r) request_cancel(r,reason);
    leave(m);
    return cancel_ticket(r,order,dev);
}
bsp_i2c_status_t bsp_i2c_device_ready(bsp_i2c_device_t *dev,uint32_t trials,uint32_t timeout_ms) {
    if(!task_context() || !valid(dev) || osThreadGetId()==worker || !trials || !timeout_ms || timeout_ms>INT32_MAX) return BSP_I2C_INVALID_PARAM;
    uint32_t start=HAL_GetTick(); bsp_i2c_status_t result=BSP_I2C_BUSY;
    for(uint32_t i=0;i<trials && HAL_GetTick()-start<timeout_ms;i++) {
        result=submit(dev,PROBE,0,0,NULL,NULL,0,NULL,NULL);
        if(result!=BSP_I2C_OK) return result;
        uint32_t elapsed=HAL_GetTick()-start;
        result=bsp_i2c_wait_complete(dev,elapsed<timeout_ms?timeout_ms-elapsed:0);
        if(result==BSP_I2C_OK || result==BSP_I2C_TIMEOUT) break;
    }
    return result;
}
bsp_i2c_status_t bsp_i2c_is_bus_busy(const bsp_i2c_device_t *dev) {
    if(!valid(dev)) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter(); bsp_i2c_status_t result=bus.quarantined?BSP_I2C_ERROR:bus.active || queued()?BSP_I2C_BUSY:BSP_I2C_OK; leave(m); return result;
}
bsp_i2c_status_t bsp_i2c_get_snapshot(bsp_i2c_bus_id_t id,bsp_i2c_snapshot_t *out) {
    if(id!=BSP_I2C_BUS_4 || !out || !bus.hal) return BSP_I2C_INVALID_PARAM;
    uint32_t m=enter();
    *out=(bsp_i2c_snapshot_t){.state=bus.active!=NULL,.hal_state=HAL_I2C_GetState(bus.hal),.hal_error=HAL_I2C_GetError(bus.hal),
        .lock_held=bus.active!=NULL,.lock_owner=bus.active?(uintptr_t)worker:0,.current_dev=bus.active?(uintptr_t)bus.active->device:0,
        .current_len=bus.active?(uint32_t)bus.active->length:0,.last_status=DBG.h.last_status,.last_tick=HAL_GetTick(),
        .recover_count=DBG.recover_count,.queued=queued(),.high_water=bus.high_water,.quarantined=bus.quarantined,
        .periodic=bus.stats[0],.background=bus.stats[1]};
    leave(m); return BSP_I2C_OK;
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
