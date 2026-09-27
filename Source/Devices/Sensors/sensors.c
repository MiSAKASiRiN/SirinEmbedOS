#include "sensor_internal.h"
#include "sensor_registers.h"
#include "sensors_board.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <math.h>
static SRNOS_SensorConfig config;
static SRNOS_SensorStatus health[SRNOS_SENSOR_COUNT];
static SRNOS_IMUSample imu[2];
static SRNOS_MagneticSample magnetic;
static SRNOS_PressureSample pressure;
static bsp_spi_device_t lsm_device;
static bsp_i2c_device_t lis_device, spa_device;
static SRNOS_SensorRegisters registers[3];
static uint8_t lsm_tx[15],lsm_rx[15];
static volatile unsigned lsm_inflight;
static uint64_t lsm_start, lsm_ready_since;
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static void poll(SRNOS_SensorId id) {
    uint64_t now=DWT_GetUptimeUs(); uint32_t m=enter(); SRNOS_SensorStatus *h=&health[id];
    if(h->last_poll_us) { uint32_t dt=(uint32_t)(now-h->last_poll_us);
        unsigned i=dt<=500?0:dt<=1000?1:dt<=1500?2:dt<=10000?3:dt<=20000?4:5;
        ++h->period_us[i]; if(dt>h->max_period_us) h->max_period_us=dt;
    }
    h->last_poll_us=now; ++h->polls; leave(m);
}
static void state(SRNOS_SensorId id,SRNOS_SensorState value) {
    uint32_t m=enter(); health[id].state=value; leave(m);
}
static void publish_header(SRNOS_SensorId id, SRNOS_SampleHeader *h,uint64_t start,uint64_t end,uint32_t fields) {
    SRNOS_SensorStatus *s=&health[id];
    h->sequence=++s->samples; h->state=SRNOS_SENSOR_READY; h->valid_fields=fields;
    h->acquisition_start_us=start; h->acquisition_end_us=end;
    s->last_sample_us=end; s->state=SRNOS_SENSOR_READY;
    uint32_t duration=(uint32_t)(end-start);
    if(duration>s->max_acquisition_us) s->max_acquisition_us=duration;
}
static uint32_t stale_limit(SRNOS_SensorId id) {
    return id==SRNOS_SENSOR_SPA06?250000:id==SRNOS_SENSOR_LIS2MDL?100000:
        5000u*(id==SRNOS_SENSOR_BMI088?config.bmi_period_ms:config.lsm_period_ms);
}
static SRNOS_SensorState current_state(SRNOS_SensorId id,uint64_t now) {
    SRNOS_SensorStatus *s=&health[id];
    if(s->state==SRNOS_SENSOR_READY && (!s->last_sample_us || (now>=s->last_sample_us && now-s->last_sample_us>stale_limit(id)))) return SRNOS_SENSOR_STALE;
    return s->state;
}
static void decorate(SRNOS_SensorId id,SRNOS_SampleHeader *h,uint64_t now) {
    h->state=current_state(id,now);
    if(h->state!=SRNOS_SENSOR_READY) h->valid_fields=0;
}
bool SRNOS_SensorGetBusStatistics(SRNOS_SensorId id,SRNOS_SensorBusStatistics *out) {
    if(!out || (unsigned)id>SRNOS_SENSOR_LSM6DS3) return false;
    bsp_spi_statistics_t a={0},b={0};
    if(id==SRNOS_SENSOR_BMI088) BSP_BMI088_GetSPIStatistics(&a,&b);
    else bsp_spi_get_statistics(&lsm_device,&a);
    *out=(SRNOS_SensorBusStatistics){
        .accepted=a.accepted+b.accepted,.completed=a.completed+b.completed,
        .queue_full=a.queue_full+b.queue_full,.queue_expired=a.queue_expired+b.queue_expired,
        .errors=a.errors+b.errors,.timeouts=a.timeouts+b.timeouts,.recoveries=a.recoveries+b.recoveries,
        .max_queue_us=a.max_queue_us>b.max_queue_us?a.max_queue_us:b.max_queue_us,
        .max_transfer_us=a.max_transfer_us>b.max_transfer_us?a.max_transfer_us:b.max_transfer_us};
    for(unsigned i=0;i<6;i++) { out->queue_us[i]=a.queue_us[i]+b.queue_us[i]; out->transfer_us[i]=a.transfer_us[i]+b.transfer_us[i]; }
    return true;
}
bool SRNOS_SensorGetStatus(SRNOS_SensorId id,SRNOS_SensorStatus *out) {
    if((unsigned)id>=SRNOS_SENSOR_COUNT || !out) return false;
    uint64_t now=DWT_GetUptimeUs(); uint32_t m=enter(); *out=health[id]; out->state=current_state(id,now); leave(m); return true;
}
bool SRNOS_SensorGetIMU(SRNOS_SensorId id,SRNOS_IMUSample *out) {
    if((unsigned)id>SRNOS_SENSOR_LSM6DS3 || !out) return false;
    uint64_t now=DWT_GetUptimeUs(); uint32_t m=enter(); *out=imu[id]; decorate(id,&out->header,now); leave(m); return true;
}
bool SRNOS_SensorGetMagnetic(SRNOS_MagneticSample *out) {
    if(!out) return false; uint64_t now=DWT_GetUptimeUs(); uint32_t m=enter(); *out=magnetic; decorate(SRNOS_SENSOR_LIS2MDL,&out->header,now); leave(m); return true;
}
bool SRNOS_SensorGetPressure(SRNOS_PressureSample *out) {
    if(!out) return false; uint64_t now=DWT_GetUptimeUs(); uint32_t m=enter(); *out=pressure; decorate(SRNOS_SENSOR_SPA06,&out->header,now); leave(m); return true;
}
void SRNOS_SensorsPrepare(const SRNOS_SensorConfig *c) {
    config=*c;
    SRNOS_BoardSensorBindings(&lsm_device,&lis_device,&spa_device);
    health[0].state=SRNOS_SENSOR_INITIALIZING;
    health[1].state=c->lsm_enabled?SRNOS_SENSOR_INITIALIZING:SRNOS_SENSOR_DISABLED;
    health[2].state=c->lis_enabled?SRNOS_SENSOR_INITIALIZING:SRNOS_SENSOR_DISABLED;
    health[3].state=c->spa_enabled?SRNOS_SENSOR_INITIALIZING:SRNOS_SENSOR_DISABLED;
    for(unsigned i=0;i<3;i++) { registers[i].id=(SRNOS_SensorId)(i+1); registers[i].state=health[i+1].state; }
}
static bool spi_read(void *ctx,uint8_t reg,uint8_t *out,size_t n) {
    uint8_t tx[24],rx[24]; if(n+1>sizeof(tx)) return false;
    memset(tx,0xff,n+1); tx[0]=reg|0x80;
    if(bsp_spi_transfer_blocking(ctx,tx,rx,n+1,5)!=BSP_SPI_OK) return false;
    memcpy(out,rx+1,n); return true;
}
static bool spi_write(void *ctx,uint8_t reg,uint8_t value) {
    uint8_t tx[2]={reg,value}; return bsp_spi_transfer_blocking(ctx,tx,NULL,2,5)==BSP_SPI_OK;
}
static bool i2c_read(void *ctx,uint8_t reg,uint8_t *out,size_t n) {
    if(ctx==&lis_device && n>1) reg|=0x80; /* LIS I2C SUB[7] enables auto increment. */
    return bsp_i2c_mem_read(ctx,reg,I2C_MEMADD_SIZE_8BIT,out,n,NULL,NULL)==BSP_I2C_OK &&
        bsp_i2c_wait_complete(ctx,BSP_I2C_PERIODIC_WAIT_MS)==BSP_I2C_OK;
}
static bool i2c_write(void *ctx,uint8_t reg,uint8_t value) {
    return bsp_i2c_mem_write(ctx,reg,I2C_MEMADD_SIZE_8BIT,&value,1,NULL,NULL)==BSP_I2C_OK &&
        bsp_i2c_wait_complete(ctx,BSP_I2C_PERIODIC_WAIT_MS)==BSP_I2C_OK;
}
static void init_step(unsigned index,const SRNOS_SensorRegisterIO *io) {
    SRNOS_SensorRegisters *r=&registers[index]; SRNOS_SensorState before=r->state;
    uint64_t now=DWT_GetUptimeUs(), previous_retry=r->retry_us;
    if(r->phase==0 && now>=r->retry_us) ++health[index+1].init_attempts;
    SRNOS_SensorInitStep(r,io,now);
    uint32_t m=enter(); SRNOS_SensorStatus *h=&health[index+1]; h->state=r->state; h->identity=r->identity;
    if(r->id==SRNOS_SENSOR_SPA06) { h->product=r->identity&15; h->revision=r->identity>>4; }
    memcpy(h->config_readback,r->readback,sizeof(h->config_readback));
    if(before!=r->state && r->state==SRNOS_SENSOR_READY) { if(h->initializations++) ++h->recoveries; }
    if(r->retry_us!=previous_retry) ++h->errors;
    leave(m);
}
static void failed(unsigned index,SRNOS_SensorState why) {
    SRNOS_SensorInitFailed(&registers[index],why,DWT_GetUptimeUs());
    uint32_t m=enter(); health[index+1].state=why; ++health[index+1].errors; leave(m);
}
static void lsm_callback(bsp_spi_device_t *dev,void *ctx,bsp_spi_status_t result) {
    (void)dev; (void)ctx;
    if(result!=BSP_SPI_OK) { lsm_inflight=0; failed(0,SRNOS_SENSOR_OFFLINE); return; }
    if(lsm_inflight==1) {
        if((lsm_rx[1]&3)!=3) { ++health[1].not_ready; lsm_inflight=0; return; }
        memset(lsm_tx,0xff,sizeof(lsm_tx)); lsm_tx[0]=0x20|0x80; lsm_inflight=2;
        if(bsp_spi_transfer_async(&lsm_device,lsm_tx,lsm_rx,15,lsm_callback,NULL)!=BSP_SPI_OK) {
            lsm_inflight=0; failed(0,SRNOS_SENSOR_OFFLINE);
        }
    } else if(lsm_inflight==2) {
        SRNOS_IMUSample sample={0}; SRNOS_LSMConvert(lsm_rx+1,&sample);
        uint64_t end=DWT_GetUptimeUs(); uint32_t m=enter();
        publish_header(SRNOS_SENSOR_LSM6DS3,&sample.header,lsm_start,end,SRNOS_SENSOR_ACCEL|SRNOS_SENSOR_GYRO|SRNOS_SENSOR_TEMPERATURE);
        imu[1]=sample; leave(m); lsm_inflight=0;
        if(config.imu_observer) config.imu_observer(SRNOS_SENSOR_LSM6DS3,&sample,config.imu_observer_user);
    }
}
/* Never issue catch-up bursts after preemption or recovery. */
static void wait_period(TickType_t *last,uint32_t ms) {
    TickType_t step=pdMS_TO_TICKS(ms), now=xTaskGetTickCount();
    if(now-*last>=step) *last=now;
    vTaskDelayUntil(last,step);
}
void SRNOS_LSMWorker(void *arg) {
    (void)arg; SRNOS_SensorRegisterIO io={&lsm_device,spi_read,spi_write};
    TickType_t last=xTaskGetTickCount();
    for(;;) {
        poll(SRNOS_SENSOR_LSM6DS3);
        if(lsm_inflight) ++health[1].overruns;
        else if(registers[0].state!=SRNOS_SENSOR_READY) {
            init_step(0,&io); lsm_ready_since=DWT_GetUptimeUs();
        } else if(DWT_GetUptimeUs()-(health[1].last_sample_us>lsm_ready_since?health[1].last_sample_us:lsm_ready_since)>100000) {
            failed(0,SRNOS_SENSOR_STALE);
        } else {
            lsm_start=DWT_GetUptimeUs(); lsm_inflight=1; lsm_tx[0]=0x1e|0x80; lsm_tx[1]=0xff;
            if(bsp_spi_transfer_async(&lsm_device,lsm_tx,lsm_rx,2,lsm_callback,NULL)!=BSP_SPI_OK) { lsm_inflight=0; ++health[1].overruns; }
        }
        wait_period(&last,config.lsm_period_ms);
    }
}
void SRNOS_EnvironmentWorker(void *arg) {
    (void)arg;
    SRNOS_SensorRegisterIO io[2]={{&lis_device,i2c_read,i2c_write},{&spa_device,i2c_read,i2c_write}};
    uint64_t ready_since[2]={0,0}; TickType_t last=xTaskGetTickCount();
    for(;;) {
        for(unsigned n=0;n<2;n++) {
            if(n==0?!config.lis_enabled:!config.spa_enabled) continue;
            unsigned index=n+1; SRNOS_SensorId id=(SRNOS_SensorId)(n+2); poll(id);
            if(registers[index].state!=SRNOS_SENSOR_READY) { init_step(index,&io[n]); ready_since[n]=DWT_GetUptimeUs(); continue; }
            uint64_t start=DWT_GetUptimeUs(); uint8_t status=0,bytes[8];
            if(!i2c_read(io[n].context,n?0x08:0x67,&status,1)) { failed(index,SRNOS_SENSOR_OFFLINE); continue; }
            if(n?((status&0x30)!=0x30):((status&8)==0)) {
                ++health[id].not_ready;
                uint64_t fresh=health[id].last_sample_us>ready_since[n]?health[id].last_sample_us:ready_since[n];
                if(start-fresh>500000) failed(index,SRNOS_SENSOR_STALE);
                continue;
            }
            if(!i2c_read(io[n].context,n?0:0x68,bytes,6) || (!n && !i2c_read(io[n].context,0x6e,bytes+6,2))) { failed(index,SRNOS_SENSOR_OFFLINE); continue; }
            uint64_t end=DWT_GetUptimeUs();
            if(n) {
                SRNOS_PressureSample sample={0}; SRNOS_SPAConvert(&registers[index].coefficients,bytes,&sample);
                if(!isfinite(sample.pressure_pa) || !isfinite(sample.temperature_c)) { failed(index,SRNOS_SENSOR_CONFIG_ERROR); continue; }
                uint32_t m=enter(); publish_header(id,&sample.header,start,end,SRNOS_SENSOR_PRESSURE|SRNOS_SENSOR_TEMPERATURE); pressure=sample; leave(m);
            } else {
                SRNOS_MagneticSample sample={0}; SRNOS_LISConvert(bytes,&sample);
                uint32_t m=enter(); publish_header(id,&sample.header,start,end,SRNOS_SENSOR_MAG|SRNOS_SENSOR_TEMPERATURE); magnetic=sample; leave(m);
            }
        }
        wait_period(&last,config.environment_period_ms);
    }
}
static void bmi_complete(void *thread) { (void)osThreadFlagsSet(thread,1u); }
static bsp_bmi088_status_t collect_bmi(void) {
    bmi088_data_t raw;
    bsp_bmi088_status_t result=BSP_BMI088_GetSampleIfReady(&raw);
    if(result!=BSP_BMI088_OK) return result;
    SRNOS_IMUSample sample={0};
    for(unsigned i=0;i<3;i++) {
        sample.raw_accel[i]=raw.raw_accel[i];sample.raw_gyro[i]=raw.raw_gyro[i];
        sample.accel_m_s2[i]=raw.accel[i];sample.gyro_rad_s[i]=raw.gyro[i]*0.017453292519943295f;
    }
    sample.raw_temperature=raw.raw_temperature;sample.temperature_c=raw.temperature;
    uint32_t m=enter();
    publish_header(0,&sample.header,raw.acquisition_start_us,raw.acquisition_end_us,
        SRNOS_SENSOR_ACCEL|SRNOS_SENSOR_GYRO|SRNOS_SENSOR_TEMPERATURE);
    imu[0]=sample;leave(m);
    if(config.imu_observer) config.imu_observer(SRNOS_SENSOR_BMI088,&sample,config.imu_observer_user);
    return BSP_BMI088_OK;
}
void SRNOS_BMIWorker(void *arg) {
    (void)arg;bool ready=false,inflight=false;uint64_t retry=0;
    osThreadId_t self=osThreadGetId();TickType_t last=xTaskGetTickCount();
    for(;;) {
        poll(SRNOS_SENSOR_BMI088);
        if(!ready) {
            if(DWT_GetUptimeUs()>=retry && BSP_BMI088_ResetAsyncState()!=BSP_BMI088_BUSY) {
                state(0,SRNOS_SENSOR_INITIALIZING);++health[0].init_attempts;
                ready=SRNOS_BoardBMIInit(bmi_complete,self)==BSP_BMI088_OK;
                state(0,ready?SRNOS_SENSOR_READY:SRNOS_SENSOR_OFFLINE);
                if(ready) {if(health[0].initializations++)++health[0].recoveries;health[0].identity=0x1e;}
                else {++health[0].errors;retry=DWT_GetUptimeUs()+1000000;}
            }
        } else {
            bsp_bmi088_status_t result=BSP_BMI088_OK;
            if(inflight) {
                result=collect_bmi();inflight=result==BSP_BMI088_BUSY;
                if(inflight) ++health[0].overruns;
            }
            if(result==BSP_BMI088_OK && !inflight) {
                (void)osThreadFlagsClear(1u);
                result=BSP_BMI088_BeginSample();inflight=result==BSP_BMI088_OK;
                if(inflight) {
                    /* DMA completion wakes this existing sampling task. Publish
                     * within the current period instead of retaining an extra
                     * 1ms-old frame. No extra worker or blocking SPI callback. */
                    (void)osThreadFlagsWait(1u,osFlagsWaitAny,pdMS_TO_TICKS(1));
                    result=collect_bmi();inflight=result==BSP_BMI088_BUSY;
                } else if(result==BSP_BMI088_BUSY) ++health[0].overruns;
            }
            if(result!=BSP_BMI088_OK && result!=BSP_BMI088_BUSY) {
                ++health[0].errors;ready=false;inflight=false;state(0,SRNOS_SENSOR_OFFLINE);retry=DWT_GetUptimeUs()+1000000;
            }
        }
        wait_period(&last,config.bmi_period_ms);
    }
}
