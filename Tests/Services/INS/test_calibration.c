#include "imu_calibration_internal.h"
#include "Services/Storage/storage_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t data[2][64];static size_t lengths[2];
static SRNOS_StorageStatus reads[2]={SRNOS_STORAGE_NOT_FOUND,SRNOS_STORAGE_NOT_FOUND};
static uint32_t now,read_delay;static bool task_context=true,locked,fail_write;
static uint32_t advance_after_read,advance_at_lock;
uint32_t test_get_mask(void){now+=advance_at_lock;advance_at_lock=0;return locked;}
void test_disable_irq(void){assert(!locked);locked=true;}
void test_set_mask(uint32_t mask){assert(!mask&&locked);locked=false;}
uint32_t SRNOS_TimeMs(void){return now;}
bool SRNOS_KernelTaskContext(void){return task_context;}
SRNOS_StorageStatus SRNOS_StorageReadRecord(SRNOS_StorageRecord r,uint32_t owner,uint16_t schema,
 void *out,size_t cap,size_t *len,uint32_t *revision,uint32_t timeout) {
 assert(!locked&&owner&&schema==1&&timeout);unsigned n=(unsigned)r-2;assert(n<2);
 now+=read_delay;advance_at_lock=advance_after_read;advance_after_read=0;*len=0;*revision=0;
 if(reads[n]!=SRNOS_STORAGE_OK)return reads[n];
 assert(cap>=lengths[n]);memcpy(out,data[n],lengths[n]);*len=lengths[n];*revision=5+n;
 return SRNOS_STORAGE_OK;
}
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord r,uint32_t owner,uint16_t schema,
 const void *src,size_t len,uint32_t *revision,uint32_t timeout) {
 (void)timeout;assert(!locked&&owner&&schema==1&&len<=64);unsigned n=(unsigned)r-2;assert(n<2);
 if(fail_write)return SRNOS_STORAGE_IO_ERROR;
 memcpy(data[n],src,len);lengths[n]=len;reads[n]=SRNOS_STORAGE_OK;*revision=5+n;return SRNOS_STORAGE_OK;
}
static SRNOS_IMUCalibration value(unsigned n) {
 SRNOS_IMUCalibration v={.sensor=(SRNOS_SensorId)n,.model_version=1,.axes_version=1,
  .gyro_bias_rad_s={.01f,-.02f,.03f},.accel_bias_m_s2={.1f,.2f,-.3f},.accel_scale={.9f,1,1.1f},.temperature_c=60};
 return v;
}
int main(void) {
 SRNOS_IMUCalibrationStatus s;SRNOS_IMUCalibration out,v=value(0);
 assert(SRNOS_IMUCalibrationValidate(&v)==SRNOS_STORAGE_OK);
 v.accel_scale[0]=0;assert(SRNOS_IMUCalibrationValidate(&v)!=SRNOS_STORAGE_OK);
 v=value(0);v.gyro_bias_rad_s[1]=NAN;assert(SRNOS_IMUCalibrationValidate(&v)!=SRNOS_STORAGE_OK);
 v=value(0);v.accel_bias_m_s2[0]=INFINITY;assert(SRNOS_IMUCalibrationValidate(&v)!=SRNOS_STORAGE_OK);
 v=value(0);v.temperature_c=126;assert(SRNOS_IMUCalibrationValidate(&v)!=SRNOS_STORAGE_OK);
 v=value(0);v.sensor=SRNOS_SENSOR_LIS2MDL;assert(SRNOS_IMUCalibrationValidate(&v)!=SRNOS_STORAGE_OK);
 // Save never activates even valid values: reboot/loading is required.
 SRNOS_IMUCalibrationPrepare(false);
 for(unsigned i=0;i<2;i++){v=value(i);assert(SRNOS_IMUCalibrationSave(&v,0)==SRNOS_STORAGE_OK);}
 assert(!SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&out));
 assert(SRNOS_IMUCalibrationGetStatus(&s)&&s.pending_restart_mask==3&&!s.active_mask);
 // Startup handles wraparound and a bad peer record independently.
 now=UINT32_MAX-20;read_delay=1;reads[1]=SRNOS_STORAGE_CORRUPT;
 SRNOS_IMUCalibrationPrepare(true);uint32_t deadline=SRNOS_IMUCalibrationStartupBegin(now);
 assert(!SRNOS_IMUCalibrationStartupPoll(now));SRNOS_IMUCalibrationLoadStartup(deadline);
 assert(SRNOS_IMUCalibrationStartupPoll(now));
 assert(SRNOS_IMUCalibrationGetStatus(&s)&&s.active_mask==1&&s.load_status[1]==SRNOS_STORAGE_CORRUPT);
 assert(SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&out)&&out.gyro_bias_rad_s[0]==.01f);
 v=value(0);v.gyro_bias_rad_s[0]=.4f;assert(SRNOS_IMUCalibrationSave(&v,0)==SRNOS_STORAGE_OK);
 assert(SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&out)&&out.gyro_bias_rad_s[0]==.01f);
 fail_write=true;v.gyro_bias_rad_s[0]=.5f;assert(SRNOS_IMUCalibrationSave(&v,0)==SRNOS_STORAGE_IO_ERROR);
 assert(SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&out)&&out.gyro_bias_rad_s[0]==.01f);fail_write=false;
 // A late successful read must never enter the active calibration after timeout.
 SRNOS_IMUCalibrationPrepare(true);now=100;read_delay=2001;deadline=SRNOS_IMUCalibrationStartupBegin(now);
 SRNOS_IMUCalibrationLoadStartup(deadline);
 assert(SRNOS_IMUCalibrationGetStatus(&s)&&s.startup_resolved&&!s.active_mask&&s.load_status[0]==SRNOS_STORAGE_TIMEOUT);
 // Tick/preemption immediately before the publication guard must also reject
 // a read that completed on time; this covers the check-to-publication window.
 SRNOS_IMUCalibrationPrepare(true);now=100;read_delay=0;advance_after_read=2000;
 SRNOS_IMUCalibrationLoadStartup(SRNOS_IMUCalibrationStartupBegin(now));
 assert(SRNOS_IMUCalibrationGetStatus(&s)&&s.startup_resolved&&!s.active_mask&&s.load_status[0]==SRNOS_STORAGE_TIMEOUT);
 // Physically valid CRC is insufficient: wrong sensor identity is rejected.
 SRNOS_IMUCalibrationPrepare(true);now=0;read_delay=0;data[0][0]=1;
 SRNOS_IMUCalibrationLoadStartup(SRNOS_IMUCalibrationStartupBegin(now));
 assert(SRNOS_IMUCalibrationGetStatus(&s)&&!s.active_mask&&s.load_status[0]==SRNOS_STORAGE_INCOMPATIBLE);
 task_context=false;assert(!SRNOS_IMUCalibrationGetStatus(&s));assert(SRNOS_IMUCalibrationSave(&v,0)==SRNOS_STORAGE_CONTEXT_ERROR);
 puts("IMU calibration: validation, serialized persistence, independent failure, startup deadline/wrap, next-boot activation passed");
}
