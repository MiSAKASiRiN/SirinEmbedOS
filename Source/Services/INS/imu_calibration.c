#include "imu_calibration_internal.h"
#include "Services/Storage/storage_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "thread_internal.h"
#include "stm32h7xx.h"
#include <math.h>
#include <string.h>

#define SRNOS_IMU_CALIBRATION_OWNER UINT32_C(0x53494D55)
#define SRNOS_IMU_CALIBRATION_BYTES 60u
static SRNOS_IMUCalibration active[2];
static SRNOS_IMUCalibrationStatus status;
static uint32_t startup_deadline;
static bool startup_started, loading, saving;
static uint8_t loaded_mask;
static uint32_t enter(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void leave(uint32_t m) { __set_PRIMASK(m); }
static bool sensor_valid(SRNOS_SensorId id) { return id==SRNOS_SENSOR_BMI088 || id==SRNOS_SENSOR_LSM6DS3; }
static SRNOS_StorageRecord record(SRNOS_SensorId id) {
    return id==SRNOS_SENSOR_BMI088?SRNOS_STORAGE_RECORD_BMI:SRNOS_STORAGE_RECORD_LSM;
}
static uint32_t remaining(uint32_t deadline) {
    int32_t left=(int32_t)(deadline-SRNOS_TimeMs()); return left>0?(uint32_t)left:0;
}
SRNOS_StorageStatus SRNOS_IMUCalibrationValidate(const SRNOS_IMUCalibration *v) {
    if(!v || !sensor_valid(v->sensor) || (v->model_version!=1 && v->model_version!=SRNOS_IMU_CALIBRATION_MODEL_VERSION) ||
       v->axes_version!=SRNOS_IMU_CALIBRATION_SENSOR_AXES_VERSION) return SRNOS_STORAGE_INVALID_ARGUMENT;
    if(!isfinite(v->temperature_c) || v->temperature_c < -40 || v->temperature_c > 125)
        return SRNOS_STORAGE_INVALID_ARGUMENT;
    for(unsigned i=0;i<3;i++) {
        if(v->model_version>=2 && (!isfinite(v->gyro_scale[i]) || v->gyro_scale[i]<.8f || v->gyro_scale[i]>1.2f))
            return SRNOS_STORAGE_INVALID_ARGUMENT;
        if(!isfinite(v->gyro_bias_rad_s[i]) || fabsf(v->gyro_bias_rad_s[i])>1 ||
           !isfinite(v->accel_bias_m_s2[i]) || fabsf(v->accel_bias_m_s2[i])>9.80665f ||
           !isfinite(v->accel_scale[i]) || v->accel_scale[i]<.5f || v->accel_scale[i]>1.5f)
            return SRNOS_STORAGE_INVALID_ARGUMENT;
    }
    return SRNOS_STORAGE_OK;
}
static void encode(const SRNOS_IMUCalibration *v,uint8_t out[SRNOS_IMU_CALIBRATION_BYTES]) {
    memset(out,0,SRNOS_IMU_CALIBRATION_BYTES);
    SRNOS_Memory_MutableByteSpan bytes=SRNOS_Memory_MutableBytes(out,SRNOS_IMU_CALIBRATION_BYTES);
    SRNOS_Endian_StoreLE16(bytes,0,(uint16_t)v->sensor);
    SRNOS_Endian_StoreLE16(bytes,2,v->model_version); SRNOS_Endian_StoreLE16(bytes,4,v->axes_version);
    for(unsigned i=0;i<13;i++) {
        float value=i<3?v->gyro_bias_rad_s[i]:i<6?v->accel_bias_m_s2[i-3]:i<9?v->accel_scale[i-6]:i==9?v->temperature_c:(v->model_version>=2?v->gyro_scale[i-10]:1.0f);
        uint32_t bits; memcpy(&bits,&value,4); SRNOS_Endian_StoreLE32(bytes,8+4*i,bits);
    }
}
static SRNOS_StorageStatus decode(const uint8_t *data,size_t len,SRNOS_SensorId id,SRNOS_IMUCalibration *out) {
    if(len!=48 && len!=SRNOS_IMU_CALIBRATION_BYTES) return SRNOS_STORAGE_CORRUPT;
    const SRNOS_Memory_ByteSpan bytes=SRNOS_Memory_Bytes(data,len);
    uint16_t sensor,reserved; SRNOS_Endian_LoadLE16(bytes,0,&sensor);
    SRNOS_Endian_LoadLE16(bytes,2,&out->model_version); SRNOS_Endian_LoadLE16(bytes,4,&out->axes_version);
    SRNOS_Endian_LoadLE16(bytes,6,&reserved); out->sensor=(SRNOS_SensorId)sensor;
    if(reserved || out->sensor!=id) return SRNOS_STORAGE_INCOMPATIBLE;
    if((out->model_version==2 && len!=60) || (out->model_version==1 && len!=48 && len!=60)) return SRNOS_STORAGE_CORRUPT;
    for(unsigned i=0;i<3;i++)out->gyro_scale[i]=1;
    for(unsigned i=0;i<(len-8)/4;i++) {
        uint32_t bits; float value; SRNOS_Endian_LoadLE32(bytes,8+4*i,&bits); memcpy(&value,&bits,4);
        if(i<3)out->gyro_bias_rad_s[i]=value;
        else if(i<6)out->accel_bias_m_s2[i-3]=value;
        else if(i<9)out->accel_scale[i-6]=value;
        else if(i==9)out->temperature_c=value;
        else if(out->model_version>=2)out->gyro_scale[i-10]=value;
    }
    return SRNOS_IMUCalibrationValidate(out)==SRNOS_STORAGE_OK?SRNOS_STORAGE_OK:SRNOS_STORAGE_CORRUPT;
}
void SRNOS_IMUCalibrationPrepare(bool enabled) {
    memset(&status,0,sizeof(status)); memset(active,0,sizeof(active));
    startup_started=loading=saving=false; startup_deadline=0; loaded_mask=0;
    status.startup_resolved=!enabled;
    for(unsigned i=0;i<2;i++) {
        status.load_status[i]=enabled?SRNOS_STORAGE_NOT_FOUND:SRNOS_STORAGE_DISABLED;
        status.save_status[i]=SRNOS_STORAGE_NOT_FOUND;
    }
}
uint32_t SRNOS_IMUCalibrationStartupBegin(uint32_t now) {
    uint32_t key=enter();
    if(!startup_started) {startup_started=true;startup_deadline=now+SRNOS_STORAGE_READ_TIMEOUT_MS;}
    uint32_t deadline=startup_deadline; leave(key); return deadline;
}
static void expire_startup(uint32_t now) {
    if(!status.startup_resolved && (int32_t)(now-startup_deadline)>=0) {
        for(unsigned i=0;i<2;i++) if(!(loaded_mask&(1u<<i))) status.load_status[i]=SRNOS_STORAGE_TIMEOUT;
        status.startup_resolved=true;
    }
}
bool SRNOS_IMUCalibrationStartupPoll(uint32_t now) {
    (void)SRNOS_IMUCalibrationStartupBegin(now);
    uint32_t key=enter();
    expire_startup(now);
    bool done=status.startup_resolved; leave(key); return done;
}
void SRNOS_IMUCalibrationLoadStartup(uint32_t deadline) {
    uint32_t key=enter();
    if(status.startup_resolved || loading) {leave(key);return;}
    loading=true; leave(key);
    for(unsigned i=0;i<2;i++) {
        uint8_t bytes[SRNOS_IMU_CALIBRATION_BYTES]; size_t len=0;uint32_t revision=0;
        SRNOS_IMUCalibration value={0}; uint32_t left=remaining(deadline);
        SRNOS_StorageStatus result=left?SRNOS_StorageReadRecord(record((SRNOS_SensorId)i),
            SRNOS_IMU_CALIBRATION_OWNER,1,bytes,sizeof(bytes),&len,&revision,left):SRNOS_STORAGE_TIMEOUT;
        if(result==SRNOS_STORAGE_OK)result=decode(bytes,len,(SRNOS_SensorId)i,&value);
        key=enter();
        /* Recheck the clock in the publication guard: a preemption after I/O
         * must not activate a calibration beyond the common startup deadline. */
        expire_startup(SRNOS_TimeMs());
        if(!status.startup_resolved) {
            status.load_status[i]=result; loaded_mask|=(uint8_t)(1u<<i);
            if(result==SRNOS_STORAGE_OK) {
                active[i]=value;status.active_mask|=(uint8_t)(1u<<i);
                status.active_revision[i]=status.saved_revision[i]=revision;
            }
        }
        bool done=status.startup_resolved;leave(key);if(done)break;
    }
    key=enter();loading=false;status.startup_resolved=true;leave(key);
}
uint8_t SRNOS_IMUCalibrationCopyActive(SRNOS_IMUCalibration values[2]) {
    uint32_t key=enter();uint8_t mask=status.active_mask;
    if(values)memcpy(values,active,sizeof(active));leave(key);return mask;
}
bool SRNOS_IMUCalibrationGet(SRNOS_SensorId id,SRNOS_IMUCalibration *out) {
    if(!out || !sensor_valid(id) || !SRNOS_KernelTaskContext())return false;
    uint32_t key=enter();bool valid=(status.active_mask&(1u<<(unsigned)id))!=0;
    if(valid)*out=active[id];else memset(out,0,sizeof(*out));leave(key);return valid;
}
bool SRNOS_IMUCalibrationGetStatus(SRNOS_IMUCalibrationStatus *out) {
    if(!out || !SRNOS_KernelTaskContext())return false;
    uint32_t key=enter();*out=status;leave(key);return true;
}
SRNOS_StorageStatus SRNOS_IMUCalibrationBeginSave(void) {
    if(!SRNOS_KernelTaskContext())return SRNOS_STORAGE_CONTEXT_ERROR;
    uint32_t key=enter();
    if(!status.startup_resolved || saving){leave(key);return SRNOS_STORAGE_BUSY;}
    saving=true;leave(key);return SRNOS_STORAGE_OK;
}
void SRNOS_IMUCalibrationEndSave(void) {
    uint32_t key=enter();saving=false;leave(key);
}
SRNOS_StorageStatus SRNOS_IMUCalibrationReadStored(SRNOS_SensorId id,SRNOS_IMUCalibration *out,uint32_t timeout) {
    if(!out || !sensor_valid(id))return SRNOS_STORAGE_INVALID_ARGUMENT;
    uint8_t bytes[SRNOS_IMU_CALIBRATION_BYTES];size_t len=0;uint32_t revision=0;
    SRNOS_StorageStatus result=SRNOS_StorageReadRecord(record(id),SRNOS_IMU_CALIBRATION_OWNER,1,
        bytes,sizeof(bytes),&len,&revision,timeout);
    return result==SRNOS_STORAGE_OK?decode(bytes,len,id,out):result;
}
SRNOS_StorageStatus SRNOS_IMUCalibrationWriteLocked(const SRNOS_IMUCalibration *value,uint32_t timeout) {
    uint8_t bytes[SRNOS_IMU_CALIBRATION_BYTES];encode(value,bytes);uint32_t revision=0;
    SRNOS_StorageStatus result=SRNOS_StorageWriteRecord(record(value->sensor),SRNOS_IMU_CALIBRATION_OWNER,1,bytes,value->model_version>=2?sizeof(bytes):48u,&revision,timeout);
    uint32_t key=enter();status.save_status[value->sensor]=result;
    if(result==SRNOS_STORAGE_OK) {
        status.saved_revision[value->sensor]=revision;
        status.pending_restart_mask|=(uint8_t)(1u<<(unsigned)value->sensor);
    }
    leave(key);return result;
}
SRNOS_StorageStatus SRNOS_IMUCalibrationSave(const SRNOS_IMUCalibration *input,uint32_t timeout) {
    if(!SRNOS_KernelTaskContext())return SRNOS_STORAGE_CONTEXT_ERROR;
    if(!input)return SRNOS_STORAGE_INVALID_ARGUMENT;
    SRNOS_IMUCalibration value=*input;
    SRNOS_StorageStatus result=SRNOS_IMUCalibrationValidate(&value);
    if(result!=SRNOS_STORAGE_OK)return result;
    result=SRNOS_IMUCalibrationBeginSave();if(result!=SRNOS_STORAGE_OK)return result;
    result=SRNOS_IMUCalibrationWriteLocked(&value,timeout);
    SRNOS_IMUCalibrationEndSave();return result;
}
