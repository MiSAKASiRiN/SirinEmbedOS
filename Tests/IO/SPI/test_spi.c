#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../Source/IO/SPI/bsp_spi.c"
static SPI_HandleTypeDef hw;
static DMA_HandleTypeDef txdma,rxdma;
static GPIO_TypeDef gpio;
static uint64_t clock_us;
static unsigned selected, launches, completions, aborts, callbacks;
static bool abort_ok=true, auto_complete=true, early_complete=false;
static int semaphores[8], sem_count;
static uint8_t observed_tx[512];
static size_t observed_len;
static bsp_spi_status_t callback_result;
void DWT_Init(uint32_t x) { (void)x; }
uint64_t DWT_GetUptimeUs(void) { return clock_us; }
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
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) { (void)t;return f; }
uint32_t osThreadFlagsWait(uint32_t a,uint32_t b,uint32_t c) { (void)a;(void)b;(void)c; return 0; }
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
static void cb(bsp_spi_device_t *d,void *c,bsp_spi_status_t s) { (void)d;(void)c; ++callbacks; callback_result=s; }
static bsp_spi_device_t a,b,mcp;
static void reset(void) {
    selected=launches=completions=aborts=callbacks=sem_count=0;
    clock_us=100; abort_ok=auto_complete=true; early_complete=false;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b)); memset(&mcp,0,sizeof(mcp));
    hw.State=HAL_SPI_STATE_READY; bsp_spi_init_pre(); bsp_spi_init_rtos();
    assert(bsp_spi_device_init(&a,0,&gpio,1)==BSP_SPI_OK);
    assert(bsp_spi_device_init(&b,0,&gpio,2)==BSP_SPI_OK);
    assert(bsp_spi_device_init(&mcp,0,&gpio,3)==BSP_SPI_OK);
    a.imu_priority=b.imu_priority=1;
}
static void submit(bsp_spi_device_t *d) { uint8_t v=0x80; assert(bsp_spi_transfer_async(d,&v,NULL,1,cb,NULL)==BSP_SPI_OK); }
int main(void) {
    reset();
    uint8_t tx[3]={1,2,3},rx[3]={0};
    assert(bsp_spi_transfer_async(&a,tx,rx,3,cb,NULL)==BSP_SPI_OK);
    memset(tx,0,3); service_once(); assert(observed_len==3 && observed_tx[0]==1 && rx[0]==0);
    complete(); service_once(); assert(rx[0]==(1^0x55) && callbacks==1 && !selected);
    /* All eight slots, FIFO within class and sustained competitor fairness. */
    reset(); submit(&a);submit(&b);submit(&a);submit(&b);
    for(unsigned i=0;i<4;i++) submit(&mcp);
    assert(bsp_spi_transfer_async(&a,tx,rx,1,cb,NULL)==BSP_SPI_BUSY);
    assert(bsp_spi_transfer_async(&mcp,tx,rx,1,cb,NULL)==BSP_SPI_BUSY);
    unsigned expect[]={1,2,1,2,3,1,2,1,2,3};
    service_once();
    for(unsigned i=0;i<10;i++) {
        assert(selected==expect[i]); clock_us+=20; complete(); service_once();
        if(i<8) { if(i==0 || i==2 || i==5 || i==7) submit(&a); else if(i==1 || i==3 || i==6) submit(&b); }
    }
    /* Waiting expiry cannot assert another CS; cancellation acknowledges RX lifetime. */
    reset(); submit(&mcp); service_once(); submit(&a); clock_us+=1001; service_once();
    assert(a.statistics.queue_expired==1 && selected==3);
    reset(); rx[0]=0; assert(bsp_spi_transfer_async(&a,tx,rx,1,cb,NULL)==BSP_SPI_OK);
    assert(bsp_spi_cancel(&a)==BSP_SPI_BUSY); service_once();
    assert(bsp_spi_cancel(&a)==BSP_SPI_OK && !launches && rx[0]==0 && callback_result==BSP_SPI_CANCELLED);
    reset(); assert(bsp_spi_transfer_async(&a,tx,rx,1,cb,NULL)==BSP_SPI_OK); service_once();
    assert(bsp_spi_cancel(&a)==BSP_SPI_BUSY); service_once(); assert(aborts==1 && !selected);
    assert(bsp_spi_cancel(&a)==BSP_SPI_OK && rx[0]==0);
    unsigned done=callbacks; HAL_SPI_TxRxCpltCallback(&hw); service_once(); assert(callbacks==done && DBG.spurious_complete==1);
    reset(); submit(&mcp); service_once(); clock_us+=5000; service_once(); assert(aborts==1 && callback_result==BSP_SPI_TIMEOUT);
    reset(); submit(&mcp); service_once(); submit(&a); abort_ok=false; clock_us+=5000; service_once();
    assert(bus.faulted && selected==3 && launches==1 && bsp_spi_transfer_async(&a,tx,rx,1,cb,NULL)==BSP_SPI_ERROR);
    reset(); early_complete=true; submit(&a); service_once(); service_once(); assert(callbacks==1 && !bus.active);
    reset(); assert(bsp_spi_transfer_blocking(&mcp,tx,rx,3,5)==BSP_SPI_OK); assert(rx[0]==0x55);
    reset(); bus.callback_dev=&a;
    assert(bsp_spi_cancel(&a)==BSP_SPI_BUSY); /* completion callback still owns context */
    bus.callback_dev=NULL; assert(bsp_spi_cancel(&a)==BSP_SPI_OK);
    reset(); auto_complete=false; rx[0]=0xaa;
    assert(bsp_spi_transfer_blocking(&mcp,tx,rx,1,1)==BSP_SPI_CANCELLED);
    assert(rx[0]==0xaa && !bus.active && !selected && aborts==1);
    puts("SPI actual dispatcher: copy, lifetime, 8 slots, FIFO, fairness, expiry, cancellation, abort quarantine, late/early IRQ, sync passed");
}
