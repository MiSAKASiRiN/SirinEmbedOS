#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../Source/IO/SPI/SPI.c"
uint32_t SRNOS_TestPrimask;
static bool inject_reservation;
static unsigned reservation_preemptions;
static uint64_t clock_us;
static uint32_t reservation_delay_us;
void SRNOS_TestPreempt(void) {
    if(!inject_reservation)return;
    for(unsigned i=0;i<8;i++)if(bus.slots[i].state==SRNOS_SPI_SLOT_RESERVED) {
        inject_reservation=false;
        SRNOS_SPIRequest *r=&bus.slots[i];
        assert(!SRNOS_TestPrimask);
        assert(SRNOS_SPIIsBusBusy(r->dev)==SRNOS_SPI_BUSY);
        assert(SRNOS_SPIRecoverBus(SRNOS_SPI_BUS_4,SRNOS_SPI_RECOVER_REASON_USER)==SRNOS_SPI_OK);
        assert(r->state==SRNOS_SPI_SLOT_RESERVED && !r->cancel);
        if(reservation_delay_us) {
            clock_us+=reservation_delay_us;
            return;
        }
        assert(SRNOS_SPICancel(r->dev)==SRNOS_SPI_BUSY);
        service_once();
        assert(r->state==SRNOS_SPI_SLOT_RESERVED && r->cancel && !bus.active);
        ++reservation_preemptions;
        return;
    }
}
static SPI_HandleTypeDef hw;
static DMA_HandleTypeDef txdma,rxdma;
static GPIO_TypeDef gpio;
static unsigned selected, launches, completions, aborts, callbacks;
static bool abort_ok=true, auto_complete=true, early_complete=false;
static bool complete_on_wait;
static uint32_t pending_flags, waited_ticks, observed_flags;
static int semaphores[8], sem_count;
static uint8_t observed_tx[512];
static size_t observed_len;
static SRNOS_SPIStatus callback_result;
void SRNOS_TimeInit(uint32_t x) { (void)x; }
uint64_t SRNOS_TimeGetUptimeUs(void) { return clock_us; }
uint32_t HAL_GetTick(void) { return clock_us/1000; }
void SRNOS_BoardSPIHandles(uint8_t id,SPI_HandleTypeDef **h,DMA_HandleTypeDef **t,DMA_HandleTypeDef **r) { (void)id; *h=&hw; *t=&txdma; *r=&rxdma; }
bool SRNOS_BoardSPISelectMode(SPI_HandleTypeDef *h,uint8_t mode) { (void)mode; assert(!selected); return h->State==HAL_SPI_STATE_READY; }
bool SRNOS_BoardSPIAbort(SPI_HandleTypeDef *h,DMA_HandleTypeDef *t,DMA_HandleTypeDef *r) { (void)t;(void)r; ++aborts; if(abort_ok) h->State=HAL_SPI_STATE_READY; return abort_ok; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,int value) {
    (void)p;
    if(!value) { assert(!selected); selected=pin; } else if(selected==pin) selected=0;
}
static void complete(void) { assert(bus.active); hw.State=HAL_SPI_STATE_READY; ++completions; HAL_SPI_TxRxCpltCallback(&hw); }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *h,uint8_t *t,uint8_t *r,uint16_t n) {
    assert(selected); ++launches; observed_len=n; memcpy(observed_tx,t,n);
    for(unsigned i=0;i<n;i++) r[i]=t[i]^0x55;
    h->State=HAL_SPI_STATE_BUSY;
    if(early_complete) complete();
    return HAL_OK;
}
uint32_t osKernelGetTickFreq(void) { return 1000; }
int osKernelGetState(void) { return osKernelRunning; }
osThreadId_t osThreadGetId(void) { return (void*)2; }
osThreadId_t osThreadNew(void(*f)(void*),void*a,const osThreadAttr_t*b) { (void)f;(void)a;(void)b; return (void*)1; }
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) { (void)t;return pending_flags|=f; }
uint32_t osThreadFlagsWait(uint32_t a,uint32_t b,uint32_t c) {
    (void)b; waited_ticks=c;
    /* A completion after next_wait() but before entering the kernel wait
     * must remain pending; there is no separate flags-clear operation. */
    if(complete_on_wait) { complete_on_wait=false;complete(); }
    observed_flags=pending_flags&a; pending_flags&=~observed_flags;
    return observed_flags;
}
osSemaphoreId_t osSemaphoreNew(uint32_t a,uint32_t b,const void*c) { (void)a;(void)c; assert(sem_count<8); semaphores[sem_count]=b; return &semaphores[sem_count++]; }
int osSemaphoreRelease(osSemaphoreId_t s) { ++*(int*)s; return osOK; }
int osSemaphoreAcquire(osSemaphoreId_t s,uint32_t timeout) {
    uint64_t end=clock_us+(timeout==osWaitForever?100000:(uint64_t)timeout*1000);
    while(!*(int*)s && clock_us<end) {
        service_once();
        if(auto_complete && bus.active && !bus.active->cancel) { clock_us+=10; complete(); }
        clock_us+=10;
    }
    if(*(int*)s) { --*(int*)s; return osOK; }
    assert(timeout!=osWaitForever); return -1;
}
static void cb(SRNOS_SPIDevice *d,void *c,SRNOS_SPIStatus s) { (void)d;(void)c; ++callbacks; callback_result=s; }
static SRNOS_SPIDevice a,b,mcp;
static void reset(void) {
    selected=launches=completions=aborts=callbacks=sem_count=0;
    clock_us=100; abort_ok=auto_complete=true; early_complete=false;
    inject_reservation=complete_on_wait=false;reservation_delay_us=0;
    pending_flags=waited_ticks=observed_flags=0;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b)); memset(&mcp,0,sizeof(mcp));
    hw.State=HAL_SPI_STATE_READY; SRNOS_SPIInitPre(); SRNOS_SPIInitRtos();
    assert(SRNOS_SPIDeviceInit(&a,0,&gpio,1)==SRNOS_SPI_OK);
    assert(SRNOS_SPIDeviceInit(&b,0,&gpio,2)==SRNOS_SPI_OK);
    assert(SRNOS_SPIDeviceInit(&mcp,0,&gpio,3)==SRNOS_SPI_OK);
    a.imu_priority=b.imu_priority=1;
}
static void submit(SRNOS_SPIDevice *d) { uint8_t v=0x80; assert(SRNOS_SPITransferAsync(d,&v,NULL,1,cb,NULL)==SRNOS_SPI_OK); }
int main(void) {
    reset();
    assert(next_wait()==osWaitForever);
    uint8_t large[512]={0};inject_reservation=true;
    assert(SRNOS_SPITransferAsync(&a,large,NULL,sizeof(large),cb,NULL)==SRNOS_SPI_OK);
    assert(reservation_preemptions==1 && !inject_reservation);
    assert(next_wait()==0);service_once();
    assert(callback_result==SRNOS_SPI_CANCELLED && callbacks==1 && launches==0);
    assert(next_wait()==osWaitForever);
    reset(); submit(&mcp);service_once();
    assert(next_wait()==5);
    clock_us+=4500;assert(next_wait()==1);
    clock_us+=500;assert(next_wait()==0);
    reset(); submit(&mcp);service_once();pending_flags=0;complete_on_wait=true;
    dispatch_wait(NULL);
    assert(waited_ticks==5 && observed_flags==1 && bus.completion==1);
    assert(next_wait()==0);service_once();assert(!bus.active && callbacks==1);
    reset();
    uint8_t tx[3]={1,2,3},rx[3]={0};
    assert(SRNOS_SPITransferAsync(&a,tx,rx,3,cb,NULL)==SRNOS_SPI_OK);
    memset(tx,0,3); service_once(); assert(observed_len==3 && observed_tx[0]==1 && rx[0]==0);
    complete(); service_once(); assert(rx[0]==(1^0x55) && callbacks==1 && !selected);
    /* All eight slots, FIFO within class and sustained competitor fairness. */
    reset(); submit(&a);submit(&b);submit(&a);submit(&b);
    for(unsigned i=0;i<4;i++) submit(&mcp);
    assert(SRNOS_SPITransferAsync(&a,tx,rx,1,cb,NULL)==SRNOS_SPI_BUSY);
    assert(SRNOS_SPITransferAsync(&mcp,tx,rx,1,cb,NULL)==SRNOS_SPI_BUSY);
    unsigned expect[]={1,2,1,2,3,1,2,1,2,3};
    service_once();
    for(unsigned i=0;i<10;i++) {
        assert(selected==expect[i]); clock_us+=20; complete(); service_once();
        if(i<8) { if(i==0 || i==2 || i==5 || i==7) submit(&a); else if(i==1 || i==3 || i==6) submit(&b); }
    }
    /* Waiting expiry cannot assert another CS; cancellation acknowledges RX lifetime. */
    reset(); submit(&mcp); service_once(); submit(&a); clock_us+=1001; service_once();
    assert(a.statistics.queue_expired==1 && selected==3);
    reset(); rx[0]=0; assert(SRNOS_SPITransferAsync(&a,tx,rx,1,cb,NULL)==SRNOS_SPI_OK);
    assert(SRNOS_SPICancel(&a)==SRNOS_SPI_BUSY); service_once();
    assert(SRNOS_SPICancel(&a)==SRNOS_SPI_OK && !launches && rx[0]==0 && callback_result==SRNOS_SPI_CANCELLED);
    reset(); assert(SRNOS_SPITransferAsync(&a,tx,rx,1,cb,NULL)==SRNOS_SPI_OK); service_once();
    assert(SRNOS_SPICancel(&a)==SRNOS_SPI_BUSY); service_once(); assert(aborts==1 && !selected);
    assert(SRNOS_SPICancel(&a)==SRNOS_SPI_OK && rx[0]==0);
    unsigned done=callbacks; HAL_SPI_TxRxCpltCallback(&hw); service_once(); assert(callbacks==done && SRNOS_SPI_DBG.spurious_complete==1);
    reset(); submit(&mcp); service_once(); clock_us+=5000; service_once(); assert(aborts==1 && callback_result==SRNOS_SPI_TIMEOUT);
    reset(); submit(&mcp); service_once(); submit(&a); abort_ok=false; clock_us+=5000; service_once();
    assert(bus.faulted && selected==3 && launches==1 && SRNOS_SPITransferAsync(&a,tx,rx,1,cb,NULL)==SRNOS_SPI_ERROR);
    reset(); early_complete=true; submit(&a); service_once(); service_once(); assert(callbacks==1 && !bus.active);
    reset(); assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,3,5)==SRNOS_SPI_OK); assert(rx[0]==0x55);
    reset(); bus.callback_dev=&a;
    assert(SRNOS_SPICancel(&a)==SRNOS_SPI_BUSY); /* completion callback still owns context */
    bus.callback_dev=NULL; assert(SRNOS_SPICancel(&a)==SRNOS_SPI_OK);
    reset(); auto_complete=false; rx[0]=0xaa;
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,1)==SRNOS_SPI_CANCELLED);
    assert(rx[0]==0xaa && !bus.active && !selected && aborts==1);
    /* A preempted TX copy consumes the original synchronous deadline. */
    reset();auto_complete=false;inject_reservation=true;reservation_delay_us=3000;
    uint64_t started=clock_us;rx[0]=0xaa;
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,5)==SRNOS_SPI_CANCELLED);
    assert(clock_us-started>=5000 && clock_us-started<5100);
    assert(rx[0]==0xaa && aborts==1 && !bus.active);
    reset();auto_complete=false;inject_reservation=true;reservation_delay_us=6000;
    started=clock_us;
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,5)==SRNOS_SPI_CANCELLED);
    assert(clock_us-started<6100 && !launches && !bus.active);
    /* SDK wait constants: a zero deadline has no DMA/ownership side effect. */
    reset();started=clock_us;rx[0]=0xaa;
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,SRNOS_NO_WAIT)==SRNOS_SPI_TIMEOUT);
    assert(SRNOS_SPITransferAsyncWait(&mcp,tx,rx,1,cb,NULL,SRNOS_NO_WAIT)==SRNOS_SPI_TIMEOUT);
    assert(clock_us==started && !launches && !callbacks && !mcp.statistics.accepted);
    assert(rx[0]==0xaa && SRNOS_SPIIsBusBusy(&mcp)==SRNOS_SPI_OK);
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,0x80000000u)==SRNOS_SPI_INVALID_PARAM);
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,SRNOS_WAIT_FOREVER)==SRNOS_SPI_OK);
    reset();auto_complete=false;started=clock_us;rx[0]=0xaa;
    assert(SRNOS_SPITransferBlocking(&mcp,tx,rx,1,SRNOS_WAIT_FOREVER)==SRNOS_SPI_TIMEOUT);
    assert(clock_us-started>=5000 && clock_us-started<5100 && aborts==1 && rx[0]==0xaa);
    reset();
    assert(SRNOS_SPITransferAsyncWait(&mcp,tx,rx,1,cb,NULL,INT32_MAX)==SRNOS_SPI_OK);
    clock_us+=(UINT64_C(1)<<32);service_once();
    assert(launches==1 && mcp.statistics.max_queue_us==UINT32_MAX);
    complete();service_once();assert(callback_result==SRNOS_SPI_OK);
    reset();submit(&mcp);assert(bus.slots[4].deadline_us==5000);
    puts("SPI actual dispatcher: unmasked copy/preemption, total deadline, event race, lifetime, 8 slots, FIFO, fairness, expiry, cancellation, quarantine and IRQ passed");
}
