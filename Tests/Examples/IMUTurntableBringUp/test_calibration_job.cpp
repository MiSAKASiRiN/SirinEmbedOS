#include "../../../Examples/IMUTurntableBringUp/calibration_job.hpp"
#include <cassert>
#include <cstdio>
using namespace SRNOS::Examples::Turntable;
static uint64_t clock_us=0;
static SRNOS_INSStatus ins{};
extern "C" uint64_t SRNOS_TimeUs(void){return clock_us;}
extern "C" bool SRNOS_INSGetStatus(SRNOS_INSStatus *out){*out=ins;return true;}
extern "C" bool SRNOS_SensorGetIMU(SRNOS_SensorId,SRNOS_IMUSample *out){*out={};out->header.state=SRNOS_SENSOR_READY;out->header.valid_fields=3;out->header.acquisition_end_us=clock_us;out->accel_m_s2[2]=9.80665f;out->temperature_c=60;return true;}
int main(){
 Feedback f[3]={{350,true,25},{4764,true,25},{4045,true,25}};Controller c;CalibrationJob j;JobStatus s{};
 for(int i=0;i<100;++i){clock_us+=1000;j.tick(c,f,.001f,clock_us/1000);}assert(!j.engaged);for(auto&a:c.axis)assert(a.command==0);
 j.start.store(17);for(int i=0;i<6000;++i){clock_us+=1000;j.heartbeat.store(clock_us/1000);j.tick(c,f,.001f,clock_us/1000);}
 assert(j.published.get(s)&&s.phase==uint32_t(JobPhase::Heating)&&!j.engaged);
 ins.heater.enabled=true;ins.heater.target_c=ins.heater.feedback_c=60;ins.healthy_imu_mask=3;
 for(int i=0;i<5010;++i){clock_us+=1000;j.heartbeat.store(clock_us/1000);j.tick(c,f,.001f,clock_us/1000);}assert(j.engaged);
 c.phase=Phase::Hold;for(unsigned i=0;i<3;i++){c.axis[i].home=i==2?38:0;c.axis[i].enabled=true;}
 // Walk the recipe at ideal endpoint feedback; this tests sequencing, not mechanics.
 for(int k=0;k<120000;++k){clock_us+=1000;j.heartbeat.store(clock_us/1000);for(auto&a:c.axis){a.position=a.target;a.speed=0;}j.tick(c,f,.001f,clock_us/1000);j.published.get(s);assert(s.phase!=uint32_t(JobPhase::Aborted));if(s.phase==uint32_t(JobPhase::Result))break;}
 assert(s.phase==uint32_t(JobPhase::Result)&&s.records==24);
 for(unsigned i=0;i<24;i++){CalibrationRecord r{};assert(j.records[i].get(r)&&r.index==i&&r.kind==(i<12?1u:2u));}
 j.saving.store(2);j.saved.store(1);j.tick(c,f,.001f,clock_us/1000);assert(j.published.get(s)&&s.phase==uint32_t(JobPhase::Aborted)&&s.saved==1);
 // Restart while bearing weight must preserve support through heating.
 c.axis[2].command=6000;j.start.store(18);j.tick(c,f,.001f,clock_us/1000);assert(j.engaged&&c.axis[2].command==6000);
 clock_us+=6000000;c.axis[2].position=42;j.tick(c,f,.001f,clock_us/1000);assert(j.published.get(s)&&s.phase==uint32_t(JobPhase::Aborted));assert(c.axis[2].target==42&&c.axis[2].command==6000);
 puts("PASS: idle zero, heat gate, complete recipe, partial save, disconnect support hold");
}
