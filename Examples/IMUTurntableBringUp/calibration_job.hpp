#pragma once
#include "control.hpp"
#include "SirinEmbedOS/Services/INS.h"
#include <atomic>
#include <cstring>

namespace SRNOS::Examples::Turntable {
constexpr uint32_t RecipeSignature=0x54544332u;
enum class JobPhase:uint32_t {Idle,Heating,Homing,Moving,Static,Dynamic,Observe,Result,Saving,Done,Aborted,Failed};
struct CalibrationRecord {
    uint32_t kind,index; // 1 static SI means; 2 dynamic gyro integrals (rad).
    float duration[2],delta[3],accel[6],gyro[6],temperature[2],accel_rms[2],gyro_rms[2];
};
static_assert(sizeof(CalibrationRecord)==100);
struct JobStatus {
    uint32_t magic=RecipeSignature,version=1,job=0,phase=0,step=0,total=30,records=0,error=0,saved=0;
    float elapsed=0,temperature=0,target_temperature=60,position[3]{},target[3]{};
    uint32_t store[2]{},revision[2]{};
};
static_assert(sizeof(JobStatus)<=128);
// Atomics avoid cross-priority spin locks. Readers make one bounded attempt.
template<class T> class Published {
    std::atomic<uint32_t> version{0},words[(sizeof(T)+3)/4]{};
public:
    void put(const T&v) {uint32_t a[(sizeof(T)+3)/4]{};std::memcpy(a,&v,sizeof(v));version.fetch_add(1);for(unsigned i=0;i<sizeof(a)/4;i++)words[i].store(a[i]);version.fetch_add(1);}
    bool get(T&v) const {const auto before=version.load();if(before&1)return false;uint32_t a[(sizeof(T)+3)/4];for(unsigned i=0;i<sizeof(a)/4;i++)a[i]=words[i].load();if(version.load()!=before)return false;std::memcpy(&v,a,sizeof(v));return true;}
};
class CalibrationJob {
    JobStatus state{};
    float time=0,settled=0,heat_stable=0,started_position[3]{};
    uint32_t random=1;
    struct Acc {double a[3]{},a2[3]{},g[3]{},g2[3]{},integral[3]{},temp=0,duration=0;uint32_t count=0;uint64_t stamp=0;} accum[2];
    void phase(JobPhase p) {state.phase=uint32_t(p);time=settled=0;}
    bool near(const Controller &c) {
        for(const auto &a:c.axis)if(std::fabs(a.position-a.target)>12*DegreesPerCount||std::fabs(a.speed)>2)return false;
        return true;
    }
    void hold(Controller &c,JobPhase p,uint32_t why) {
        for(auto &a:c.axis)a.target=a.position;
        c.phase=Phase::Hold;state.error=why;phase(p);
    }
    void target(Controller &c,float a,float b,float d) {
        const float offsets[3]={a,b,d};for(unsigned i=0;i<3;i++)c.axis[i].target=c.axis[i].home+offsets[i];
        c.axis[2].target=std::clamp(c.axis[2].target,LowerTarget,UpperTarget);
    }
    void reset_accum(const Controller &c) {for(auto &a:accum)a=Acc{};for(unsigned i=0;i<3;i++)started_position[i]=c.axis[i].position;}
    bool sample() {
        for(unsigned n=0;n<2;n++) {
            SRNOS_IMUSample s{};if(!SRNOS_SensorGetIMU(static_cast<SRNOS_SensorId>(n),&s)||s.header.state!=SRNOS_SENSOR_READY||(s.header.valid_fields&3)!=3)return false;
            auto &a=accum[n];const uint64_t stamp=s.header.acquisition_end_us;
            if(SRNOS_TimeUs()<stamp || SRNOS_TimeUs()-stamp>5000)return false;
            if(stamp==a.stamp)continue;
            if(a.stamp&&(stamp<a.stamp||stamp-a.stamp>4000))return false;
            const double dt=a.stamp?(stamp-a.stamp)*1e-6:.001;a.stamp=stamp;a.duration+=dt;a.count++;a.temp+=s.temperature_c;
            for(unsigned j=0;j<3;j++){const double x=s.accel_m_s2[j],g=s.gyro_rad_s[j];if(!std::isfinite(x)||!std::isfinite(g))return false;a.a[j]+=x;a.a2[j]+=x*x;a.g[j]+=g;a.g2[j]+=g*g;a.integral[j]+=g*dt;}
        }
        return true;
    }
    bool record(const Controller &c,bool dynamic) {
        CalibrationRecord r{};r.kind=dynamic?2:1;r.index=state.records;
        for(unsigned i=0;i<3;i++)r.delta[i]=c.axis[i].position-started_position[i];
        for(unsigned n=0;n<2;n++) {
            const auto &a=accum[n];if(a.count<1000||a.duration<=0)return false;
            r.duration[n]=a.duration;r.temperature[n]=a.temp/a.count;
            double av=0,gv=0;
            for(unsigned j=0;j<3;j++) {const auto i=3*n+j;r.accel[i]=a.a[j]/a.count;r.gyro[i]=dynamic?a.integral[j]:a.g[j]/a.count;av+=std::max(0.0,a.a2[j]/a.count-std::pow(a.a[j]/a.count,2));gv+=std::max(0.0,a.g2[j]/a.count-std::pow(a.g[j]/a.count,2));}
            r.accel_rms[n]=std::sqrt(av/3);r.gyro_rms[n]=std::sqrt(gv/3);
            if(!dynamic&&(r.accel_rms[n]>.3f||r.gyro_rms[n]>.025f))return false;
        }
        if(state.records>=24)return false;
        records[state.records].put(r);state.records++;return true;
    }
    void next(Controller &c) {
        ++state.step;
        if(state.step<12){constexpr float tilt[3]={-30,50,150};target(c,0,float(state.step%4)*90,tilt[state.step/4]);phase(JobPhase::Moving);}
        else if(state.step==12){target(c,0,0,0);phase(JobPhase::Moving);}
        else if(state.step<=24) {
            const unsigned k=state.step-13,ax=(k%6)/2;
            float offset[3]{};if(k%2==0)offset[ax]=ax==2?(k<6?120:60):(k<6?180:-120);
            reset_accum(c);target(c,offset[0],offset[1],offset[2]);phase(JobPhase::Dynamic);
        } else if(state.step<=27) {
            float offset[3];for(auto &x:offset){random=random*1664525u+1013904223u;x=float((random>>16)%121)-60;}
            offset[2]=std::fabs(offset[2])+20;target(c,offset[0],offset[1],offset[2]);phase(JobPhase::Observe);
        } else if(state.step==28){target(c,0,0,0);phase(JobPhase::Moving);}
        else phase(JobPhase::Result);
    }
public:
    Published<JobStatus> published;
    Published<CalibrationRecord> records[24];
    std::atomic<uint32_t> start{0},abort{0},heartbeat{0},saving{0},saved{0},store[2]{},revisions[2]{};
    bool engaged=false;
    void tick(Controller &c,const Feedback (&f)[3],float dt,uint32_t now) {
        uint32_t request=start.exchange(0);
        if(request) {if(!engaged)c=Controller{};else for(auto &a:c.axis)a.target=a.position;state=JobStatus{};state.job=request;random=request;state.saved=0;saved.store(0);for(unsigned i=0;i<2;i++){store[i].store(0);revisions[i].store(0);}heat_stable=0;phase(JobPhase::Heating);}
        auto p=JobPhase(state.phase);
        const bool running=p>=JobPhase::Heating&&p<=JobPhase::Observe;
        if((running||p==JobPhase::Result)&&abort.exchange(0)){hold(c,JobPhase::Aborted,1);p=JobPhase::Aborted;}
        if(running&&uint32_t(now-heartbeat.load())>5000){hold(c,JobPhase::Aborted,2);p=JobPhase::Aborted;}
        if(engaged&&c.fault!=Fault::None){state.error=100+uint32_t(c.fault);phase(JobPhase::Failed);p=JobPhase::Failed;}
        state.elapsed+=running?dt:0;time+=dt;
        SRNOS_INSStatus ins{};SRNOS_INSGetStatus(&ins);state.temperature=ins.heater.feedback_c;state.target_temperature=ins.heater.target_c;
        if(p==JobPhase::Heating) {
            bool ready=ins.heater.enabled&&!ins.heater.overtemperature&&!ins.heater.hardware_fault&&ins.healthy_imu_mask==3&&std::fabs(ins.heater.feedback_c-ins.heater.target_c)<1;
            for(auto &v:f)ready&=v.online;
            heat_stable=ready?heat_stable+dt:0;
            if(time>360)hold(c,JobPhase::Aborted,3);
            else if(heat_stable>=5){engaged=true;phase(JobPhase::Homing);}
        } else if(p==JobPhase::Homing) {
            if(c.phase==Phase::Level||c.phase==Phase::Hold){c.phase=Phase::Hold;target(c,0,0,-30);state.step=0;phase(JobPhase::Moving);}
            else if(time>120)hold(c,JobPhase::Aborted,4);
        } else if(p==JobPhase::Moving||p==JobPhase::Dynamic) {
            if(p==JobPhase::Dynamic&&!sample()){hold(c,JobPhase::Aborted,5);}
            else {settled=near(c)?settled+dt:0;if(time>60)hold(c,JobPhase::Aborted,4);
            else if(settled>=1){if(state.step<12){reset_accum(c);phase(JobPhase::Static);}else if(p==JobPhase::Dynamic&&!record(c,true))hold(c,JobPhase::Aborted,6);else next(c);}}
        } else if(p==JobPhase::Static) {
            if(!near(c)||!sample())hold(c,JobPhase::Aborted,5);
            else if(time>=2){if(!record(c,false))hold(c,JobPhase::Aborted,6);else next(c);}
        } else if(p==JobPhase::Observe && time>=5)next(c);
        if(JobPhase(state.phase)==JobPhase::Result&&saving.load()!=0)phase(JobPhase::Saving);
        if(JobPhase(state.phase)==JobPhase::Saving&&saving.load()==2) {
            state.saved=saved.load();for(unsigned i=0;i<2;i++){state.store[i]=store[i].load();state.revision[i]=revisions[i].load();}
            saving.store(0);phase(state.saved==3?JobPhase::Done:JobPhase::Aborted);if(state.saved!=3)state.error=7;
        }
        for(unsigned i=0;i<3;i++){state.position[i]=c.axis[i].position;state.target[i]=c.axis[i].target;}
        published.put(state);
    }
};
}
