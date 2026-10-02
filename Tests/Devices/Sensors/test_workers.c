#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../../Source/Devices/Sensors/sensors.c"
uint32_t SRNOS_TestPrimask;
void SRNOS_TestPreempt(void) { assert(SRNOS_TestPrimask == 0); }
static uint64_t now_us, sample_start;
static uint32_t iterations, limit, begins, sample_duration;
static bool pending;
static jmp_buf stop;
static size_t spi_len;
static uint8_t spi_command;
static SRNOS_SPICallback spi_callback;
uint64_t SRNOS_TimeGetUptimeUs(void) { return now_us; }
TickType_t xTaskGetTickCount(void) { return now_us/1000; }
void vTaskDelayUntil(TickType_t *last,TickType_t step) {
    *last+=step; now_us=(uint64_t)*last*1000;
    if(++iterations==limit) longjmp(stop,1);
}
void SRNOS_BMI088GetSPIStatistics(SRNOS_SPIStatistics *a,SRNOS_SPIStatistics *b) { memset(a,0,sizeof(*a)); memset(b,0,sizeof(*b)); }
void SRNOS_SPIGetStatistics(const SRNOS_SPIDevice *d,SRNOS_SPIStatistics *s) { *s=d->statistics; }
void SRNOS_BoardSensorBindings(SRNOS_SPIDevice *a,SRNOS_I2CDevice *b,SRNOS_I2CDevice *c) { (void)a;(void)b;(void)c; }
static SRNOS_BMI088CompletionFn complete;
static void *complete_user;
osThreadId_t osThreadGetId(void) { return (void*)1; }
uint32_t osThreadFlagsSet(osThreadId_t id,uint32_t flags) { assert(id==(void*)1);return flags; }
uint32_t osThreadFlagsClear(uint32_t flags) { return flags; }
uint32_t osThreadFlagsWait(uint32_t flags,uint32_t mode,uint32_t timeout) {
    (void)mode;assert(pending);uint64_t finish=sample_start+sample_duration;
    if(finish<=now_us+1000*timeout) {now_us=finish;complete(complete_user);return flags;}
    now_us+=timeout*1000;return 0;
}
SRNOS_BMI088Status SRNOS_BoardBMIInit(SRNOS_BMI088CompletionFn cb,void *user) { complete=cb;complete_user=user;return SRNOS_BMI088_OK; }
SRNOS_BMI088Status SRNOS_BMI088ResetAsyncState(void) { pending=false; return SRNOS_BMI088_OK; }
SRNOS_BMI088Status SRNOS_BMI088BeginSample(void) { assert(!pending); pending=true; sample_start=now_us; ++begins; return SRNOS_BMI088_OK; }
SRNOS_BMI088Status SRNOS_BMI088GetSampleIfReady(SRNOS_BMI088Data *out) {
    assert(pending); if(now_us-sample_start<sample_duration) return SRNOS_BMI088_BUSY;
    memset(out,0,sizeof(*out)); out->accel[2]=9.80665f; out->gyro[0]=180;
    out->acquisition_start_us=sample_start; out->acquisition_end_us=sample_start+sample_duration;
    pending=false; return SRNOS_BMI088_OK;
}
SRNOS_SPIStatus SRNOS_SPITransferEx(SRNOS_SPIDevice *dev,const uint8_t *tx,uint8_t *rx,size_t n,SRNOS_SPICallback cb,void *ctx,uint8_t sync,uint32_t ms) {
    (void)dev;(void)rx;(void)ctx;(void)sync;(void)ms; spi_len=n; spi_command=tx[0]; spi_callback=cb; return SRNOS_SPI_OK;
}
SRNOS_I2CStatus SRNOS_I2CMemRead(SRNOS_I2CDevice *d,uint16_t reg,uint16_t size,uint8_t *out,size_t n,SRNOS_I2CCallback cb,void *ctx) { (void)d;(void)reg;(void)size;(void)out;(void)n;(void)cb;(void)ctx; return SRNOS_I2C_ERROR; }
SRNOS_I2CStatus SRNOS_I2CMemWrite(SRNOS_I2CDevice *d,uint16_t reg,uint16_t size,const uint8_t *in,size_t n,SRNOS_I2CCallback cb,void *ctx) { (void)d;(void)reg;(void)size;(void)in;(void)n;(void)cb;(void)ctx; return SRNOS_I2C_ERROR; }
SRNOS_I2CStatus SRNOS_I2CWaitComplete(SRNOS_I2CDevice *d,uint32_t timeout) { (void)d;(void)timeout; return SRNOS_I2C_ERROR; }
static unsigned observed[2];
static void observer(SRNOS_SensorId id,const SRNOS_IMUSample *s,void *user) {
    (void)user; assert(id<=SRNOS_SENSOR_LSM6DS3);
    assert(s->header.sequence==++observed[id] && s->header.valid_fields==19);
    SRNOS_IMUSample published; assert(SRNOS_SensorGetIMU(id,&published));
    assert(published.header.sequence==s->header.sequence);
}
static void reset(void) {
    memset(health,0,sizeof(health)); memset(imu,0,sizeof(imu)); memset(registers,0,sizeof(registers));
    memset(observed,0,sizeof(observed));
    config=(SRNOS_SensorConfig){.imu_observer=observer,.bmi_period_ms=1,.lsm_period_ms=1,.environment_period_ms=10};
    now_us=1000; begins=iterations=0; pending=false; limit=1002;
}
int main(void) {
    reset(); sample_duration=70;
    if(!setjmp(stop)) SRNOS_BMIWorker(NULL);
    assert(observed[0]==1001);
    assert(health[0].samples==1001 && begins==1001 && health[0].overruns==0);
    SRNOS_IMUSample s; assert(SRNOS_SensorGetIMU(0,&s));
    assert(s.header.sequence==1001 && s.header.valid_fields==19 && s.gyro_rad_s[0]>3.14f && s.gyro_rad_s[0]<3.15f);
    assert(now_us-s.header.acquisition_end_us<=1000); /* no extra publication-period latency */
    uint64_t timestamp=s.header.acquisition_end_us; now_us+=10000;
    assert(SRNOS_SensorGetIMU(0,&s) && s.header.state==SRNOS_SENSOR_STALE && !s.header.valid_fields && s.header.acquisition_end_us==timestamp);
    state(0,SRNOS_SENSOR_OFFLINE); assert(SRNOS_SensorGetIMU(0,&s) && s.header.state==SRNOS_SENSOR_OFFLINE && s.header.sequence==1001);
    assert(!SRNOS_SensorGetIMU(SRNOS_SENSOR_LIS2MDL,&s) && !SRNOS_SensorGetStatus(SRNOS_SENSOR_COUNT,NULL));
    reset(); sample_duration=3000; if(!setjmp(stop)) SRNOS_BMIWorker(NULL);
    assert(health[0].samples<=(now_us-1000)/3000 && begins-health[0].samples<=1 && health[0].overruns>0); /* one round only, no backlog */
    reset(); lsm_inflight=1; lsm_rx[1]=0; lsm_callback(&lsm_device,NULL,SRNOS_SPI_OK);
    assert(health[1].not_ready==1 && !lsm_inflight && !health[1].samples);
    lsm_inflight=1; lsm_rx[1]=3; lsm_callback(&lsm_device,NULL,SRNOS_SPI_OK);
    assert(lsm_inflight==2 && spi_len==15 && spi_command==0xa0 && spi_callback==lsm_callback);
    memset(lsm_rx,0,sizeof(lsm_rx)); lsm_start=now_us; now_us+=50;
    lsm_callback(&lsm_device,NULL,SRNOS_SPI_OK);
    assert(!lsm_inflight && health[1].samples==1 && SRNOS_SensorGetIMU(1,&s) && s.header.valid_fields==19);
    assert(observed[1]==1);
    lsm_inflight=1; lsm_callback(&lsm_device,NULL,SRNOS_SPI_TIMEOUT);
    assert(!lsm_inflight && health[1].state==SRNOS_SENSOR_OFFLINE && SRNOS_SensorGetIMU(1,&s) && !s.header.valid_fields && s.header.sequence==1);
    puts("Actual sensor workers: BMI 1000Hz pipeline, slow-transfer skip, LSM ready/burst/failure, stale retained snapshots passed; no fusion/calibration/EEPROM symbols linked");
}
