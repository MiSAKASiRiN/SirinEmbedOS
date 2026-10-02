#include "imu_calibration_internal.h"
#include "Services/Storage/storage_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
uint32_t test_basepri;
static uint32_t tick,base,write_count,read_count,save_fail_mask;
static uint64_t micros;
static bool locked,context=true,gate_checked;
static uint8_t disk[2][48];static bool exists[2];
static unsigned mode;
static SRNOS_StorageStatus read_error;
uint32_t test_get_mask(void){return locked;}
void test_disable_irq(void){assert(!locked);locked=true;}
void test_set_mask(uint32_t mask){assert(locked&&!mask);locked=false;}
bool SRNOS_KernelTaskContext(void){return context;}
uint32_t SRNOS_TimeMs(void){return tick;}
uint64_t SRNOS_TimeUs(void){return micros;}
SRNOS_StorageStatus SRNOS_StorageReadRecord(SRNOS_StorageRecord record,uint32_t owner,uint16_t schema,
 void *out,size_t cap,size_t *len,uint32_t *revision,uint32_t timeout) {
 assert(!locked&&owner&&schema==1&&timeout&&cap>=48);unsigned i=(unsigned)record-2;assert(i<2);read_count++;
 *len=0;*revision=0;if(read_error!=SRNOS_STORAGE_OK)return read_error;
 if(!exists[i])return SRNOS_STORAGE_NOT_FOUND;
 memcpy(out,disk[i],48);*len=48;*revision=7;return SRNOS_STORAGE_OK;
}
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord record,uint32_t owner,uint16_t schema,
 const void *data,size_t n,uint32_t *revision,uint32_t timeout) {
 assert(!locked&&owner&&schema==1&&timeout&&n==48);unsigned i=(unsigned)record-2;assert(i<2);write_count++;
 if(save_fail_mask&(1u<<i))return SRNOS_STORAGE_VERIFY_ERROR;
 memcpy(disk[i],data,48);exists[i]=true;*revision=8;return SRNOS_STORAGE_OK;
}
bool SRNOS_SensorGetStatus(SRNOS_SensorId id,SRNOS_SensorStatus *s) {
 assert(!locked);*s=(SRNOS_SensorStatus){.state=SRNOS_SENSOR_READY};
 if(mode==7&&tick-base>=50&&id==SRNOS_SENSOR_LSM6DS3)s->errors=1;
 if(mode==8&&id==SRNOS_SENSOR_LSM6DS3)s->state=SRNOS_SENSOR_OFFLINE;
 return true;
}
bool SRNOS_SensorGetHeaterStatus(SRNOS_HeaterStatus *s) {
 *s=(SRNOS_HeaterStatus){.enabled=true,.thermal_ready=true};
 if(mode==9&&tick-base>=50)s->hardware_fault=true;
 return true;
}
bool SRNOS_SensorGetIMU(SRNOS_SensorId id,SRNOS_IMUSample *s) {
 assert(!locked);float noise=(tick/10%2)? .0002f:-.0002f;
 *s=(SRNOS_IMUSample){.header={.state=SRNOS_SENSOR_READY,.valid_fields=19,.sequence=(uint32_t)(tick-base)+1,
    .acquisition_start_us=micros-100,.acquisition_end_us=micros},
    .accel_m_s2={5.66f,5.66f,5.66f},.gyro_rad_s={.012f+noise,-.003f,.02f},.temperature_c=60};
 if(tick-base>=50) {
  if(mode==1)s->gyro_rad_s[0]=.2f;
  if(mode==2)s->accel_m_s2[0]+=.5f;
  if(mode==3)s->temperature_c+=1.1f;
  if(mode==4)s->header.acquisition_end_us=micros-30000;
  if(mode==5)s->gyro_rad_s[0]=NAN;
  if(mode==6)s->header.sequence=41; /* Last observed sequence. */
 }
 if(mode==10)s->gyro_rad_s[0]=.012f+((tick/10%2)? .025f:-.025f);
 if(mode==11)s->header.sequence=UINT32_MAX-100+(tick-base); /* Legal sequence wrap. */
 if(mode==12)s->header.valid_fields&=~SRNOS_SENSOR_GYRO;
 if(mode==13)s->accel_m_s2[0]=20;
 if(mode==14&&id==SRNOS_SENSOR_LSM6DS3)s->temperature_c=INFINITY;
 if(mode==17) { /* Raw noise envelope from stationary board, plus margin. */
  unsigned n=(tick-base)/10;float sign=(n%2)?1.f:-1.f;
  s->gyro_rad_s[0]=.012f+sign*((n%100)<2?.045f:.012f);
  s->gyro_rad_s[1]=-.03f+sign*.010f;
  s->accel_m_s2[0]+=sign*((n%100)<2?.175f:.04f);
  s->temperature_c+=sign*.375f;
 }
 if(mode==18)s->gyro_rad_s[0]=((tick/10%2)? .065f:-.065f); /* Span, under abs limit. */
 return true;
}
int osDelay(uint32_t n) {
 assert(!locked&&n==10);
 if(!gate_checked) {
  SRNOS_IMUCalibration v={.sensor=SRNOS_SENSOR_BMI088,.model_version=1,.axes_version=1,.accel_scale={1,1,1}};
  assert(SRNOS_IMUCalibrationSave(&v,1)==SRNOS_STORAGE_BUSY);
  SRNOS_IMUStaticStatus nested;assert(SRNOS_IMUCalibrationRunStatic(1,&nested)==SRNOS_IMU_STATIC_BUSY);
  gate_checked=true;
 }
 if(mode==15)n=50; /* Scheduler starvation must not quietly average gaps. */
 if(mode==16)n=21000;
 tick+=n;micros+=(uint64_t)n*1000;return 0;
}
static void reset(unsigned scenario) {
 mode=scenario;tick=base=100;micros=1000000;write_count=read_count=save_fail_mask=0;gate_checked=false;
 read_error=SRNOS_STORAGE_OK;memset(exists,0,sizeof(exists));SRNOS_IMUCalibrationPrepare(false);
}
int main(void) {
 SRNOS_IMUStaticStatus s,last;SRNOS_IMUCalibration v;
 reset(0);assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_OK);
 assert(s.saved_mask==3&&s.samples[0]==501&&s.samples[1]==501&&s.elapsed_ms==5000&&write_count==2);
 assert(fabsf(s.gyro_bias_rad_s[0][0]-.012f)<.00001f&&s.phase==SRNOS_IMU_STATIC_DONE);
 assert(!SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&v));
 assert(SRNOS_IMUCalibrationGetStaticStatus(&last)&&last.job_id==s.job_id);
 SRNOS_IMUCalibrationPrepare(true);SRNOS_IMUCalibrationLoadStartup(SRNOS_IMUCalibrationStartupBegin(tick));
 assert(SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&v)&&fabsf(v.gyro_bias_rad_s[0]-.012f)<.00001f);
 // A valid saved accelerator calibration survives a second calibration BEFORE reboot.
 v.accel_bias_m_s2[1]=.15f;v.accel_scale[2]=1.08f;
 assert(SRNOS_IMUCalibrationSave(&v,100)==SRNOS_STORAGE_OK);
 assert(SRNOS_IMUCalibrationRunStatic(1,&s)==SRNOS_IMU_STATIC_OK);
 SRNOS_IMUCalibrationPrepare(true);SRNOS_IMUCalibrationLoadStartup(SRNOS_IMUCalibrationStartupBegin(tick));
 assert(SRNOS_IMUCalibrationGet(SRNOS_SENSOR_BMI088,&v)&&v.accel_bias_m_s2[1]==.15f&&v.accel_scale[2]==1.08f);
 const SRNOS_IMUStaticResult expected[]={SRNOS_IMU_STATIC_OK,SRNOS_IMU_STATIC_MOVING,SRNOS_IMU_STATIC_MOVING,
  SRNOS_IMU_STATIC_TEMPERATURE,SRNOS_IMU_STATIC_STALE,SRNOS_IMU_STATIC_SENSOR_ERROR,SRNOS_IMU_STATIC_STALE,
  SRNOS_IMU_STATIC_SENSOR_ERROR,SRNOS_IMU_STATIC_NOT_READY,SRNOS_IMU_STATIC_TEMPERATURE,SRNOS_IMU_STATIC_MOVING};
 for(unsigned i=1;i<=10;i++){reset(i);assert(SRNOS_IMUCalibrationRunStatic(3,&s)==expected[i]);assert(!write_count&&!s.saved_mask);}
 reset(11);tick=base=UINT32_MAX-2000;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_OK);
 for(unsigned i=12;i<=16;i++){reset(i);assert(SRNOS_IMUCalibrationRunStatic(3,&s)!=SRNOS_IMU_STATIC_OK);assert(!write_count);}
 reset(17);assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_OK);
 assert(s.samples[0]==501&&s.saved_mask==3&&write_count==2);
 assert(fabsf(s.gyro_bias_rad_s[0][0]-.012f)<.0001f);
 reset(18);assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_MOVING&&!write_count);
 reset(0);save_fail_mask=2;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_STORAGE_ERROR);
 assert(s.saved_mask==1&&s.failed_sensor==1&&s.storage_status[1]==SRNOS_STORAGE_VERIFY_ERROR);
 SRNOS_IMUCalibrationStatus cs;assert(SRNOS_IMUCalibrationGetStatus(&cs)&&cs.pending_restart_mask==1&&!cs.active_mask);
 reset(0);read_error=SRNOS_STORAGE_BUSY;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_STORAGE_ERROR&&!write_count);
 reset(0);assert(SRNOS_IMUCalibrationRunStatic(0,&s)==SRNOS_IMU_STATIC_INVALID);
 context=false;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_CONTEXT&&!read_count);context=true;
 locked=true;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_CONTEXT&&!read_count);locked=false;
 test_basepri=80;assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_CONTEXT&&!read_count);test_basepri=0;
 reset(0);SRNOS_IMUCalibrationPrepare(true);assert(SRNOS_IMUCalibrationRunStatic(3,&s)==SRNOS_IMU_STATIC_NOT_READY);
 puts("Static IMU: arbitrary pose, bias, preserved accel, next boot, motion/variance/temperature, stale/NaN/fault, starvation, wrap, lease and partial save passed");
}
