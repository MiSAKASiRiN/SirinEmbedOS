#include "ins_internal.h"
#include "SirinEmbedOS/Algorithms/Control/pid.hpp"
#include <cmath>
#include <cstring>
namespace {
SRNOS::Control::Pid pid;
SRNOS_HeaterStatus state;
uint64_t last_us,good_lsm_us,good_feedback_us,stable_us,cool_us;
bool started,needs_recovery;
bool fresh(const SRNOS_IMUSample& s,uint64_t now) {
    for(unsigned i=0;i<3;i++) if(!std::isfinite(s.accel_m_s2[i])||!std::isfinite(s.gyro_rad_s[i])) return false;
    return s.header.state==SRNOS_SENSOR_READY&&(s.header.valid_fields&19u)==19u&&
        s.header.acquisition_end_us&&now>=s.header.acquisition_end_us&&now-s.header.acquisition_end_us<=5000&&
        std::isfinite(s.temperature_c);
}
void off() {state.duty=0;state.thermal_ready=false;stable_us=0;pid.reset(state.feedback_c,0);}
}
extern "C" void SRNOS_HeaterInit(bool enabled,float target,bool hardware_ok) {
    state={};state.enabled=enabled;state.target_c=target;state.source=-1;
    state.hardware_fault=enabled&&!hardware_ok;state.faulted=state.hardware_fault;
    last_us=good_lsm_us=good_feedback_us=stable_us=cool_us=0;started=needs_recovery=false;
    SRNOS::Control::PidConfig cfg;
    cfg.form=SRNOS::Control::PidForm::Positional;cfg.gains={40,2,0};
    cfg.output_min=0;cfg.output_max=500;cfg.integral_min=0;cfg.integral_max=350;
    cfg.slew_up_per_s=cfg.slew_down_per_s=25;
    cfg.anti_windup=SRNOS::Control::AntiWindupMode::BackCalculation;cfg.back_calculation_gain=1;
    cfg.min_dt_s=.000001f;cfg.max_dt_s=.01f;
    if(pid.configure(cfg)!=SRNOS::Control::PidStatus::Ok) state.hardware_fault=state.faulted=true;
}
extern "C" SRNOS_HeaterStatus SRNOS_HeaterStep(const SRNOS_IMUSample samples[2],uint64_t now) {
    if(!state.enabled||state.hardware_fault) {off();return state;}
    const uint64_t elapsed=last_us&&now>last_us?now-last_us:1000;
    const bool bad_clock=last_us&&(now<=last_us||elapsed>10000);last_us=now;
    bool valid[2]={fresh(samples[0],now),fresh(samples[1],now)};
    for(unsigned n=0;n<2;n++) if(valid[n]&&samples[n].temperature_c>=80) {
        if(!state.overtemperature) ++state.protection_count;
        state.overtemperature=true;state.hot_sensor_mask|=(uint8_t)(1u<<n);cool_us=0;
    }
    state.feedback_missing=!valid[0]&&!valid[1];
    if(state.overtemperature) {
        bool cool=!state.feedback_missing;
        for(unsigned n=0;n<2;n++) {
            if((state.hot_sensor_mask&(1u<<n))&&!valid[n]) cool=false;
            if(valid[n]&&samples[n].temperature_c>=75) cool=false;
        }
        if(!cool) cool_us=0;
        else if(!cool_us) cool_us=now;
        if(cool_us&&now-cool_us>=5000000) {state.overtemperature=false;state.hot_sensor_mask=0;++state.protection_recoveries;cool_us=0;needs_recovery=true;good_feedback_us=0;}
        else {state.faulted=true;off();return state;}
    }
    state.faulted=state.hardware_fault;
    if(bad_clock||state.feedback_missing) {
        needs_recovery=started;good_feedback_us=good_lsm_us=0;state.source=-1;off();return state;
    }
    if(needs_recovery) {
        if(!good_feedback_us) good_feedback_us=now;
        if(now-good_feedback_us<100000) {off();return state;}
        needs_recovery=false;
    }
    if(valid[1]) {if(!good_lsm_us)good_lsm_us=now;} else good_lsm_us=0;
    int source=valid[1]&&(state.source!=0||!valid[0]||now-good_lsm_us>=100000)?1:0;
    state.feedback_c=samples[source].temperature_c;
    if(source!=state.source) {state.source=(int8_t)source;++state.source_selections;off();}
    started=true;
    if(std::fabs(state.feedback_c-state.target_c)<=.25f) {
        if(!stable_us) stable_us=now;
        state.thermal_ready=now-stable_us>=5000000;
    } else {stable_us=0;state.thermal_ready=false;}
    if(state.feedback_c<state.target_c-10) {
        pid.track(state.target_c,state.feedback_c,500);state.duty=1;
    } else {
        auto result=pid.step({state.target_c,state.feedback_c,0,(float)elapsed*1e-6f});
        if(result.status!=SRNOS::Control::PidStatus::Ok) {off();needs_recovery=true;good_feedback_us=0;}
        else state.duty=result.output/500;
    }
    return state;
}
