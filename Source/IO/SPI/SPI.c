#include "thread_internal.h"
/* SPI4 has one owner: this dispatcher. Callers never touch HAL or CS. */
#include "IO/SPI/spi_internal.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "IO/spi_board.h"
#include <string.h>

enum { SRNOS_SPI_SLOT_FREE, SRNOS_SPI_SLOT_RESERVED, SRNOS_SPI_SLOT_QUEUED, SRNOS_SPI_SLOT_ACTIVE, SRNOS_SPI_SLOT_DONE };
typedef struct {
    unsigned state;
    uint32_t order;
    uint64_t enqueued, started, submitted;
    SRNOS_SPISequencePrepare prepare;
    unsigned sequence_completed;
    uint64_t deadline_us;
    SRNOS_SPIDevice *dev;
    SRNOS_SPICallback cb;
    void *ctx;
    uint8_t tx[SRNOS_SPI_DMA_STAGING_SIZE];
    uint8_t *rx;
    size_t len;
    bool synchronous, cancel;
    SRNOS_SPIStatus result;
    osSemaphoreId_t done;
} SRNOS_SPIRequest;
struct SRNOS_SPIBus {
    SPI_HandleTypeDef *hspi;
    DMA_HandleTypeDef *tx, *rx;
    SRNOS_SPIRequest slots[8];
    SRNOS_SPIRequest *active;
    SRNOS_SPIDevice *callback_dev;
    uint32_t order, imu_run, can_run;
    volatile unsigned completion;
    volatile bool accepting_irq, recover_requested;
    bool faulted;
};
static struct SRNOS_SPIBus bus;
static osThreadId_t worker;
static uint8_t tx_stage[SRNOS_SPI_DMA_STAGING_SIZE] SRNOS_SPI_DMA_SEC;
static uint8_t rx_stage[SRNOS_SPI_DMA_STAGING_SIZE] SRNOS_SPI_DMA_SEC;
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgSPI SRNOS_DebugDbgSPI[SRNOS_SPI_BUS_MAX];
#define SRNOS_SPI_DBG SRNOS_DebugDbgSPI[0]
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static uint32_t ticks_us(uint64_t us) { return (uint32_t)((us*osKernelGetTickFreq()+999999)/1000000); }
static void wake(void) {
    if(!worker)return;
    if(__get_IPSR()) {
        BaseType_t woken=pdFALSE;
        (void)xTaskNotifyFromISR((TaskHandle_t)worker,1,eSetBits,&woken);
        portYIELD_FROM_ISR(woken);
    } else (void)xTaskNotify((TaskHandle_t)worker,1,eSetBits);
}
static void histogram(uint32_t bins[6], uint32_t us) {
    unsigned i=us<=25?0:us<=100?1:us<=250?2:us<=500?3:us<=1000?4:5;
    ++bins[i];
}
static void finish(SRNOS_SPIRequest *r, SRNOS_SPIStatus status, bool count_success)
{
    uint32_t m=enter();
    SRNOS_SPIDevice *dev=r->dev;
    SRNOS_SPICallback cb=r->cb;
    void *ctx=r->ctx;
    if (status==SRNOS_SPI_OK) { if(count_success) { ++dev->statistics.completed; ++SRNOS_SPI_DBG.xfer_done; } }
    else { ++dev->statistics.errors; ++SRNOS_SPI_DBG.xfer_error; }
    if (status==SRNOS_SPI_TIMEOUT) { ++dev->statistics.timeouts; ++SRNOS_SPI_DBG.xfer_timeout; }
    r->result=status;
    if (bus.active==r) bus.active=NULL;
    SRNOS_SPI_DBG.state=0; SRNOS_SPI_DBG.current_dev=0; SRNOS_SPI_DBG.lock_held=0; SRNOS_SPI_DBG.h.last_status=status;
    if(r->synchronous) {
        r->state=SRNOS_SPI_SLOT_DONE; bus.callback_dev=dev; leave(m);
        if(cb) cb(dev,ctx,status);
        bus.callback_dev=NULL;
        (void)osSemaphoreRelease(r->done);
    }
    else {
        /* Free before callback so chained BMI transactions can use this slot.
         * No RX access is possible after completion notification. */
        bus.callback_dev=dev;
        r->state=SRNOS_SPI_SLOT_FREE;
        leave(m);
        if(cb) cb(dev,ctx,status);
        bus.callback_dev=NULL;
    }
}
static bool abort_active(void)
{
    bus.accepting_irq=false;
    ++SRNOS_SPI_DBG.recover_count;
    if(bus.active) ++bus.active->dev->statistics.recoveries;
    bool ok=SRNOS_BoardSPIAbort(bus.hspi, bus.tx, bus.rx);
    bus.completion=0;
    if(!ok) { bus.faulted=true; ++SRNOS_SPI_DBG.recover_error; }
    /* Board helper only returns true after DMA, SPI and pending IRQs quiesce.
     * A failed abort quarantines the bus; no other CS is ever asserted. */
    if(ok && bus.active) HAL_GPIO_WritePin(bus.active->dev->cs_port,bus.active->dev->cs_pin,GPIO_PIN_SET);
    return ok;
}
static SRNOS_SPIRequest *oldest(unsigned first, unsigned end, bool can)
{
    SRNOS_SPIRequest *r=NULL;
    for(unsigned i=first;i<end;i++) {
        SRNOS_SPIRequest *p=&bus.slots[i];
        if(p->state==SRNOS_SPI_SLOT_QUEUED && !!p->dev->can_priority==can &&
           (!r || (int32_t)(p->order-r->order)<0)) r=p;
    }
    return r;
}
/* One bounded dispatch pass, also driven directly by the host HAL simulator. */
static void service_once(void)
{
    uint64_t now=SRNOS_TimeGetUptimeUs();
    SRNOS_SPIRequest *r=bus.active;
    if(r) {
        unsigned completion=bus.completion;
        if(completion || r->cancel || bus.recover_requested || now-r->started>=5000 || (r->prepare && now-r->submitted>=r->deadline_us)) {
            SRNOS_SPIStatus status=SRNOS_SPI_OK;
            bool abort_needed=completion!=1 || r->cancel || bus.recover_requested;
            if(abort_needed) {
                status=r->cancel?SRNOS_SPI_CANCELLED:completion==2?SRNOS_SPI_ERROR:SRNOS_SPI_TIMEOUT;
                (void)abort_active();
            } else {
                bus.accepting_irq=false;
                HAL_GPIO_WritePin(r->dev->cs_port,r->dev->cs_pin,GPIO_PIN_SET);
                if(r->rx) memcpy(r->rx,rx_stage,r->len);
            }
            uint32_t elapsed=(uint32_t)(SRNOS_TimeGetUptimeUs()-r->started);
            histogram(r->dev->statistics.transfer_us,elapsed);
            if(elapsed>r->dev->statistics.max_transfer_us) r->dev->statistics.max_transfer_us=elapsed;
            bus.completion=0; bus.recover_requested=false;
            if(status==SRNOS_SPI_OK && r->prepare) {
                /* Each physical transfer retains its own statistics and CS
                 * boundary. Requeue before selecting the next owner; never
                 * execute an entire sequence as one bus reservation. */
                ++r->dev->statistics.completed; ++SRNOS_SPI_DBG.xfer_done;
                bus.active=NULL;
                ++r->sequence_completed;
                int len=r->prepare(r->ctx,r->sequence_completed,r->tx,&r->rx);
                if(len==0) finish(r,SRNOS_SPI_OK,false);
                else if(len<0 || len>(int)SRNOS_SPI_SEQUENCE_MAX_BYTES ||
                        r->sequence_completed>=SRNOS_SPI_SEQUENCE_MAX_STEPS)
                    finish(r,SRNOS_SPI_INVALID_PARAM,false);
                else {
                    r->len=(size_t)len;
                    uint32_t key=enter();
                    r->order=++bus.order; r->enqueued=SRNOS_TimeGetUptimeUs();
                    r->state=SRNOS_SPI_SLOT_QUEUED; ++r->dev->statistics.accepted;
                    leave(key);
                }
            } else finish(r,status,true);
        }
    } else bus.recover_requested=false;
    now=SRNOS_TimeGetUptimeUs();
    for(unsigned i=0;i<8;i++) {
        r=&bus.slots[i];
        if(r->state==SRNOS_SPI_SLOT_QUEUED && (r->cancel || bus.faulted || now-r->submitted>=r->deadline_us)) {
            if(!r->cancel && !bus.faulted) ++r->dev->statistics.queue_expired;
            finish(r,r->cancel?SRNOS_SPI_CANCELLED:bus.faulted?SRNOS_SPI_ERROR:SRNOS_SPI_TIMEOUT,false);
        }
    }
    if(bus.active || bus.faulted) return;
    uint32_t m=enter();
    SRNOS_SPIRequest *imu=oldest(0,4,false), *normal=oldest(4,8,false), *can=oldest(4,8,true);
    /* IMU owns the next boundary, with at most four transactions before one
     * waiting lower-class request. Within that lower class CAN has priority,
     * with at most four CAN transactions before one waiting ordinary request.
     * Keep the four reserved IMU slots and four shared slots unchanged.
     * A running DMA is never preempted and all existing deadlines still apply. */
    SRNOS_SPIRequest *lower=can && (bus.can_run<4 || !normal)?can:normal;
    r=imu && (bus.imu_run<4 || !lower)?imu:lower;
    if(!r) { leave(m); return; }
    if(r==imu) { if(bus.imu_run<4) ++bus.imu_run; }
    else {
        bus.imu_run=0;
        if(r==can) { if(bus.can_run<4) ++bus.can_run; } else bus.can_run=0;
    }
    r->state=SRNOS_SPI_SLOT_ACTIVE; bus.active=r; r->started=SRNOS_TimeGetUptimeUs();
    uint64_t queue_elapsed=r->started-r->enqueued;
    uint32_t wait=queue_elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)queue_elapsed;
    histogram(r->dev->statistics.queue_us,wait);
    if(wait>r->dev->statistics.max_queue_us) r->dev->statistics.max_queue_us=wait;
    leave(m);
    memcpy(tx_stage,r->tx,r->len);
    SRNOS_SPI_DBG.state=1; SRNOS_SPI_DBG.lock_held=1; SRNOS_SPI_DBG.current_dev=(uintptr_t)r->dev;
    SRNOS_SPI_DBG.last_len=r->len; ++SRNOS_SPI_DBG.xfer_start;
    if(!SRNOS_BoardSPISelectMode(bus.hspi,r->dev->mode)) { bus.completion=2; wake(); return; }
    bus.completion=0; bus.accepting_irq=true;
    HAL_GPIO_WritePin(r->dev->cs_port,r->dev->cs_pin,GPIO_PIN_RESET);
    /* Completion can arrive before HAL returns; worker serializes publication. */
    HAL_StatusTypeDef st=HAL_SPI_TransmitReceive_DMA(bus.hspi,tx_stage,rx_stage,(uint16_t)r->len);
    SRNOS_SPI_DBG.last_hal_ret=st;
    if(st!=HAL_OK) { bus.completion=2; wake(); }
}
static void dispatch_step(void *arg) {(void)arg;service_once();}
static uint32_t next_wait(void)
{
    uint64_t now=SRNOS_TimeGetUptimeUs(), deadline=UINT64_MAX;
    uint32_t m=enter();
    if(bus.completion || bus.recover_requested) { leave(m);return 0; }
    for(unsigned i=0;i<8;i++) {
        SRNOS_SPIRequest *r=&bus.slots[i];
        if(r->state==SRNOS_SPI_SLOT_ACTIVE) {
            uint64_t end=r->started+5000;
            if(r->prepare && r->submitted+r->deadline_us<end) end=r->submitted+r->deadline_us;
            if(r->cancel) { leave(m);return 0; }
            if(end<deadline)deadline=end;
        } else if(r->state==SRNOS_SPI_SLOT_QUEUED) {
            if(!bus.active || r->cancel || bus.faulted) { leave(m);return 0; }
            uint64_t end=r->submitted+r->deadline_us;
            if(end<deadline)deadline=end;
        }
    }
    leave(m);
    if(deadline==UINT64_MAX)return osWaitForever;
    if(now>=deadline)return 0;
    uint64_t delay=((deadline-now)*osKernelGetTickFreq()+999999)/1000000;
    return delay<osWaitForever?(uint32_t)delay:osWaitForever-1;
}
static bool dispatch_wait(void *arg) {
    (void)arg;uint32_t delay=next_wait();
    if(delay) { uint32_t flags; (void)xTaskNotifyWait(0,1,&flags,delay); }
    return true;
}
static void dispatch(void *argument) {
    (void)argument;const SRNOS_InternalLoop loop={dispatch_step,dispatch_wait,NULL,0,false};
    SRNOS_KernelRunLoop(&loop);
}
void SRNOS_SPIInitPre(void)
{
    memset(&bus,0,sizeof(bus));
    SRNOS_BoardSPIHandles(0,&bus.hspi,&bus.tx,&bus.rx);
    SRNOS_TimeInit(0);
    SRNOS_DbgZero(&SRNOS_SPI_DBG,sizeof(SRNOS_SPI_DBG));
    SRNOS_DbgInit(&SRNOS_SPI_DBG.h,SRNOS_DBG_MODULE_SPI,0,sizeof(SRNOS_SPI_DBG));
}
void SRNOS_SPIInitRtos(void)
{
    for(unsigned i=0;i<8;i++) {
        bus.slots[i].done=osSemaphoreNew(1,0,NULL);
        configASSERT(bus.slots[i].done);
    }
    const osThreadAttr_t attr={.name="SRNOS_SPI4",.stack_size=3072,.priority=osPriorityRealtime1};
    worker=SRNOS_KernelThreadNew(dispatch,NULL,&attr); configASSERT(worker);
}
SRNOS_SPIStatus SRNOS_SPIDeviceInit(SRNOS_SPIDevice *d,SRNOS_SPIBusId id,GPIO_TypeDef *cs,uint16_t pin)
{
    if(!d || !cs || id!=SRNOS_SPI_BUS_4 || !bus.hspi) return SRNOS_SPI_INVALID_PARAM;
    if(bus.callback_dev==d) return SRNOS_SPI_BUSY;
    /* Device descriptors have startup/static lifetime; reinit only when idle. */
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state!=SRNOS_SPI_SLOT_FREE && bus.slots[i].dev==d) return SRNOS_SPI_BUSY;
    d->bus=&bus; d->cs_port=cs; d->cs_pin=pin; d->max_speed_hz=10000000;
    d->mode=0; d->bits_per_word=8; d->imu_priority=0; d->can_priority=0;
    HAL_GPIO_WritePin(cs,pin,GPIO_PIN_SET); return SRNOS_SPI_OK;
}
static SRNOS_SPIStatus transfer(SRNOS_SPIDevice *d,const uint8_t *tx,uint8_t *rx,size_t len,
    SRNOS_SPICallback cb,void *ctx,uint8_t blocking,uint32_t timeout_ms,SRNOS_SPISequencePrepare prepare)
{
    if(!d || d->bus!=&bus || (d->imu_priority && d->can_priority) || !len || len>SRNOS_SPI_DMA_STAGING_SIZE || (!tx&&!rx) ||
       (timeout_ms>INT32_MAX && timeout_ms!=SRNOS_WAIT_FOREVER) || __get_IPSR()) return SRNOS_SPI_INVALID_PARAM;
    if(!worker || osKernelGetState()!=osKernelRunning || (blocking && osThreadGetId()==worker)) return SRNOS_SPI_INVALID_PARAM;
    if(timeout_ms==SRNOS_NO_WAIT) return SRNOS_SPI_TIMEOUT;
    uint32_t m=enter();
    if(bus.faulted) { leave(m); return SRNOS_SPI_ERROR; }
    SRNOS_SPIRequest *r=NULL;
    unsigned first=d->imu_priority?0:4;
    for(unsigned i=first;i<first+4;i++) if(bus.slots[i].state==SRNOS_SPI_SLOT_FREE) { r=&bus.slots[i]; break; }
    if(!r) { ++d->statistics.queue_full; ++SRNOS_SPI_DBG.xfer_busy; leave(m); return SRNOS_SPI_BUSY; }
    r->dev=d; r->rx=rx; r->len=len; r->cb=cb; r->ctx=ctx;
    r->synchronous=blocking!=0; r->cancel=false;
    r->prepare=prepare; r->sequence_completed=0;
    r->order=++bus.order; r->submitted=r->enqueued=SRNOS_TimeGetUptimeUs();
    r->deadline_us=d->imu_priority?1000U:(uint64_t)(timeout_ms==SRNOS_WAIT_FOREVER?5U:timeout_ms)*1000U;
    /* RESERVED cannot be dispatched or recycled while the caller copies. */
    r->state=SRNOS_SPI_SLOT_RESERVED;
    leave(m);
    if(tx) memcpy(r->tx,tx,len); else memset(r->tx,0xff,len);
    m=enter();
    r->state=SRNOS_SPI_SLOT_QUEUED; ++d->statistics.accepted;
    leave(m); wake();
    if(!blocking) return SRNOS_SPI_OK;
    /* Admission/copy and queueing share one caller budget. Preemption during
     * the unmasked copy must not grant another full timeout after publication.
     * On timeout retain RX ownership until cancellation is acknowledged. */
    uint64_t elapsed=SRNOS_TimeGetUptimeUs()-r->submitted;
    uint64_t budget_us=(uint64_t)timeout_ms*1000U;
    uint32_t remaining=timeout_ms==SRNOS_WAIT_FOREVER?osWaitForever:
        elapsed<budget_us?ticks_us(budget_us-elapsed):0;
    if(osSemaphoreAcquire(r->done,remaining)!=osOK) {
        m=enter(); if(r->state!=SRNOS_SPI_SLOT_DONE) r->cancel=true; leave(m); wake();
        (void)osSemaphoreAcquire(r->done,osWaitForever);
    }
    m=enter(); SRNOS_SPIStatus result=r->result; r->state=SRNOS_SPI_SLOT_FREE; leave(m);
    return result;
}
SRNOS_SPIStatus SRNOS_SPITransferEx(SRNOS_SPIDevice *d,const uint8_t *tx,uint8_t *rx,size_t len,
    SRNOS_SPICallback cb,void *ctx,uint8_t blocking,uint32_t timeout_ms)
{
    return transfer(d,tx,rx,len,cb,ctx,blocking,timeout_ms,NULL);
}
SRNOS_SPIStatus SRNOS_SPITransferSequence(SRNOS_SPIDevice *d,SRNOS_SPISequencePrepare prepare,void *ctx)
{
    if(!prepare || __get_IPSR() || osThreadGetId()==worker) return SRNOS_SPI_INVALID_PARAM;
    uint8_t tx[SRNOS_SPI_SEQUENCE_MAX_BYTES], *rx=NULL;
    int len=prepare(ctx,0,tx,&rx);
    if(len<=0 || len>(int)SRNOS_SPI_SEQUENCE_MAX_BYTES) return SRNOS_SPI_INVALID_PARAM;
    return transfer(d,tx,rx,(size_t)len,NULL,ctx,1,5,prepare);
}
SRNOS_SPIStatus SRNOS_SPITransferDMA(SRNOS_SPIDevice*d,const uint8_t*t,uint8_t*r,size_t n,SRNOS_SPICallback c,void*x) { return SRNOS_SPITransferEx(d,t,r,n,c,x,0,5); }
SRNOS_SPIStatus SRNOS_SPITransferDMABlocking(SRNOS_SPIDevice*d,const uint8_t*t,uint8_t*r,size_t n,uint32_t ms) { return SRNOS_SPITransferEx(d,t,r,n,NULL,NULL,1,ms); }
SRNOS_SPIStatus SRNOS_SPIIsBusBusy(const SRNOS_SPIDevice *d)
{
    if(!d || d->bus!=&bus) return SRNOS_SPI_INVALID_PARAM;
    uint32_t m=enter();
    SRNOS_SPIStatus result=bus.faulted?SRNOS_SPI_ERROR:SRNOS_SPI_OK;
    for(unsigned i=0;result==SRNOS_SPI_OK && i<8;i++) {
        unsigned state=bus.slots[i].state;
        if(state==SRNOS_SPI_SLOT_RESERVED || state==SRNOS_SPI_SLOT_QUEUED || state==SRNOS_SPI_SLOT_ACTIVE) result=SRNOS_SPI_BUSY;
    }
    leave(m); return result;
}
SRNOS_SPIStatus SRNOS_SPICancel(SRNOS_SPIDevice *d)
{
    if(!d || d->bus!=&bus || __get_IPSR() || osThreadGetId()==worker) return SRNOS_SPI_INVALID_PARAM;
    uint32_t m=enter(); bool pending=bus.callback_dev==d;
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state!=SRNOS_SPI_SLOT_FREE && bus.slots[i].state!=SRNOS_SPI_SLOT_DONE && bus.slots[i].dev==d) { bus.slots[i].cancel=true; pending=true; }
    leave(m); if(pending) wake();
    /* Nonblocking: BUSY means buffers still owned. Retry until OK. */
    return pending?SRNOS_SPI_BUSY:SRNOS_SPI_OK;
}
SRNOS_SPIStatus SRNOS_SPIRecoverBus(SRNOS_SPIBusId id,uint32_t reason)
{
    if(id!=SRNOS_SPI_BUS_4 || __get_IPSR()) return SRNOS_SPI_INVALID_PARAM;
    uint32_t m=enter();
    SRNOS_SPI_DBG.last_recover_reason=reason;
    if(bus.faulted) { leave(m);return SRNOS_SPI_ERROR; }
    if(!bus.active) { leave(m);return SRNOS_SPI_OK; }
    bus.recover_requested=true; leave(m);wake(); return SRNOS_SPI_BUSY;
}
void SRNOS_SPIGetStatistics(const SRNOS_SPIDevice *d,SRNOS_SPIStatistics *out) { if(d&&out) { uint32_t m=enter(); *out=d->statistics; leave(m); } }
SRNOS_SPIStatus SRNOS_SPIGetSnapshot(SRNOS_SPIBusId id,SRNOS_SPISnapshot *out)
{
    if(id!=0 || !out) return SRNOS_SPI_INVALID_PARAM;
    uint32_t m=enter(); memset(out,0,sizeof(*out));
    out->state=bus.faulted?3:bus.active?1:0; out->hal_state=HAL_SPI_GetState(bus.hspi);
    out->hal_error=HAL_SPI_GetError(bus.hspi); out->lock_held=bus.active!=NULL;
    if(bus.active) { out->current_dev=(uintptr_t)bus.active->dev; out->current_len=bus.active->len; }
    out->last_status=SRNOS_SPI_DBG.h.last_status; out->last_tick=HAL_GetTick(); out->recover_count=SRNOS_SPI_DBG.recover_count;
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state==SRNOS_SPI_SLOT_QUEUED) ++out->cb_queued;
    out->spurious_complete=SRNOS_SPI_DBG.spurious_complete; out->last_hal_ret=SRNOS_SPI_DBG.last_hal_ret;
    leave(m); return SRNOS_SPI_OK;
}
static void completion(SPI_HandleTypeDef *h,unsigned result)
{
    if(h!=bus.hspi) return;
    if(!bus.accepting_irq || !bus.active || bus.completion || (result==1 && HAL_SPI_GetState(h)!=HAL_SPI_STATE_READY)) { ++SRNOS_SPI_DBG.spurious_complete; return; }
    bus.completion=result; wake();
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h) { completion(h,2); }
