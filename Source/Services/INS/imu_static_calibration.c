#include "imu_calibration_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "thread_internal.h"
#include "stm32h7xx.h"
#include "cmsis_os2.h"
#include <math.h>
#include <string.h>

#define SRNOS_IMU_STATIC_WINDOW_MS 5000u
#define SRNOS_IMU_STATIC_TOTAL_MS 20000u
#define SRNOS_IMU_STATIC_POLL_MS 10u
#define SRNOS_IMU_STATIC_MIN_SAMPLES 400u
/* Raw-sample rejection thresholds: stationary board measurements on 2026-09-29
 * showed gyro sigma up to .0112 rad/s and span .0853 rad/s. Leave noise margin
 * without changing ODR/filtering; these are not an accuracy guarantee. */
#define SRNOS_IMU_STATIC_GYRO_LIMIT .10f
#define SRNOS_IMU_STATIC_GYRO_SPAN .12f
#define SRNOS_IMU_STATIC_GYRO_VARIANCE (.02f*.02f)
#define SRNOS_IMU_STATIC_ACCEL_SPAN .40f
#define SRNOS_IMU_STATIC_ACCEL_VARIANCE (.08f*.08f)
#define SRNOS_IMU_STATIC_TEMP_SPAN 1.0f

typedef struct {
    float mean[6], m2[6], minimum[6], maximum[6], temp_mean, temp_min, temp_max;
    uint64_t first_us, last_us;
    uint32_t sequence, count;
    SRNOS_SensorStatus health;
    SRNOS_IMUCalibration value;
} SRNOS_StaticAccumulator;
/* One writer owns this workspace through the calibration save gate. */
static SRNOS_StaticAccumulator accum[2];
static SRNOS_IMUStaticStatus published = {.failed_sensor=UINT8_MAX};
static uint32_t next_job;
static uint32_t enter(void) {uint32_t k=__get_PRIMASK();__disable_irq();return k;}
static void leave(uint32_t k) {__set_PRIMASK(k);}
static void publish(const SRNOS_IMUStaticStatus *s) {uint32_t k=enter();published=*s;leave(k);}
bool SRNOS_IMUCalibrationGetStaticStatus(SRNOS_IMUStaticStatus *out) {
    if(!out || !SRNOS_KernelTaskContext())return false;
    uint32_t k=enter();*out=published;leave(k);return true;
}
static uint32_t remaining(uint32_t started,uint32_t maximum) {
    uint32_t elapsed=SRNOS_TimeMs()-started;
    if(elapsed>=SRNOS_IMU_STATIC_TOTAL_MS)return 0;
    uint32_t left=SRNOS_IMU_STATIC_TOTAL_MS-elapsed;return left<maximum?left:maximum;
}
/* Keep sample/health locals off the EEPROM call chain on the 2 KiB HostCmd stack. */
static __attribute__((noinline)) SRNOS_IMUStaticResult observe(unsigned id,SRNOS_StaticAccumulator *a) {
    SRNOS_IMUSample sample;SRNOS_SensorStatus health;
    if(!SRNOS_SensorGetIMU((SRNOS_SensorId)id,&sample) ||
       !SRNOS_SensorGetStatus((SRNOS_SensorId)id,&health))return SRNOS_IMU_STATIC_SENSOR_ERROR;
    const uint32_t required=SRNOS_SENSOR_ACCEL|SRNOS_SENSOR_GYRO|SRNOS_SENSOR_TEMPERATURE;
    if(sample.header.state!=SRNOS_SENSOR_READY || health.state!=SRNOS_SENSOR_READY ||
       (sample.header.valid_fields&required)!=required || health.errors!=a->health.errors ||
       health.overruns!=a->health.overruns || health.recoveries!=a->health.recoveries)
        return SRNOS_IMU_STATIC_SENSOR_ERROR;
    uint64_t now=SRNOS_TimeUs(),stamp=sample.header.acquisition_end_us;
    if(stamp>now || now-stamp>20000u || sample.header.acquisition_start_us>stamp ||
       (a->count && (stamp<=a->last_us || stamp-a->last_us>40000u ||
        (uint32_t)(sample.header.sequence-a->sequence)==0 ||
        (uint32_t)(sample.header.sequence-a->sequence)>INT32_MAX)))return SRNOS_IMU_STATIC_STALE;
    if(!isfinite(sample.temperature_c) || sample.temperature_c < -40 || sample.temperature_c > 125)
        return SRNOS_IMU_STATIC_SENSOR_ERROR;
    float norm=0;
    for(unsigned i=0;i<3;i++) {
        if(!isfinite(sample.gyro_rad_s[i]) || !isfinite(sample.accel_m_s2[i]))return SRNOS_IMU_STATIC_SENSOR_ERROR;
        if(fabsf(sample.gyro_rad_s[i])>SRNOS_IMU_STATIC_GYRO_LIMIT)return SRNOS_IMU_STATIC_MOVING;
        norm+=sample.accel_m_s2[i]*sample.accel_m_s2[i];
    }
    if(norm<8.8f*8.8f || norm>10.8f*10.8f)return SRNOS_IMU_STATIC_MOVING;
    for(unsigned i=0;i<6;i++) {
        float v=i<3?sample.gyro_rad_s[i]:sample.accel_m_s2[i-3];
        if(!a->count)a->minimum[i]=a->maximum[i]=v;
        if(v<a->minimum[i])a->minimum[i]=v;if(v>a->maximum[i])a->maximum[i]=v;
        if(a->maximum[i]-a->minimum[i]>(i<3?SRNOS_IMU_STATIC_GYRO_SPAN:SRNOS_IMU_STATIC_ACCEL_SPAN))
            return SRNOS_IMU_STATIC_MOVING;
        float delta=v-a->mean[i];a->mean[i]+=delta/(float)(a->count+1u);a->m2[i]+=delta*(v-a->mean[i]);
    }
    if(!a->count) {a->temp_min=a->temp_max=sample.temperature_c;a->first_us=stamp;}
    if(sample.temperature_c<a->temp_min)a->temp_min=sample.temperature_c;
    if(sample.temperature_c>a->temp_max)a->temp_max=sample.temperature_c;
    if(a->temp_max-a->temp_min>SRNOS_IMU_STATIC_TEMP_SPAN)return SRNOS_IMU_STATIC_TEMPERATURE;
    a->temp_mean+=(sample.temperature_c-a->temp_mean)/(float)(a->count+1u);
    a->count++;a->last_us=stamp;a->sequence=sample.header.sequence;
    return SRNOS_IMU_STATIC_OK;
}
static __attribute__((noinline)) SRNOS_IMUStaticResult thermal_health(void) {
    SRNOS_HeaterStatus h;
    if(!SRNOS_SensorGetHeaterStatus(&h))return SRNOS_IMU_STATIC_SENSOR_ERROR;
    return h.enabled && (h.faulted || h.overtemperature || h.hardware_fault || h.feedback_missing)?
        SRNOS_IMU_STATIC_TEMPERATURE:SRNOS_IMU_STATIC_OK;
}
static SRNOS_IMUStaticResult finish(SRNOS_IMUStaticStatus *s,uint32_t started) {
    s->elapsed_ms=SRNOS_TimeMs()-started;
    s->phase=s->result==SRNOS_IMU_STATIC_OK?SRNOS_IMU_STATIC_DONE:SRNOS_IMU_STATIC_FAILED;
    publish(s);SRNOS_IMUCalibrationEndSave();return s->result;
}
SRNOS_IMUStaticResult SRNOS_IMUCalibrationRunStatic(uint8_t mask,SRNOS_IMUStaticStatus *out) {
    if(!out)return SRNOS_IMU_STATIC_INVALID;
    *out=(SRNOS_IMUStaticStatus){.requested_mask=mask,.failed_sensor=UINT8_MAX,.phase=SRNOS_IMU_STATIC_FAILED};
    if(!mask || (mask&~SRNOS_IMU_CALIBRATION_BOTH_MASK))return out->result=SRNOS_IMU_STATIC_INVALID;
    if(!SRNOS_KernelTaskContext() || __get_PRIMASK() || __get_BASEPRI())return out->result=SRNOS_IMU_STATIC_CONTEXT;
    SRNOS_IMUCalibrationStatus cal;
    if(!SRNOS_IMUCalibrationGetStatus(&cal) || !cal.startup_resolved)return out->result=SRNOS_IMU_STATIC_NOT_READY;
    SRNOS_StorageStatus gate=SRNOS_IMUCalibrationBeginSave();
    if(gate!=SRNOS_STORAGE_OK)return out->result=gate==SRNOS_STORAGE_BUSY?SRNOS_IMU_STATIC_BUSY:SRNOS_IMU_STATIC_CONTEXT;
    uint32_t started=SRNOS_TimeMs();out->job_id=++next_job;
    out->phase=SRNOS_IMU_STATIC_PREPARING;publish(out);memset(accum,0,sizeof(accum));
    for(unsigned i=0;i<2;i++)if(mask&(1u<<i)) {
        out->failed_sensor=(uint8_t)i;
        uint32_t left=remaining(started,SRNOS_STORAGE_READ_TIMEOUT_MS);
        if(!left) {out->result=SRNOS_IMU_STATIC_TIMEOUT;return finish(out,started);}
        SRNOS_StaticAccumulator *a=&accum[i];
        SRNOS_StorageStatus st=SRNOS_IMUCalibrationReadStored((SRNOS_SensorId)i,&a->value,left);
        out->storage_status[i]=st;
        if(st==SRNOS_STORAGE_NOT_FOUND || st==SRNOS_STORAGE_CORRUPT || st==SRNOS_STORAGE_INCOMPATIBLE) {
            a->value=(SRNOS_IMUCalibration){.sensor=(SRNOS_SensorId)i,.model_version=1,.axes_version=1,.accel_scale={1,1,1}};
        } else if(st!=SRNOS_STORAGE_OK) {out->result=SRNOS_IMU_STATIC_STORAGE_ERROR;return finish(out,started);}
        if(!SRNOS_SensorGetStatus((SRNOS_SensorId)i,&a->health) || a->health.state!=SRNOS_SENSOR_READY) {
            out->result=SRNOS_IMU_STATIC_NOT_READY;return finish(out,started);
        }
    }
    uint32_t window_start=SRNOS_TimeMs();out->phase=SRNOS_IMU_STATIC_SAMPLING;out->failed_sensor=UINT8_MAX;publish(out);
    for(;;) {
        if(!remaining(started,1)) {out->result=SRNOS_IMU_STATIC_TIMEOUT;return finish(out,started);}
        out->result=thermal_health();if(out->result!=SRNOS_IMU_STATIC_OK)return finish(out,started);
        for(unsigned i=0;i<2;i++)if(mask&(1u<<i)) {
            out->result=observe(i,&accum[i]);out->samples[i]=accum[i].count;
            if(out->result!=SRNOS_IMU_STATIC_OK) {out->failed_sensor=(uint8_t)i;return finish(out,started);}
        }
        out->elapsed_ms=SRNOS_TimeMs()-started;
        if((SRNOS_TimeMs()-window_start)>=SRNOS_IMU_STATIC_WINDOW_MS)break;
        if((accum[(mask&1u)?0:1].count%10u)==0)publish(out);
        osDelay(SRNOS_IMU_STATIC_POLL_MS);
    }
    /* Validate every selected sensor before writing either record. */
    for(unsigned i=0;i<2;i++)if(mask&(1u<<i)) {
        SRNOS_StaticAccumulator *a=&accum[i];out->failed_sensor=(uint8_t)i;
        if(a->count<SRNOS_IMU_STATIC_MIN_SAMPLES || a->last_us-a->first_us<4900000u) {
            out->result=SRNOS_IMU_STATIC_STALE;return finish(out,started);
        }
        for(unsigned j=0;j<6;j++)if(a->m2[j]/(float)(a->count-1u)>
            (j<3?SRNOS_IMU_STATIC_GYRO_VARIANCE:SRNOS_IMU_STATIC_ACCEL_VARIANCE)) {
            out->result=SRNOS_IMU_STATIC_MOVING;return finish(out,started);
        }
        for(unsigned j=0;j<3;j++)out->gyro_bias_rad_s[i][j]=a->value.gyro_bias_rad_s[j]=a->mean[j];
        out->temperature_c[i]=a->value.temperature_c=a->temp_mean;
        if(SRNOS_IMUCalibrationValidate(&a->value)!=SRNOS_STORAGE_OK) {
            out->result=SRNOS_IMU_STATIC_SENSOR_ERROR;return finish(out,started);
        }
    }
    out->failed_sensor=UINT8_MAX;out->phase=SRNOS_IMU_STATIC_SAVING;publish(out);
    for(unsigned i=0;i<2;i++)if(mask&(1u<<i)) {
        uint32_t left=remaining(started,SRNOS_STORAGE_WRITE_TIMEOUT_MS);
        if(!left){out->failed_sensor=(uint8_t)i;out->result=SRNOS_IMU_STATIC_TIMEOUT;return finish(out,started);}
        out->storage_status[i]=SRNOS_IMUCalibrationWriteLocked(&accum[i].value,left);
        if(out->storage_status[i]!=SRNOS_STORAGE_OK) {
            out->failed_sensor=(uint8_t)i;out->result=SRNOS_IMU_STATIC_STORAGE_ERROR;return finish(out,started);
        }
        out->saved_mask|=(uint8_t)(1u<<i);
        SRNOS_IMUCalibrationGetStatus(&cal);out->saved_revision[i]=cal.saved_revision[i];publish(out);
    }
    out->result=SRNOS_IMU_STATIC_OK;return finish(out,started);
}
