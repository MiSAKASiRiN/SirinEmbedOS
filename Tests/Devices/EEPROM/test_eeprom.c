#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../Source/Devices/EEPROM/ft24c128a.c"

static uint8_t chip[16384], full[16384], output[16384], tx_copy[256];
static uint32_t tick, irq, ipsr, basepri, transfers, writes, cancels, delay_calls;
static int kernel;
static uintptr_t owner;
static bool stalled, nack, protected_write, quarantine, locked;
static unsigned busy_attempts;
static struct { bsp_i2c_device_t *dev; uint16_t addr; uint8_t *rx; size_t len; } active;

uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return irq; }
uint32_t __get_BASEPRI(void) { return basepri; }
uint32_t __get_IPSR(void) { return ipsr; }
void __disable_irq(void) { irq=1; }
void __set_PRIMASK(uint32_t value) { irq=value; }
int osKernelGetState(void) { return kernel; }
osThreadId_t osThreadGetId(void) { return (void *)owner; }
static void advance(void) {
    ++tick;
    if (!active.dev || stalled) return;
    if (!nack) {
        if (active.rx) memcpy(active.rx,chip+active.addr,active.len);
        else if (!protected_write) memcpy(chip+active.addr,tx_copy,active.len);
    }
    active.dev->completed_status=nack?BSP_I2C_ERROR:BSP_I2C_OK;
    active.dev->pending=0; active.dev=NULL;
}
void osDelay(uint32_t ticks) { assert(ticks==1); ++delay_calls; advance(); }
bsp_i2c_status_t bsp_i2c_device_init(bsp_i2c_device_t *dev,bsp_i2c_bus_id_t bus,uint16_t addr) {
    assert(bus==0 && addr==0x50); memset(dev,0,sizeof(*dev)); return BSP_I2C_OK;
}
static bsp_i2c_status_t start(bsp_i2c_device_t *dev,uint16_t addr,uint16_t width,
                              uint8_t *rx,const uint8_t *tx,size_t len) {
    assert(width==2 && len && len<=32 && (size_t)addr+len<=sizeof(chip));
    if(quarantine) return BSP_I2C_ERROR;
    if(locked || busy_attempts) { if(busy_attempts) --busy_attempts; return BSP_I2C_BUSY; }
    assert(!active.dev);
    if(tx) { assert(addr/64==(addr+len-1)/64); memcpy(tx_copy,tx,len); ++writes; }
    ++transfers; active.dev=dev; active.addr=addr; active.rx=rx; active.len=len;
    dev->pending=1;dev->completed_status=BSP_I2C_BUSY;
    return BSP_I2C_OK;
}
bsp_i2c_status_t bsp_i2c_mem_read(bsp_i2c_device_t *dev,uint16_t addr,uint16_t width,
                                  uint8_t *out,size_t len,bsp_i2c_callback_t cb,void *ctx) {
    assert(!cb && !ctx); return start(dev,addr,width,out,NULL,len);
}
bsp_i2c_status_t bsp_i2c_mem_write(bsp_i2c_device_t *dev,uint16_t addr,uint16_t width,
                                   const uint8_t *in,size_t len,bsp_i2c_callback_t cb,void *ctx) {
    assert(!cb && !ctx); return start(dev,addr,width,NULL,in,len);
}
bsp_i2c_status_t bsp_i2c_cancel_device(bsp_i2c_device_t *dev,uint32_t reason) {
    assert(reason==1 || reason==255); assert(active.dev==dev); ++cancels;
    active.dev=NULL; dev->pending=0; dev->completed_status=BSP_I2C_ERROR;
    return quarantine?BSP_I2C_ERROR:BSP_I2C_OK;
}
static void reset(void) {
    memset(&s,0,sizeof(s)); memset(&active,0,sizeof(active));
    memset(chip,0xA5,sizeof(chip)); memset(output,0,sizeof(output));
    tick=irq=ipsr=basepri=transfers=writes=cancels=delay_calls=busy_attempts=0;
    stalled=nack=protected_write=quarantine=locked=false;
    kernel=osKernelRunning;owner=1;
    for(size_t i=0;i<sizeof(full);++i) full[i]=(uint8_t)(i*73U+(i>>8));
}
static SRNOS_EEPROMStatus drain(void) {
    for(unsigned i=0;i<20000;++i) {
        unsigned before=delay_calls;
        SRNOS_EEPROMStatus st=SRNOS_EEPROMPoll();
        assert(before==delay_calls); /* Async polling never sleeps. */
        if(st!=SRNOS_EEPROM_IN_PROGRESS) return st;
        advance();
    }
    assert(!"operation never terminated");return SRNOS_EEPROM_TIMEOUT;
}
int main(void) {
    reset();
    assert(SRNOS_EEPROMWrite(0,full,sizeof(full),20)==SRNOS_EEPROM_OK);
    assert(writes==512 && !memcmp(chip,full,sizeof(chip)));
    assert(SRNOS_EEPROMReadAsync(0,output,sizeof(output),20)==SRNOS_EEPROM_OK);
    assert(drain()==SRNOS_EEPROM_OK && !memcmp(full,output,sizeof(full)));
    reset();busy_attempts=3;
    assert(SRNOS_EEPROMWriteAsync(0xF3,full,777,20)==SRNOS_EEPROM_OK && transfers==0);
    assert(SRNOS_EEPROMIsBusy());
    assert(SRNOS_EEPROMReadAsync(0,output,1,20)==SRNOS_EEPROM_BUSY);
    owner=2;assert(SRNOS_EEPROMPoll()==SRNOS_EEPROM_BUSY && SRNOS_EEPROMCancel()==SRNOS_EEPROM_BUSY);owner=1;
    assert(drain()==SRNOS_EEPROM_OK && !SRNOS_EEPROMIsBusy());
    assert(!memcmp(chip+0xF3,full,777) && chip[0xF2]==0xA5 && chip[0xF3+777]==0xA5);
    assert(SRNOS_EEPROMRead(0xF3,output,777,20)==SRNOS_EEPROM_OK && !memcmp(output,full,777));
    assert(SRNOS_EEPROMWrite(0x3FFF,full,1,20)==SRNOS_EEPROM_OK);
    assert(SRNOS_EEPROMRead(0x3FFF,output,1,20)==SRNOS_EEPROM_OK && output[0]==full[0]);
    unsigned before=transfers;
    assert(SRNOS_EEPROMWriteAsync(0x3FFF,full,2,20)==SRNOS_EEPROM_INVALID_ARGUMENT);
    assert(SRNOS_EEPROMReadAsync(0x4000,output,1,20)==SRNOS_EEPROM_INVALID_ARGUMENT);
    assert(SRNOS_EEPROMReadAsync(0,output,SIZE_MAX,20)==SRNOS_EEPROM_INVALID_ARGUMENT);
    assert(SRNOS_EEPROMReadAsync(0,NULL,1,20)==SRNOS_EEPROM_INVALID_ARGUMENT);
    assert(SRNOS_EEPROMReadAsync(0,output,0,20)==SRNOS_EEPROM_INVALID_ARGUMENT);
    assert(SRNOS_EEPROMReadAsync(0,output,1,0)==SRNOS_EEPROM_INVALID_ARGUMENT && transfers==before);
    reset();protected_write=true;
    assert(SRNOS_EEPROMWrite(0,full,1,20)==SRNOS_EEPROM_VERIFY_ERROR);
    reset();nack=true;
    assert(SRNOS_EEPROMRead(0,output,1,20)==SRNOS_EEPROM_IO_ERROR);
    reset();locked=true;
    assert(SRNOS_EEPROMRead(0,output,1,3)==SRNOS_EEPROM_TIMEOUT && !cancels);
    reset();stalled=true;
    assert(SRNOS_EEPROMRead(0,output,1,3)==SRNOS_EEPROM_TIMEOUT && cancels==1);
    reset();
    assert(SRNOS_EEPROMReadAsync(0,output,1,20)==SRNOS_EEPROM_OK);
    assert(SRNOS_EEPROMCancel()==SRNOS_EEPROM_CANCELLED && !cancels);
    assert(SRNOS_EEPROMWriteAsync(0,full,1,20)==SRNOS_EEPROM_OK);
    assert(SRNOS_EEPROMPoll()==SRNOS_EEPROM_IN_PROGRESS);
    assert(SRNOS_EEPROMCancel()==SRNOS_EEPROM_CANCELLED && cancels==1);
    assert(SRNOS_EEPROMReadAsync(0,output,1,20)==SRNOS_EEPROM_OK);
    before=transfers;for(unsigned i=0;i<6;++i) { assert(SRNOS_EEPROMPoll()==SRNOS_EEPROM_IN_PROGRESS && transfers==before);advance(); }
    assert(drain()==SRNOS_EEPROM_OK);
    reset();stalled=true;
    assert(SRNOS_EEPROMReadAsync(0,output,1,3)==SRNOS_EEPROM_OK);
    assert(SRNOS_EEPROMPoll()==SRNOS_EEPROM_IN_PROGRESS);quarantine=true;
    assert(drain()==SRNOS_EEPROM_IO_ERROR && cancels==1);
    reset();tick=UINT32_MAX-4;
    assert(SRNOS_EEPROMWrite(0x3F,full,130,20)==SRNOS_EEPROM_OK);
    reset();ipsr=1;assert(SRNOS_EEPROMReadAsync(0,output,1,20)==SRNOS_EEPROM_CONTEXT_ERROR);
    ipsr=0;irq=1;assert(SRNOS_EEPROMRead(0,output,1,20)==SRNOS_EEPROM_CONTEXT_ERROR);
    irq=0;basepri=5;assert(SRNOS_EEPROMRead(0,output,1,20)==SRNOS_EEPROM_CONTEXT_ERROR);
    basepri=0;kernel=0;assert(SRNOS_EEPROMRead(0,output,1,20)==SRNOS_EEPROM_CONTEXT_ERROR);
    puts("FT24C128A: full 16KiB, page split, 16-bit addressing, readback, async ownership, bounds, NACK, timeout/cancel/quarantine and tick wrap passed");
}
