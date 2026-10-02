#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../Source/IO/I2C/I2C.c"
uint32_t SRNOS_TestPrimask;
static bool inject_reservation;
static unsigned reservation_preemptions;
static uint64_t time_us;
static uint32_t reservation_delay_us;
void SRNOS_TestPreempt(void) {
    if(!inject_reservation)return;
    for(unsigned i=0;i<4;i++)if(bus.slots[i].state==SRNOS_I2C_SLOT_RESERVED) {
        inject_reservation=false;SRNOS_I2CRequest *r=&bus.slots[i];
        assert(!SRNOS_TestPrimask && r->device->pending);
        assert(SRNOS_I2CIsBusBusy(r->device)==SRNOS_I2C_BUSY);
        assert(SRNOS_I2CRecoverBus(SRNOS_I2C_BUS_4,SRNOS_I2C_RECOVER_REASON_USER)==SRNOS_I2C_OK);
        assert(r->state==SRNOS_I2C_SLOT_RESERVED && !r->cancel);
        if(reservation_delay_us) {time_us+=reservation_delay_us;return;}
        uint8_t value=1;
        assert(SRNOS_I2CMasterTransmit(r->device,&value,1,NULL,NULL)==SRNOS_I2C_BUSY);
        r->cancel=true;service_once();
        assert(r->state==SRNOS_I2C_SLOT_RESERVED && !bus.active);
        ++reservation_preemptions;return;
    }
}
static I2C_HandleTypeDef hw;
static uint32_t abort_count, starts, callbacks;
static int sem[4], sem_count;
static bool abort_ok, auto_complete;
static bool complete_on_wait;
static uint32_t pending_flags, waited_ticks, observed_flags;
static SRNOS_I2CRequest *publish_on_delay;
static osThreadId_t caller;
static SRNOS_I2CDevice lis,spa,eeprom,other,extra;
static uint16_t addresses[128];
static uint8_t sent[64];
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t id) { assert(!id); return &hw; }
bool SRNOS_BoardI2CQuiescent(I2C_HandleTypeDef *h) { return !h->State; }
void SRNOS_TimeInit(uint32_t frequency) { (void)frequency; }
uint64_t SRNOS_TimeGetUptimeUs(void) { return time_us; }
uint32_t HAL_GetTick(void) { return (uint32_t)(time_us/1000); }
static void run(void) { osThreadId_t saved=caller;caller=worker;service_once();caller=saved; }
static void advance(void) {
    time_us+=1000;
    if(publish_on_delay) {
        /* Resume the interrupted producer's bounded copy/publication before
         * the dispatcher runs. The cancellation caller must yield for this. */
        assert(publish_on_delay->state==SRNOS_I2C_SLOT_RESERVED && publish_on_delay->cancel);
        publish_on_delay->state=SRNOS_I2C_SLOT_QUEUED;publish_on_delay=NULL;
    }
    if(auto_complete && hw.State) { hw.State=0;HAL_I2C_MemRxCpltCallback(&hw); }
    run();
}
void osDelay(uint32_t ms) { while(ms--) advance(); }
void HAL_Delay(uint32_t ms) { osDelay(ms); }
uint32_t osKernelGetTickFreq(void) { return 1000; }
int osKernelGetState(void) { return osKernelRunning; }
osThreadId_t osThreadGetId(void) { return caller; }
osThreadId_t osThreadNew(void(*f)(void*),void*a,const osThreadAttr_t*b) { (void)f;(void)a;assert(b->stack_size==3072 && b->priority==osPriorityHigh6);return (void*)1; }
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) { (void)t;return pending_flags|=f; }
uint32_t osThreadFlagsWait(uint32_t a,uint32_t b,uint32_t c) {
    (void)b;waited_ticks=c;
    if(complete_on_wait) { complete_on_wait=false;hw.State=0;HAL_I2C_MemRxCpltCallback(&hw); }
    observed_flags=pending_flags&a;pending_flags&=~observed_flags;
    return observed_flags;
}
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
    inject_reservation=complete_on_wait=false;pending_flags=waited_ticks=observed_flags=0;publish_on_delay=NULL;reservation_delay_us=0;
    SRNOS_I2CInit();SRNOS_I2CInitRtos();
    assert(SRNOS_I2CDeviceInit(&lis,0,0x1e)==SRNOS_I2C_OK);
    assert(SRNOS_I2CDeviceInit(&spa,0,0x76)==SRNOS_I2C_OK);
    assert(SRNOS_I2CDeviceInit(&eeprom,0,0x50)==SRNOS_I2C_OK);
    assert(SRNOS_I2CDeviceInit(&other,0,0x51)==SRNOS_I2C_OK);
    assert(SRNOS_I2CDeviceInit(&extra,0,0x52)==SRNOS_I2C_OK);
}
static void complete(void) { hw.State=0;HAL_I2C_MemRxCpltCallback(&hw);run(); }
static void chain(SRNOS_I2CDevice *d,void *ctx,SRNOS_I2CStatus result) {
    assert(result==SRNOS_I2C_OK && caller==worker);
    assert(SRNOS_I2CWaitComplete(d,10)==SRNOS_I2C_INVALID_PARAM);
    if(++callbacks<8) assert(SRNOS_I2CMemRead(d,7,1,ctx,1,chain,ctx)==SRNOS_I2C_OK);
}
int main(void) {
    uint8_t x=0,y=0,z=0,tx[65];reset();
    inject_reservation=true;
    assert(SRNOS_I2CMasterTransmit(&eeprom,tx,64,NULL,NULL)==SRNOS_I2C_OK);
    assert(reservation_preemptions==1);run();
    assert(!eeprom.pending && !starts && eeprom.completed_status==SRNOS_I2C_CANCELLED);
    /* Reproduce another task cancelling while the producer owns RESERVED.
     * Returning before that producer publishes would release its buffer early. */
    reset();assert(SRNOS_I2CMasterTransmit(&eeprom,tx,64,NULL,NULL)==SRNOS_I2C_OK);
    SRNOS_I2CRequest *reserved=find_device(&eeprom);reserved->state=SRNOS_I2C_SLOT_RESERVED;publish_on_delay=reserved;
    assert(SRNOS_I2CCancelDevice(&eeprom,SRNOS_I2C_RECOVER_REASON_USER)==SRNOS_I2C_OK);
    assert(time_us==1000 && !publish_on_delay && !starts && !eeprom.pending);
    assert(eeprom.completed_status==SRNOS_I2C_CANCELLED && reserved->state==SRNOS_I2C_SLOT_FREE);
    reset();assert(next_wait()==osWaitForever);
    assert(SRNOS_I2CMemRead(&lis,9,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(next_wait()==0);run();assert(next_wait()==10);
    time_us=9500;assert(next_wait()==1);time_us=10000;assert(next_wait()==0);
    reset();assert(SRNOS_I2CMemRead(&lis,9,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);run();
    pending_flags=0;complete_on_wait=true;dispatch_wait(NULL);
    assert(waited_ticks==10 && observed_flags==1 && bus.completion==1);
    assert(next_wait()==0);run();assert(!bus.active && x==9);
    reset();x=y=z=0;
    memset(tx,0x55,sizeof(tx));
    assert(SRNOS_I2CMemWrite(&eeprom,0,2,tx,64,NULL,NULL)==SRNOS_I2C_OK);
    memset(tx,0xcc,sizeof(tx));assert(!starts); /* HAL never runs in submitter. */
    run();assert(starts==1 && addresses[0]==0xa0 && sent[63]==0x55);
    SRNOS_I2CDevice alias;
    assert(SRNOS_I2CDeviceInit(&alias,0,0x50)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&alias,0,2,&z,1,NULL,NULL)==SRNOS_I2C_BUSY);
    assert(SRNOS_I2CMemRead(&lis,0x4f,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&spa,0x0d,1,&y,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&other,0,2,&z,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&extra,0,2,&z,1,NULL,NULL)==SRNOS_I2C_BUSY);
    complete();assert(bus.active->device==&lis);
    hw.State=0;HAL_I2C_MemRxCpltCallback(&hw);assert(x==0);run();assert(x==0x4f && bus.active->device==&spa);
    assert(SRNOS_I2CWaitComplete(&lis,1)==SRNOS_I2C_OK);
    assert(SRNOS_I2CCancelDevice(&lis,255)==SRNOS_I2C_OK && spa.pending && !abort_count);
    complete();complete();assert(y==0x0d && !bus.active);
    assert(SRNOS_I2CMemRead(&lis,0,1,tx,65,NULL,NULL)==SRNOS_I2C_INVALID_PARAM);
    SRNOS_I2CSnapshot snap;assert(SRNOS_I2CGetSnapshot(0,&snap)==SRNOS_I2C_OK && snap.high_water==3 && snap.background.queue_full==1);
    /* Reserved slots remain usable with both background slots full; fairness
       forces background after four periodic completions even with requeue. */
    reset();assert(SRNOS_I2CMemRead(&eeprom,0,2,&z,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&other,0,2,&z,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,chain,&x)==SRNOS_I2C_OK);
    assert(SRNOS_I2CMemRead(&spa,1,1,&y,1,NULL,NULL)==SRNOS_I2C_OK);
    run();for(unsigned i=0;i<4;i++) complete();
    assert(addresses[4]==0xa0);while(bus.active) complete();assert(callbacks==8);
    /* Semaphore wait covers queueing; expiry of a queued request never aborts
       another device or permits a later copy into the expired RX buffer. */
    reset();x=0;assert(SRNOS_I2CMemRead(&eeprom,8,2,&z,1,NULL,NULL)==SRNOS_I2C_OK);run();
    assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,2)==SRNOS_I2C_TIMEOUT && !abort_count && eeprom.pending && !x);
    complete();assert(!x);
    reset();auto_complete=true;
    assert(SRNOS_I2CMemRead(&lis,9,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,30)==SRNOS_I2C_OK && x==9);
    /* Active timeout, failed abort isolation, and ignored late callback. */
    reset();x=0;assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,3)==SRNOS_I2C_TIMEOUT && abort_count==1 && !lis.pending);
    HAL_I2C_MemRxCpltCallback(&hw);run();assert(!x);
    reset();abort_ok=false;x=0;
    assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,3)==SRNOS_I2C_ERROR && bus.quarantined);
    assert(SRNOS_I2CMemRead(&spa,1,1,&y,1,NULL,NULL)==SRNOS_I2C_ERROR);
    HAL_I2C_MemRxCpltCallback(&hw);run();assert(!x);
    /* No caller wait is needed for autonomous deadline cleanup. */
    reset();assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);run();time_us=11000;run();
    assert(!lis.pending && lis.completed_status==SRNOS_I2C_TIMEOUT && abort_count==1);
    reset();assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);time_us=21000;run();
    assert(!lis.pending && lis.completed_status==SRNOS_I2C_TIMEOUT && !starts);
    reset();assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CCancelDevice(&lis,255)==SRNOS_I2C_OK && !starts && lis.completed_status==SRNOS_I2C_CANCELLED);
    assert(SRNOS_I2CDeviceReady(&eeprom,2,30)==SRNOS_I2C_OK);
    /* Active user cancellation preserves queued traffic; an ordinary NACK
       reports an error without an unnecessary abort or bus quarantine. */
    reset();assert(SRNOS_I2CMemRead(&eeprom,8,2,&z,1,NULL,NULL)==SRNOS_I2C_OK);run();
    assert(SRNOS_I2CMemRead(&lis,9,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CCancelDevice(&eeprom,255)==SRNOS_I2C_OK && abort_count==1 && lis.pending);
    assert(eeprom.completed_status==SRNOS_I2C_CANCELLED);complete();assert(x==9);
    reset();assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);run();
    hw.State=0;HAL_I2C_ErrorCallback(&hw);run();
    assert(SRNOS_I2CWaitComplete(&lis,30)==SRNOS_I2C_ERROR && !abort_count && !bus.quarantined);
    reset();time_us=(uint64_t)UINT32_MAX*1000;auto_complete=true;
    assert(SRNOS_I2CMemRead(&lis,9,1,&x,1,NULL,NULL)==SRNOS_I2C_OK && SRNOS_I2CWaitComplete(&lis,30)==SRNOS_I2C_OK);
    /* NO_WAIT is a real query: no wait/cancel/ownership change while pending. */
    reset();x=0;
    assert(SRNOS_I2CMemRead(&lis,7,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,SRNOS_NO_WAIT)==SRNOS_I2C_BUSY);
    assert(time_us==0 && lis.pending && !find_device(&lis)->cancel && !find_device(&lis)->waiter);
    run();assert(SRNOS_I2CWaitComplete(&lis,SRNOS_NO_WAIT)==SRNOS_I2C_BUSY && !abort_count);
    complete();assert(SRNOS_I2CWaitComplete(&lis,SRNOS_NO_WAIT)==SRNOS_I2C_OK && x==7);
    bus.callback_device=&lis;assert(SRNOS_I2CWaitComplete(&lis,SRNOS_NO_WAIT)==SRNOS_I2C_BUSY);bus.callback_device=NULL;
    assert(SRNOS_I2CWaitComplete(&lis,0x80000000u)==SRNOS_I2C_INVALID_PARAM);
    reset();auto_complete=true;
    assert(SRNOS_I2CMemRead(&lis,8,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,SRNOS_WAIT_FOREVER)==SRNOS_I2C_OK && x==8);
    reset();assert(SRNOS_I2CMemRead(&lis,8,1,&x,1,NULL,NULL)==SRNOS_I2C_OK);
    assert(SRNOS_I2CWaitComplete(&lis,SRNOS_WAIT_FOREVER)==SRNOS_I2C_TIMEOUT && abort_count==1 && !lis.pending);
    reset();
    assert(SRNOS_I2CDeviceReady(&eeprom,2,SRNOS_NO_WAIT)==SRNOS_I2C_TIMEOUT);
    assert(time_us==0 && !eeprom.pending && !find_device(&eeprom));
    assert(SRNOS_I2CDeviceReady(&eeprom,2,SRNOS_WAIT_FOREVER)==SRNOS_I2C_OK);
    reset();inject_reservation=true;reservation_delay_us=2000;
    assert(SRNOS_I2CDeviceReady(&eeprom,2,1)==SRNOS_I2C_TIMEOUT);
    assert(!eeprom.pending && !find_device(&eeprom) && !starts && time_us==3000);
    puts("I2C dispatcher: unmasked copy/preemption, reserved cancellation, event race, reserved slots, fairness, staged buffers, semaphore completion, deadlines, abort isolation and tick wrap passed");
}
