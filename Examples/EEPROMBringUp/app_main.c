#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Devices/EEPROM.h"
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include <string.h>

/* No automatic writes. Wait at stage=1 for host to persist the full backup,
 * then explicitly set command=1. Reset clears command. Never loop the test. */
OMX_DEBUG_WATCH_BSS volatile struct {
    uint32_t stage, command, passed, failed, started_ms, elapsed_ms, poll_count;
    SRNOS_EEPROMStatus status, restore_status;
    bool backup_valid, restored;
    uint32_t backup_hash, restored_hash;
    bsp_i2c_snapshot_t bus_before, bus_after;
    SRNOS_SensorStatus lis_before, lis_after, spa_before, spa_after;
} srnos_eeprom_bringup;
_Alignas(32) uint8_t srnos_eeprom_backup[SRNOS_EEPROM_SIZE_BYTES];
uint8_t *srnos_eeprom_work; /* Test-only heap workspace; not a driver allocation. */
typedef struct {
    uint32_t observations, not_ready, samples, max_gap_us;
    uint64_t first_us, last_us;
} Continuity;
/* phase 1 idle baseline; 2/3 first write/read; 4/5 second write/read;
 * 6 boundary tests; 7/8 restore write/read. Single monitor owns measurements. */
OMX_DEBUG_WATCH_BSS volatile unsigned srnos_eeprom_phase;
OMX_DEBUG_WATCH_BSS volatile Continuity srnos_eeprom_continuity[9][2];
static void monitor(void *argument)
{
    (void)argument;
    for(;;) {
        unsigned phase=srnos_eeprom_phase;
        if(phase>0 && phase<9) for(unsigned n=0;n<2;n++) {
            SRNOS_SensorStatus h;
            SRNOS_SensorGetStatus(n?SRNOS_SENSOR_SPA06:SRNOS_SENSOR_LIS2MDL,&h);
            volatile Continuity *p=&srnos_eeprom_continuity[phase][n];
            ++p->observations;
            if(h.state!=SRNOS_SENSOR_READY) ++p->not_ready;
            if(h.last_sample_us && h.last_sample_us!=p->last_us) {
                if(p->last_us) {
                    uint32_t gap=(uint32_t)(h.last_sample_us-p->last_us);
                    if(gap>p->max_gap_us) p->max_gap_us=gap;
                } else p->first_us=h.last_sample_us;
                p->last_us=h.last_sample_us; ++p->samples;
            }
        }
        osDelay(1);
    }
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c)
{
    c->ins.enabled=false; c->sensors.heater_enabled=false;
    c->load_parameters=false; c->host_link_storage_enabled=false;
    c->diagnostics_enabled=false; c->bringup_indicators=true;
    c->threads[c->thread_count++]=(SRNOS_ThreadConfig){
        .name="EEPROMObserve",.entry=monitor,.stack_bytes=2048,.priority=osPriorityNormal};
}
static uint8_t pattern(size_t i, unsigned seed)
{ return (uint8_t)(i*73U+(i>>8)+seed); }
static void fill(unsigned seed)
{ for(size_t i=0;i<SRNOS_EEPROM_SIZE_BYTES;++i) srnos_eeprom_work[i]=pattern(i,seed); }
static uint32_t hash(const uint8_t *data)
{
    uint32_t h=2166136261U;
    for(size_t i=0;i<SRNOS_EEPROM_SIZE_BYTES;++i) { h^=data[i];h*=16777619U; }
    return h;
}
/* SWD reads physical RAM. Publish cached application buffers before host dump;
 * the command/status lives in existing non-cache D2 Debug Watch memory. */
static void publish_buffer(const void *data)
{
    uintptr_t begin=(uintptr_t)data & ~(uintptr_t)31;
    uintptr_t end=((uintptr_t)data+SRNOS_EEPROM_SIZE_BYTES+31U) & ~(uintptr_t)31;
    SCB_CleanDCache_by_Addr((uint32_t *)begin,(int32_t)(end-begin));
    __DSB();
}
static bool check(SRNOS_EEPROMStatus status)
{
    srnos_eeprom_bringup.status=status;
    if(status==SRNOS_EEPROM_OK) { ++srnos_eeprom_bringup.passed; return true; }
    ++srnos_eeprom_bringup.failed; return false;
}
static bool matches(unsigned seed)
{
    for(size_t i=0;i<SRNOS_EEPROM_SIZE_BYTES;++i)
        if(srnos_eeprom_work[i]!=pattern(i,seed)) return check(SRNOS_EEPROM_VERIFY_ERROR);
    return check(SRNOS_EEPROM_OK);
}
static SRNOS_EEPROMStatus poll(SRNOS_EEPROMStatus status)
{
    if(status!=SRNOS_EEPROM_OK) return status;
    while((status=SRNOS_EEPROMPoll())==SRNOS_EEPROM_IN_PROGRESS) {
        ++srnos_eeprom_bringup.poll_count;osDelay(1);
    }
    return status;
}
static void exercise(void)
{
    srnos_eeprom_bringup.stage=2;fill(17);
    srnos_eeprom_phase=2;
    if(!check(SRNOS_EEPROMWrite(0,srnos_eeprom_work,SRNOS_EEPROM_SIZE_BYTES,100))) return;
    memset(srnos_eeprom_work,0,SRNOS_EEPROM_SIZE_BYTES);
    srnos_eeprom_phase=3;
    if(!check(poll(SRNOS_EEPROMReadAsync(0,srnos_eeprom_work,SRNOS_EEPROM_SIZE_BYTES,100))) || !matches(17)) return;
    srnos_eeprom_bringup.stage=3;fill(191);
    srnos_eeprom_phase=4;
    if(!check(poll(SRNOS_EEPROMWriteAsync(0,srnos_eeprom_work,SRNOS_EEPROM_SIZE_BYTES,100)))) return;
    memset(srnos_eeprom_work,0,SRNOS_EEPROM_SIZE_BYTES);
    srnos_eeprom_phase=5;
    if(!check(SRNOS_EEPROMRead(0,srnos_eeprom_work,SRNOS_EEPROM_SIZE_BYTES,100)) || !matches(191)) return;
    srnos_eeprom_bringup.stage=4;
    srnos_eeprom_phase=6;
    /* Unaligned page and 0xFF/0x100 crossings; high address and last byte. */
    const uint16_t addresses[]={0x003F,0x00FF,0x1FF3,0x3FFF};
    for(unsigned test=0;test<4;++test) {
        uint16_t addr=addresses[test];size_t n=test==3?1:259;
        fill(test+1);
        if(!check(poll(SRNOS_EEPROMWriteAsync(addr,srnos_eeprom_work,n,100)))) return;
        memset(srnos_eeprom_work,0,n);
        if(!check(SRNOS_EEPROMRead(addr,srnos_eeprom_work,n,100))) return;
        for(size_t i=0;i<n;++i) if(srnos_eeprom_work[i]!=pattern(i,test+1)) { check(SRNOS_EEPROM_VERIFY_ERROR);return; }
        check(SRNOS_EEPROM_OK);
    }
    if(SRNOS_EEPROMWriteAsync(0x3FFF,srnos_eeprom_work,2,100)!=SRNOS_EEPROM_INVALID_ARGUMENT ||
       SRNOS_EEPROMReadAsync(0x4000,srnos_eeprom_work,1,100)!=SRNOS_EEPROM_INVALID_ARGUMENT)
        check(SRNOS_EEPROM_VERIFY_ERROR);
    else check(SRNOS_EEPROM_OK);
}
void SRNOS_ApplicationMain(void)
{
    srnos_eeprom_work=pvPortMalloc(SRNOS_EEPROM_SIZE_BYTES);
    if(!srnos_eeprom_work) { srnos_eeprom_bringup.stage=92;return; }
    if(!check(SRNOS_EEPROMRead(0,srnos_eeprom_backup,sizeof(srnos_eeprom_backup),100))) {
        srnos_eeprom_bringup.stage=90;return;
    }
    srnos_eeprom_bringup.backup_hash=hash(srnos_eeprom_backup);
    publish_buffer(srnos_eeprom_backup);
    srnos_eeprom_bringup.backup_valid=true;srnos_eeprom_bringup.stage=1;
    while(srnos_eeprom_bringup.command!=1) osDelay(10);
    srnos_eeprom_bringup.command=0;
    /* Let sensors recover from the host's backup pause, then collect a clean
     * idle baseline. No debugger stops inside the measured workload. */
    osDelay(2000); srnos_eeprom_phase=1; osDelay(3000);
    srnos_eeprom_bringup.started_ms=HAL_GetTick();
    bsp_i2c_get_snapshot(BSP_I2C_BUS_4,(bsp_i2c_snapshot_t *)&srnos_eeprom_bringup.bus_before);
    SRNOS_SensorGetStatus(SRNOS_SENSOR_LIS2MDL,(SRNOS_SensorStatus *)&srnos_eeprom_bringup.lis_before);
    SRNOS_SensorGetStatus(SRNOS_SENSOR_SPA06,(SRNOS_SensorStatus *)&srnos_eeprom_bringup.spa_before);
    exercise();
    srnos_eeprom_bringup.stage=5;
    srnos_eeprom_phase=7;
    srnos_eeprom_bringup.restore_status=SRNOS_EEPROMWrite(0,srnos_eeprom_backup,sizeof(srnos_eeprom_backup),100);
    if(srnos_eeprom_bringup.restore_status==SRNOS_EEPROM_OK) {
        srnos_eeprom_phase=8;
        srnos_eeprom_bringup.restore_status=SRNOS_EEPROMRead(0,srnos_eeprom_work,SRNOS_EEPROM_SIZE_BYTES,100);
        srnos_eeprom_bringup.restored_hash=hash(srnos_eeprom_work);
        srnos_eeprom_bringup.restored=srnos_eeprom_bringup.restore_status==SRNOS_EEPROM_OK &&
            memcmp(srnos_eeprom_work,srnos_eeprom_backup,SRNOS_EEPROM_SIZE_BYTES)==0;
    }
    srnos_eeprom_bringup.elapsed_ms=HAL_GetTick()-srnos_eeprom_bringup.started_ms;
    srnos_eeprom_phase=0;
    publish_buffer(srnos_eeprom_work);
    bsp_i2c_get_snapshot(BSP_I2C_BUS_4,(bsp_i2c_snapshot_t *)&srnos_eeprom_bringup.bus_after);
    SRNOS_SensorGetStatus(SRNOS_SENSOR_LIS2MDL,(SRNOS_SensorStatus *)&srnos_eeprom_bringup.lis_after);
    SRNOS_SensorGetStatus(SRNOS_SENSOR_SPA06,(SRNOS_SensorStatus *)&srnos_eeprom_bringup.spa_after);
    srnos_eeprom_bringup.stage=srnos_eeprom_bringup.restored && !srnos_eeprom_bringup.failed?6:91;
    for(;;) osDelay(1000);
}
