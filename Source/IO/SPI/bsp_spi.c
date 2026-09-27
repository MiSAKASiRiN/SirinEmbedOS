/* SPI4 has one owner: this dispatcher. Callers never touch HAL or CS. */
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "fixed_buses.h"
#include <string.h>

enum { FREE, QUEUED, ACTIVE, DONE };
typedef struct {
    unsigned state;
    uint32_t order;
    uint64_t enqueued, started;
    uint32_t deadline_us;
    bsp_spi_device_t *dev;
    bsp_spi_callback_t cb;
    void *ctx;
    uint8_t tx[BSP_SPI_DMA_STAGING_SIZE];
    uint8_t *rx;
    size_t len;
    bool synchronous, cancel;
    bsp_spi_status_t result;
    osSemaphoreId_t done;
} request_t;
struct bsp_spi_bus {
    SPI_HandleTypeDef *hspi;
    DMA_HandleTypeDef *tx, *rx;
    request_t slots[8];
    request_t *active;
    bsp_spi_device_t *callback_dev;
    uint32_t order, imu_run;
    volatile unsigned completion;
    volatile bool accepting_irq, recover_requested;
    bool faulted;
};
static struct bsp_spi_bus bus;
static osThreadId_t worker;
static uint8_t tx_stage[BSP_SPI_DMA_STAGING_SIZE] BSP_SPI_DMA_SEC;
static uint8_t rx_stage[BSP_SPI_DMA_STAGING_SIZE] BSP_SPI_DMA_SEC;
OMX_DEBUG_WATCH_BSS volatile omx_dbg_spi_t g_omx_dbg_spi[BSP_SPI_BUS_MAX];
#define DBG g_omx_dbg_spi[0]
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static uint32_t ticks(uint32_t ms) { return (uint32_t)(((uint64_t)ms*osKernelGetTickFreq()+999)/1000); }
static void wake(void) { if(worker) (void)osThreadFlagsSet(worker,1); }
static void histogram(uint32_t bins[6], uint32_t us) {
    unsigned i=us<=25?0:us<=100?1:us<=250?2:us<=500?3:us<=1000?4:5;
    ++bins[i];
}
static void finish(request_t *r, bsp_spi_status_t status)
{
    uint32_t m=enter();
    bsp_spi_device_t *dev=r->dev;
    bsp_spi_callback_t cb=r->cb;
    void *ctx=r->ctx;
    if (status==BSP_SPI_OK) { ++dev->statistics.completed; ++DBG.xfer_done; }
    else { ++dev->statistics.errors; ++DBG.xfer_error; }
    if (status==BSP_SPI_TIMEOUT) { ++dev->statistics.timeouts; ++DBG.xfer_timeout; }
    r->result=status;
    if (bus.active==r) bus.active=NULL;
    DBG.state=0; DBG.current_dev=0; DBG.lock_held=0; DBG.h.last_status=status;
    if(r->synchronous) {
        r->state=DONE; bus.callback_dev=dev; leave(m);
        if(cb) cb(dev,ctx,status);
        bus.callback_dev=NULL;
        (void)osSemaphoreRelease(r->done);
    }
    else {
        /* Free before callback so chained BMI transactions can use this slot.
         * No RX access is possible after completion notification. */
        bus.callback_dev=dev;
        r->state=FREE;
        leave(m);
        if(cb) cb(dev,ctx,status);
        bus.callback_dev=NULL;
    }
}
static bool abort_active(void)
{
    bus.accepting_irq=false;
    ++DBG.recover_count;
    if(bus.active) ++bus.active->dev->statistics.recoveries;
    bool ok=SRNOS_BoardSPIAbort(bus.hspi, bus.tx, bus.rx);
    bus.completion=0;
    if(!ok) { bus.faulted=true; ++DBG.recover_error; }
    /* Board helper only returns true after DMA, SPI and pending IRQs quiesce.
     * A failed abort quarantines the bus; no other CS is ever asserted. */
    if(ok && bus.active) HAL_GPIO_WritePin(bus.active->dev->cs_port,bus.active->dev->cs_pin,GPIO_PIN_SET);
    return ok;
}
static request_t *oldest(unsigned first, unsigned end)
{
    request_t *r=NULL;
    for(unsigned i=first;i<end;i++) {
        request_t *p=&bus.slots[i];
        if(p->state==QUEUED && (!r || (int32_t)(p->order-r->order)<0)) r=p;
    }
    return r;
}
/* One bounded dispatch pass, also driven directly by the host HAL simulator. */
static void service_once(void)
{
    uint64_t now=DWT_GetUptimeUs();
    request_t *r=bus.active;
    if(r) {
        unsigned completion=bus.completion;
        if(completion || r->cancel || bus.recover_requested || now-r->started>=5000) {
            bsp_spi_status_t status=BSP_SPI_OK;
            bool abort_needed=completion!=1 || r->cancel || bus.recover_requested;
            if(abort_needed) {
                status=r->cancel?BSP_SPI_CANCELLED:completion==2?BSP_SPI_ERROR:BSP_SPI_TIMEOUT;
                (void)abort_active();
            } else {
                bus.accepting_irq=false;
                HAL_GPIO_WritePin(r->dev->cs_port,r->dev->cs_pin,GPIO_PIN_SET);
                if(r->rx) memcpy(r->rx,rx_stage,r->len);
            }
            uint32_t elapsed=(uint32_t)(DWT_GetUptimeUs()-r->started);
            histogram(r->dev->statistics.transfer_us,elapsed);
            if(elapsed>r->dev->statistics.max_transfer_us) r->dev->statistics.max_transfer_us=elapsed;
            bus.completion=0; bus.recover_requested=false;
            finish(r,status);
        }
    } else bus.recover_requested=false;
    for(unsigned i=0;i<8;i++) {
        r=&bus.slots[i];
        if(r->state==QUEUED && (r->cancel || bus.faulted || DWT_GetUptimeUs()-r->enqueued>=r->deadline_us)) {
            if(!r->cancel && !bus.faulted) ++r->dev->statistics.queue_expired;
            finish(r,r->cancel?BSP_SPI_CANCELLED:bus.faulted?BSP_SPI_ERROR:BSP_SPI_TIMEOUT);
        }
    }
    if(bus.active || bus.faulted) return;
    uint32_t m=enter();
    request_t *imu=oldest(0,4), *normal=oldest(4,8);
    r=imu && (bus.imu_run<4 || !normal)?imu:normal;
    if(!r) { leave(m); return; }
    if(r==imu) { if(bus.imu_run<4) ++bus.imu_run; } else bus.imu_run=0;
    r->state=ACTIVE; bus.active=r; r->started=DWT_GetUptimeUs();
    uint32_t wait=(uint32_t)(r->started-r->enqueued);
    histogram(r->dev->statistics.queue_us,wait);
    if(wait>r->dev->statistics.max_queue_us) r->dev->statistics.max_queue_us=wait;
    leave(m);
    memcpy(tx_stage,r->tx,r->len);
    DBG.state=1; DBG.lock_held=1; DBG.current_dev=(uintptr_t)r->dev;
    DBG.last_len=r->len; ++DBG.xfer_start;
    if(!SRNOS_BoardSPISelectMode(bus.hspi,r->dev->mode)) { bus.completion=2; wake(); return; }
    bus.completion=0; bus.accepting_irq=true;
    HAL_GPIO_WritePin(r->dev->cs_port,r->dev->cs_pin,GPIO_PIN_RESET);
    /* Completion can arrive before HAL returns; worker serializes publication. */
    HAL_StatusTypeDef st=HAL_SPI_TransmitReceive_DMA(bus.hspi,tx_stage,rx_stage,(uint16_t)r->len);
    DBG.last_hal_ret=st;
    if(st!=HAL_OK) { bus.completion=2; wake(); }
}
static void dispatch(void *arg)
{
    (void)arg;
    for(;;) { service_once(); (void)osThreadFlagsWait(1,osFlagsWaitAny,1); }
}
void bsp_spi_init_pre(void)
{
    memset(&bus,0,sizeof(bus));
    SRNOS_BoardSPIHandles(0,&bus.hspi,&bus.tx,&bus.rx);
    DWT_Init(0);
    omx_dbg_zero(&DBG,sizeof(DBG));
    omx_dbg_init(&DBG.h,OMX_DBG_MODULE_SPI,0,sizeof(DBG));
}
void bsp_spi_init_rtos(void)
{
    for(unsigned i=0;i<8;i++) {
        bus.slots[i].done=osSemaphoreNew(1,0,NULL);
        configASSERT(bus.slots[i].done);
    }
    const osThreadAttr_t attr={.name="bsp_spi_cb",.stack_size=3072,.priority=osPriorityHigh6};
    worker=osThreadNew(dispatch,NULL,&attr); configASSERT(worker);
}
bsp_spi_status_t bsp_spi_device_init(bsp_spi_device_t *d,bsp_spi_bus_id_t id,GPIO_TypeDef *cs,uint16_t pin)
{
    if(!d || !cs || id!=BSP_SPI_BUS_4 || !bus.hspi) return BSP_SPI_INVALID_PARAM;
    if(bus.callback_dev==d) return BSP_SPI_BUSY;
    /* Device descriptors have startup/static lifetime; reinit only when idle. */
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state!=FREE && bus.slots[i].dev==d) return BSP_SPI_BUSY;
    d->bus=&bus; d->cs_port=cs; d->cs_pin=pin; d->max_speed_hz=10000000;
    d->mode=0; d->bits_per_word=8; d->imu_priority=0;
    HAL_GPIO_WritePin(cs,pin,GPIO_PIN_SET); return BSP_SPI_OK;
}
bsp_spi_status_t bsp_spi_transfer_ex(bsp_spi_device_t *d,const uint8_t *tx,uint8_t *rx,size_t len,
    bsp_spi_callback_t cb,void *ctx,uint8_t blocking,uint32_t timeout_ms)
{
    if(!d || d->bus!=&bus || !len || len>BSP_SPI_DMA_STAGING_SIZE || (!tx&&!rx) || timeout_ms>60000 || __get_IPSR()) return BSP_SPI_INVALID_PARAM;
    if(!worker || osKernelGetState()!=osKernelRunning || (blocking && osThreadGetId()==worker)) return BSP_SPI_INVALID_PARAM;
    uint32_t m=enter();
    if(bus.faulted) { leave(m); return BSP_SPI_ERROR; }
    request_t *r=NULL;
    unsigned first=d->imu_priority?0:4;
    for(unsigned i=first;i<first+4;i++) if(bus.slots[i].state==FREE) { r=&bus.slots[i]; break; }
    if(!r) { ++d->statistics.queue_full; ++DBG.xfer_busy; leave(m); return BSP_SPI_BUSY; }
    r->dev=d; r->rx=rx; r->len=len; r->cb=cb; r->ctx=ctx;
    r->synchronous=blocking!=0; r->cancel=false;
    r->order=++bus.order; r->enqueued=DWT_GetUptimeUs();
    r->deadline_us=d->imu_priority?1000:(timeout_ms?timeout_ms:5)*1000;
    if(tx) memcpy(r->tx,tx,len); else memset(r->tx,0xff,len);
    r->state=QUEUED; ++d->statistics.accepted;
    leave(m); wake();
    if(!blocking) return BSP_SPI_OK;
    /* On timeout retain RX ownership until dispatcher acknowledges cancellation. */
    if(osSemaphoreAcquire(r->done,ticks(timeout_ms?timeout_ms:5))!=osOK) {
        m=enter(); if(r->state!=DONE) r->cancel=true; leave(m); wake();
        (void)osSemaphoreAcquire(r->done,osWaitForever);
    }
    m=enter(); bsp_spi_status_t result=r->result; r->state=FREE; leave(m);
    return result;
}
bsp_spi_status_t bsp_spi_transfer_dma(bsp_spi_device_t*d,const uint8_t*t,uint8_t*r,size_t n,bsp_spi_callback_t c,void*x) { return bsp_spi_transfer_ex(d,t,r,n,c,x,0,5); }
bsp_spi_status_t bsp_spi_transfer_dma_blocking(bsp_spi_device_t*d,const uint8_t*t,uint8_t*r,size_t n,uint32_t ms) { return bsp_spi_transfer_ex(d,t,r,n,NULL,NULL,1,ms); }
/* Former direct names now obey the same bounded copy/ownership contract. */
bsp_spi_status_t bsp_spi_transfer_dma_direct(bsp_spi_device_t*d,const uint8_t*t,uint8_t*r,size_t n,bsp_spi_callback_t c,void*x) { return bsp_spi_transfer_dma(d,t,r,n,c,x); }
bsp_spi_status_t bsp_spi_transfer_direct_ex(bsp_spi_device_t*d,const uint8_t*t,uint8_t*r,size_t n,bsp_spi_callback_t c,void*x,uint8_t b,uint32_t ms) { return bsp_spi_transfer_ex(d,t,r,n,c,x,b,ms); }
bsp_spi_status_t bsp_spi_is_bus_busy(const bsp_spi_device_t *d) { return !d || !d->bus ? BSP_SPI_INVALID_PARAM : bus.faulted ? BSP_SPI_ERROR : (bus.active || oldest(0,8)) ? BSP_SPI_BUSY : BSP_SPI_OK; }
bsp_spi_status_t bsp_spi_cancel(bsp_spi_device_t *d)
{
    if(!d || d->bus!=&bus || __get_IPSR() || osThreadGetId()==worker) return BSP_SPI_INVALID_PARAM;
    uint32_t m=enter(); bool pending=bus.callback_dev==d;
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state!=FREE && bus.slots[i].state!=DONE && bus.slots[i].dev==d) { bus.slots[i].cancel=true; pending=true; }
    leave(m); if(pending) wake();
    /* Nonblocking: BUSY means buffers still owned. Retry until OK. */
    return pending?BSP_SPI_BUSY:BSP_SPI_OK;
}
bsp_spi_status_t bsp_spi_recover_bus(bsp_spi_bus_id_t id,uint32_t reason)
{
    if(id!=BSP_SPI_BUS_4 || __get_IPSR()) return BSP_SPI_INVALID_PARAM;
    DBG.last_recover_reason=reason;
    if(bus.faulted) return BSP_SPI_ERROR;
    if(!bus.active) return BSP_SPI_OK;
    bus.recover_requested=true; wake(); return BSP_SPI_BUSY;
}
void bsp_spi_get_statistics(const bsp_spi_device_t *d,bsp_spi_statistics_t *out) { if(d&&out) { uint32_t m=enter(); *out=d->statistics; leave(m); } }
bsp_spi_status_t bsp_spi_get_snapshot(bsp_spi_bus_id_t id,bsp_spi_snapshot_t *out)
{
    if(id!=0 || !out) return BSP_SPI_INVALID_PARAM;
    uint32_t m=enter(); memset(out,0,sizeof(*out));
    out->state=bus.faulted?3:bus.active?1:0; out->hal_state=HAL_SPI_GetState(bus.hspi);
    out->hal_error=HAL_SPI_GetError(bus.hspi); out->lock_held=bus.active!=NULL;
    if(bus.active) { out->current_dev=(uintptr_t)bus.active->dev; out->current_len=bus.active->len; }
    out->last_status=DBG.h.last_status; out->last_tick=HAL_GetTick(); out->recover_count=DBG.recover_count;
    for(unsigned i=0;i<8;i++) if(bus.slots[i].state==QUEUED) ++out->cb_queued;
    out->spurious_complete=DBG.spurious_complete; out->last_hal_ret=DBG.last_hal_ret;
    leave(m); return BSP_SPI_OK;
}
static void completion(SPI_HandleTypeDef *h,unsigned result)
{
    if(h!=bus.hspi) return;
    if(!bus.accepting_irq || !bus.active || bus.completion || (result==1 && HAL_SPI_GetState(h)!=HAL_SPI_STATE_READY)) { ++DBG.spurious_complete; return; }
    bus.completion=result; wake();
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h) { completion(h,1); }
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h) { completion(h,2); }
