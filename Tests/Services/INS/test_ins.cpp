#include "ins_internal.h"
extern "C" {
#include "Devices/sensor_frames.h"
}
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
static SRNOS_IMUSample samples[2];
static SRNOS_MagneticSample mag;
static SRNOS_PressureSample pressure;
static SRNOS_HeaterStatus heat;
static SRNOS_INSConfig cfg={true,{1,0,0,0}};
static void fill(uint64_t us) {
    for(unsigned n=0;n<2;n++) {
        samples[n]={};samples[n].header={uint32_t(us/1000+1),19,us-50,us,SRNOS_SENSOR_READY};
        samples[n].accel_m_s2[2]=n?9.80665f:-9.80665f;samples[n].temperature_c=n?60:56;
    }
}
static void advance(SRNOS_INSCore& c,uint64_t first,uint64_t last,unsigned mask=3) {
    for(uint64_t t=first;t<=last;t+=1000) {
        fill(t);
        for(unsigned n=0;n<2;n++) if(!(mask&(1u<<n))) samples[n].header.state=SRNOS_SENSOR_OFFLINE;
        assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,t));
    }
}
int main() {
    // Proper rotations, handedness and the fixed PCB mounting.
    for(unsigned id=0;id<3;id++) {
        float e[3][3]={{1,0,0},{0,1,0},{0,0,1}},v[3][3];
        for(unsigned n=0;n<3;n++) SRNOS_BoardSensorToBoard((SRNOS_SensorId)id,e[n],v[n]);
        for(unsigned n=0;n<3;n++) {
            float cross=v[0][(n+1)%3]*v[1][(n+2)%3]-v[0][(n+2)%3]*v[1][(n+1)%3];assert(cross==v[2][n]);
        }
    }
    SRNOS_INSCore c;SRNOS_INSCoreInit(&c,&cfg,1000);advance(c,1000,2600000);
    assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);assert(c.snapshot.status.phase==SRNOS_INS_DEGRADED);
    assert(c.snapshot.attitude.flags&SRNOS_INS_UNCALIBRATED);
    assert(c.snapshot.attitude.source_mask==3&&c.snapshot.attitude.weights[0]==.5f);
    assert(fabsf(c.snapshot.attitude.quaternion_wxyz[0]-1)<1e-5f);
    auto seq=c.snapshot.attitude.sequence;advance(c,2601000,2620000,2);
    assert(c.snapshot.attitude.sequence==seq+20&&c.snapshot.attitude.source_mask==2);
    assert(c.snapshot.status.phase==SRNOS_INS_DEGRADED);
    // Rejoin waits then ramps, and cannot reset the orientation epoch.
    auto epoch=c.snapshot.status.epoch;advance(c,2621000,2670000);assert(c.snapshot.attitude.source_mask==2);
    advance(c,2671000,2830000);assert(c.snapshot.attitude.source_mask==3&&c.snapshot.status.epoch==epoch);
    // Repeated source frames propagate gyro but never re-use accelerometer correction.
    fill(2831000);assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,2831000));
    assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,2832000));assert(c.snapshot.attitude.flags&SRNOS_INS_PREDICTED);
    assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,2837000));assert(!(c.snapshot.attitude.flags&SRNOS_INS_VALID));
    advance(c,2838000,5440000,1);assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);assert(c.snapshot.status.epoch>epoch);
    // Calibration is applied in sensor axes BEFORE board/application rotation.
    auto corrected_cfg=cfg;corrected_cfg.board_to_robot_wxyz[0]=corrected_cfg.board_to_robot_wxyz[1]=sqrtf(.5f);
    SRNOS_INSCore corrected;SRNOS_INSCoreInit(&corrected,&corrected_cfg,1000);
    SRNOS_IMUCalibration calibration[2]={};
    for(unsigned n=0;n<2;n++)for(unsigned i=0;i<3;i++) {
        calibration[n].gyro_bias_rad_s[i]=.02f*(float)(i+1);
        calibration[n].accel_bias_m_s2[i]=.1f*(float)(i+1);
        calibration[n].accel_scale[i]=.9f+.1f*(float)i;
    }
    SRNOS_INSCoreSetCalibration(&corrected,calibration,3);
    for(uint64_t us=1000;us<=2700000;us+=1000) {
        fill(us);
        for(unsigned n=0;n<2;n++)for(unsigned i=0;i<3;i++) {
            samples[n].gyro_rad_s[i]=calibration[n].gyro_bias_rad_s[i];
            samples[n].accel_m_s2[i]=samples[n].accel_m_s2[i]/calibration[n].accel_scale[i]+calibration[n].accel_bias_m_s2[i];
        }
        auto raw=samples[0];
        assert(!SRNOS_INSCoreStep(&corrected,samples,&mag,&pressure,&heat,us));
        assert(!memcmp(&raw,&samples[0],sizeof(raw)));
    }
    assert(corrected.snapshot.status.calibrated&&corrected.snapshot.status.phase==SRNOS_INS_RUNNING);
    assert(!(corrected.snapshot.attitude.flags&SRNOS_INS_UNCALIBRATED));
    assert(fabsf(corrected.snapshot.imu[0].accel_m_s2[1]+9.80665f)<1e-4f);
    assert(fabsf(corrected.snapshot.imu[1].gyro_rad_s[0])<1e-6f);
    // v2 gyro gain applies in native sensor axes before board rotation.
    calibration[0].model_version=2;calibration[1].model_version=2;
    for(unsigned n=0;n<2;n++)for(unsigned j=0;j<3;j++)calibration[n].gyro_scale[j]=1.1f;
    SRNOS_INSCoreSetCalibration(&corrected,calibration,3);fill(2701000);
    for(unsigned n=0;n<2;n++)for(unsigned j=0;j<3;j++)samples[n].gyro_rad_s[j]=calibration[n].gyro_bias_rad_s[j]+.1f;
    SRNOS_INSCoreStep(&corrected,samples,&mag,&pressure,&heat,2701000);
    assert(fabsf(fabsf(corrected.snapshot.imu[0].gyro_rad_s[0])-.11f)<1e-5f);
    // One uncalibrated contributor makes the result explicitly degraded.
    corrected.calibration_mask=1;advance(corrected,2702000,2703000);
    assert(corrected.snapshot.status.phase==SRNOS_INS_DEGRADED&&!corrected.snapshot.status.calibrated);
    // Pending storage does not seed fusion or treat absent calibration as sensor failure.
    SRNOS_INSCoreInit(&corrected,&cfg,1000);corrected.calibration_pending=true;
    memset(samples,0,sizeof(samples));heat.duty=.25f;
    assert(!SRNOS_INSCoreStep(&corrected,samples,&mag,&pressure,&heat,2000000));
    assert(corrected.snapshot.status.calibration_load_pending&&corrected.snapshot.status.heater.duty==.25f);
    assert(!(corrected.snapshot.attitude.flags&SRNOS_INS_VALID));heat={};
    // No sensor at boot is fatal after a bounded 3s; environment cannot satisfy it.
    SRNOS_INSCoreInit(&c,&cfg,1000);memset(samples,0,sizeof(samples));
    assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,3000000));
    assert(SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,3001000));
    // Warmup forces a degraded start after 60s, still requiring observation.
    heat.enabled=true;SRNOS_INSCoreInit(&c,&cfg,1000);advance(c,1000,59999000);
    assert(c.snapshot.status.phase==SRNOS_INS_WARMUP);advance(c,60000000,62600000);
    assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);assert(c.snapshot.attitude.flags&SRNOS_INS_WARMUP_TIMEOUT);
    // All six stationary directions and a non-identity application installation.
    heat={};
    for(unsigned axis=0;axis<3;axis++) for(int sign=-1;sign<=1;sign+=2) {
        SRNOS_INSCoreInit(&c,&cfg,1000);
        for(uint64_t t=1000;t<=2600000;t+=1000) {
            fill(t);for(unsigned n=0;n<2;n++) for(unsigned i=0;i<3;i++) samples[n].accel_m_s2[i]=0;
            float b[3]={0,0,0};b[axis]=sign*9.80665f;
            samples[0].accel_m_s2[0]=b[0];samples[0].accel_m_s2[1]=-b[1];samples[0].accel_m_s2[2]=-b[2];
            samples[1].accel_m_s2[0]=-b[1];samples[1].accel_m_s2[1]=b[0];samples[1].accel_m_s2[2]=b[2];
            assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,t));
        }
        assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);
        for(unsigned i=0;i<3;i++) assert(fabsf(c.filter.rMat[2][i]-(i==axis?(float)sign:0))<1e-4f);
    }
    auto mounted=cfg;mounted.board_to_robot_wxyz[0]=mounted.board_to_robot_wxyz[1]=sqrtf(.5f);
    SRNOS_INSCoreInit(&c,&mounted,1000);advance(c,1000,2600000);
    assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);assert(fabsf(c.snapshot.imu[1].accel_m_s2[1]+9.80665f)<1e-4f);
    // Variable actual dt and a full yaw turn: robot->world Hamilton direction,
    // unit norm and quaternion sign continuity through the Euler +/-pi wrap.
    SRNOS_INSCoreInit(&c,&cfg,1000);advance(c,1000,2700000);
    uint64_t t=2700000;float previous[4]={1,0,0,0};
    for(unsigned n=0;n<6000;n++) {
        t+=(n&1)?1500:500;fill(t);samples[0].gyro_rad_s[2]=-1;samples[1].gyro_rad_s[2]=1;
        assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,t));
        float norm=0,dot=0;for(unsigned i=0;i<4;i++){float q=c.snapshot.attitude.quaternion_wxyz[i];norm+=q*q;dot+=q*previous[i];previous[i]=q;}
        assert(fabsf(norm-1)<1e-5f&&dot>0.999f);
    }
    assert(fabsf(c.snapshot.attitude.euler_rad[2]-(6-2*3.14159265359f))<.001f);
    // Asynchronous new samples outside 1ms alignment choose the newest input.
    fill(t+1000);samples[0].header.acquisition_end_us=t-500;
    assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,t+1000));
    assert(c.snapshot.attitude.source_mask==2&&(c.snapshot.attitude.flags&SRNOS_INS_TIME_SKEW));
    // Acceleration outside the accepted gravity band disables correction.
    fill(t+2000);samples[0].accel_m_s2[0]=samples[1].accel_m_s2[1]=30;
    assert(!SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,t+2000));assert(c.filter.Kp==0);
    // A moving platform cannot silently pass the initial low-dynamic window.
    SRNOS_INSCoreInit(&c,&cfg,1000);
    for(uint64_t moving=1000;moving<=4000000;moving+=1000){fill(moving);samples[0].gyro_rad_s[0]=samples[1].gyro_rad_s[1]=.2f;SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,moving);}
    assert(c.snapshot.status.phase==SRNOS_INS_OBSERVING&&!(c.snapshot.attitude.flags&SRNOS_INS_VALID));
    // RMS is sqrt(sum(axis variance)/3), distinct from residual-vector norm.
    SRNOS_INSCoreInit(&c,&cfg,1000);
    for(uint64_t n=1;n<=2700;n++) {
        fill(n*1000);float v=(n&1)?.007f:-.007f;
        samples[0].gyro_rad_s[0]=v;samples[0].gyro_rad_s[1]=samples[0].gyro_rad_s[2]=-v;
        samples[1].gyro_rad_s[0]=-v;samples[1].gyro_rad_s[1]=samples[1].gyro_rad_s[2]=v;
        SRNOS_INSCoreStep(&c,samples,&mag,&pressure,&heat,n*1000);
    }
    assert(c.snapshot.attitude.flags&SRNOS_INS_VALID);
    // Heater preference, full-power warmup, temperature protection and hysteresis.
    SRNOS_HeaterInit(true,60,true);fill(1000);samples[1].temperature_c=40;
    auto h=SRNOS_HeaterStep(samples,1000);assert(h.source==1&&h.duty==1);
    fill(2000);samples[1].temperature_c=50;h=SRNOS_HeaterStep(samples,2000);assert(h.duty>.999f);
    fill(3000);samples[0].temperature_c=80;h=SRNOS_HeaterStep(samples,3000);assert(h.overtemperature&&h.duty==0&&h.hot_sensor_mask==1);
    for(uint64_t t=4000;t<=6000000;t+=1000){fill(t);samples[0].header.state=SRNOS_SENSOR_OFFLINE;h=SRNOS_HeaterStep(samples,t);assert(h.overtemperature);}
    for(uint64_t t=6001000;t<=11103000;t+=1000){fill(t);h=SRNOS_HeaterStep(samples,t);}
    assert(!h.overtemperature&&h.protection_count==1&&h.protection_recoveries==1);
    fill(11104000);samples[1].header.state=SRNOS_SENSOR_OFFLINE;h=SRNOS_HeaterStep(samples,11104000);assert(h.source==0);
    for(uint64_t t=11105000;t<11205000;t+=1000){fill(t);h=SRNOS_HeaterStep(samples,t);assert(h.source==0);}
    fill(11205000);h=SRNOS_HeaterStep(samples,11205000);assert(h.source==1);
    fill(11206000);samples[0].gyro_rad_s[0]=NAN;samples[1].accel_m_s2[0]=NAN;
    h=SRNOS_HeaterStep(samples,11206000);assert(h.feedback_missing&&h.duty==0);
    // Stable criterion, missing feedback, hardware faults and disabled heat.
    SRNOS_HeaterInit(true,60,true);
    for(uint64_t t=1000;t<=5010000;t+=1000){fill(t);h=SRNOS_HeaterStep(samples,t);}assert(h.thermal_ready);
    memset(samples,0,sizeof(samples));h=SRNOS_HeaterStep(samples,5011000);assert(h.feedback_missing&&h.duty==0);
    SRNOS_HeaterInit(true,60,false);fill(1000);h=SRNOS_HeaterStep(samples,1000);assert(h.hardware_fault&&h.duty==0);
    SRNOS_HeaterInit(false,60,true);h=SRNOS_HeaterStep(samples,2000);assert(!h.enabled&&h.duty==0);
    puts("INS: frames, startup, warmup, fusion, freshness, failover/recovery, six poses and heater protection passed");
}
