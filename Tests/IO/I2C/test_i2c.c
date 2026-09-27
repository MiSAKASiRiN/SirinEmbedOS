#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../Source/IO/I2C/bsp_i2c.c"
static I2C_HandleTypeDef hw;
static uint64_t time_us;
static uint32_t abort_count, starts, callbacks;
static int sem[4], sem_count;
static bool abort_ok, auto_complete;
static osThreadId_t caller;
static bsp_i2c_device_t lis,spa,eeprom,other,extra;
static uint16_t addresses[128];
static uint8_t sent[64];
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t id) { assert(!id); return &hw; }
bool SRNOS_BoardI2CQuiescent(I2C_HandleTypeDef *h) { return !h->State; }
void DWT_Init(uint32_t frequency) { (void)frequency; }
uint64_t DWT_GetUptimeUs(void) { return time_us; }
uint32_t HAL_GetTick(void) { return (uint32_t)(time_us/1000); }
static void run(void) { osThreadId_t saved=caller;caller=worker;service_once();caller=saved; }
static void advance(void) {
    time_us+=1000;
    if(auto_complete && hw.State) { hw.State=0;HAL_I2C_MemRxCpltCallback(&hw); }
    run();
}
void osDelay(uint32_t ms) { while(ms--) advance(); }
uint32_t osKernelGetTickFreq(void) { return 1000; }
int osKernelGetState(void) { return osKernelRunning; }
osThreadId_t osThreadGetId(void) { return caller; }
osThreadId_t osThreadNew(void(*f)(void*),void*a,const osThreadAttr_t*b) { (void)f;(void)a;assert(b->stack_size==3072 && b->priority==osPriorityHigh6);return (void*)1; }
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) { (void)t;return f; }
uint32_t osThreadFlagsWait(uint32_t a,uint32_t b,uint32_t c) { (void)a;(void)b;(void)c;return 0; }
osSemaphoreId_t osSemaphoreNew(uint32_t maximum,uint32_t initial,const void *attr) { (void)attr;assert(maximum==1 && sem_count<4);sem[sem_count]=initial;return &sem[sem_count++]; }
int osSemaphoreRelease(osSemaphoreId_t s) { assert(!*(int*)s); ++*(int*)s; return osOK; }
int osSemaphoreAcquire(osSemaphoreId_t s,uint32_t timeout) {
    uint32_t elapsed=0;
    while(!*(int*)s && elapsed<timeout) { advance();assert(++elapsed<1000); }
    if(!*(int*)s) return osErrorTimeout;
    --*(int*)s;return osOK;
}
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *h,uint16_t addr) { (void)addr;++abort_count;if(abort_ok) { h->State=0;HAL_I2C_AbortCpltCallback(h); }return abort_ok?HAL_OK:HAL_ERROR; }
static HAL_StatusTypeDef start(I2C_HandleTypeDef *h,uint16_t address,uint16_t reg,uint8_t *p,uint16_t n,bool rx) {
    assert(caller==worker && !h->State && n<=64 && n);assert(starts<128);
    addresses[starts++]=address;h->State=1;
    assert(p==(rx?s_i2c_rx_stage:s_i2c_tx_stage));
    if(rx) memset(p,reg,n);else memcpy(sent,p,n);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read_DMA(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t w,uint8_t *p,uint16_t n) { assert(w==1 || w==2);return start(h,a,r,p,n,true); }
HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t w,uint8_t *p,uint16_t n) { assert(w==1 || w==2);return start(h,a,r,p,n,false); }
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *h,uint16_t a,uint8_t *p,uint16_t n) { return start(h,a,0,p,n,false); }
HAL_StatusTypeDef HAL_I2C_Master_Receive_DMA(I2C_HandleTypeDef *h,uint16_t a,uint8_t *p,uint16_t n) { return start(h,a,0,p,n,true); }
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h,uint16_t a,uint32_t t,uint32_t ms) { assert(caller==worker && !h->State && a==0xa0 && t==1 && ms==1);return HAL_OK; }
static void reset(void) {
    memset(&hw,0,sizeof(hw));memset(sem,0,sizeof(sem));sem_count=0;
    time_us=abort_count=starts=callbacks=0;abort_ok=true;auto_complete=false;caller=(void*)2;
    bsp_i2c_init();bsp_i2c_init_rtos();
    assert(bsp_i2c_device_init(&lis,0,0x1e)==BSP_I2C_OK);
    assert(bsp_i2c_device_init(&spa,0,0x76)==BSP_I2C_OK);
    assert(bsp_i2c_device_init(&eeprom,0,0x50)==BSP_I2C_OK);
    assert(bsp_i2c_device_init(&other,0,0x51)==BSP_I2C_OK);
    assert(bsp_i2c_device_init(&extra,0,0x52)==BSP_I2C_OK);
}
static void complete(void) { hw.State=0;HAL_I2C_MemRxCpltCallback(&hw);run(); }
static void chain(bsp_i2c_device_t *d,void *ctx,bsp_i2c_status_t result) {
    assert(result==BSP_I2C_OK && caller==worker);
    assert(bsp_i2c_wait_complete(d,10)==BSP_I2C_INVALID_PARAM);
    if(++callbacks<8) assert(bsp_i2c_mem_read(d,7,1,ctx,1,chain,ctx)==BSP_I2C_OK);
}
int main(void) {
    uint8_t x=0,y=0,z=0,tx[65];reset();
    memset(tx,0x55,sizeof(tx));
    assert(bsp_i2c_mem_write(&eeprom,0,2,tx,64,NULL,NULL)==BSP_I2C_OK);
    memset(tx,0xcc,sizeof(tx));assert(!starts); /* HAL never runs in submitter. */
    run();assert(starts==1 && addresses[0]==0xa0 && sent[63]==0x55);
    bsp_i2c_device_t alias;
    assert(bsp_i2c_device_init(&alias,0,0x50)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&alias,0,2,&z,1,NULL,NULL)==BSP_I2C_BUSY);
    assert(bsp_i2c_mem_read(&lis,0x4f,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&spa,0x0d,1,&y,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&other,0,2,&z,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&extra,0,2,&z,1,NULL,NULL)==BSP_I2C_BUSY);
    complete();assert(bus.active->device==&lis);
    hw.State=0;HAL_I2C_MemRxCpltCallback(&hw);assert(x==0);run();assert(x==0x4f && bus.active->device==&spa);
    assert(bsp_i2c_wait_complete(&lis,1)==BSP_I2C_OK);
    assert(bsp_i2c_cancel_device(&lis,255)==BSP_I2C_OK && spa.pending && !abort_count);
    complete();complete();assert(y==0x0d && !bus.active);
    assert(bsp_i2c_mem_read(&lis,0,1,tx,65,NULL,NULL)==BSP_I2C_INVALID_PARAM);
    bsp_i2c_snapshot_t snap;assert(bsp_i2c_get_snapshot(0,&snap)==BSP_I2C_OK && snap.high_water==3 && snap.background.queue_full==1);
    /* Reserved slots remain usable with both background slots full; fairness
       forces background after four periodic completions even with requeue. */
    reset();assert(bsp_i2c_mem_read(&eeprom,0,2,&z,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&other,0,2,&z,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&lis,7,1,&x,1,chain,&x)==BSP_I2C_OK);
    assert(bsp_i2c_mem_read(&spa,1,1,&y,1,NULL,NULL)==BSP_I2C_OK);
    run();for(unsigned i=0;i<4;i++) complete();
    assert(addresses[4]==0xa0);while(bus.active) complete();assert(callbacks==8);
    /* Semaphore wait covers queueing; expiry of a queued request never aborts
       another device or permits a later copy into the expired RX buffer. */
    reset();x=0;assert(bsp_i2c_mem_read(&eeprom,8,2,&z,1,NULL,NULL)==BSP_I2C_OK);run();
    assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_wait_complete(&lis,2)==BSP_I2C_TIMEOUT && !abort_count && eeprom.pending && !x);
    complete();assert(!x);
    reset();auto_complete=true;
    assert(bsp_i2c_mem_read(&lis,9,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_wait_complete(&lis,30)==BSP_I2C_OK && x==9);
    /* Active timeout, failed abort isolation, and ignored late callback. */
    reset();x=0;assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_wait_complete(&lis,3)==BSP_I2C_TIMEOUT && abort_count==1 && !lis.pending);
    HAL_I2C_MemRxCpltCallback(&hw);run();assert(!x);
    reset();abort_ok=false;x=0;
    assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_wait_complete(&lis,3)==BSP_I2C_ERROR && bus.quarantined);
    assert(bsp_i2c_mem_read(&spa,1,1,&y,1,NULL,NULL)==BSP_I2C_ERROR);
    HAL_I2C_MemRxCpltCallback(&hw);run();assert(!x);
    /* No caller wait is needed for autonomous deadline cleanup. */
    reset();assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);run();time_us=11000;run();
    assert(!lis.pending && lis.completed_status==BSP_I2C_TIMEOUT && abort_count==1);
    reset();assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);time_us=21000;run();
    assert(!lis.pending && lis.completed_status==BSP_I2C_TIMEOUT && !starts);
    reset();assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_cancel_device(&lis,255)==BSP_I2C_OK && !starts && lis.completed_status==BSP_I2C_CANCELLED);
    assert(bsp_i2c_device_ready(&eeprom,2,30)==BSP_I2C_OK);
    /* Active user cancellation preserves queued traffic; an ordinary NACK
       reports an error without an unnecessary abort or bus quarantine. */
    reset();assert(bsp_i2c_mem_read(&eeprom,8,2,&z,1,NULL,NULL)==BSP_I2C_OK);run();
    assert(bsp_i2c_mem_read(&lis,9,1,&x,1,NULL,NULL)==BSP_I2C_OK);
    assert(bsp_i2c_cancel_device(&eeprom,255)==BSP_I2C_OK && abort_count==1 && lis.pending);
    assert(eeprom.completed_status==BSP_I2C_CANCELLED);complete();assert(x==9);
    reset();assert(bsp_i2c_mem_read(&lis,7,1,&x,1,NULL,NULL)==BSP_I2C_OK);run();
    hw.State=0;HAL_I2C_ErrorCallback(&hw);run();
    assert(bsp_i2c_wait_complete(&lis,30)==BSP_I2C_ERROR && !abort_count && !bus.quarantined);
    reset();time_us=(uint64_t)UINT32_MAX*1000;auto_complete=true;
    assert(bsp_i2c_mem_read(&lis,9,1,&x,1,NULL,NULL)==BSP_I2C_OK && bsp_i2c_wait_complete(&lis,30)==BSP_I2C_OK);
    puts("I2C dispatcher: reserved slots, fairness, staged buffers, semaphore completion, per-device cancel, queue/active deadlines, abort isolation and tick wrap passed");
}
