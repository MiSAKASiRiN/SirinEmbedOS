#include "ins_internal.h"
#include "Devices/sensor_frames.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#define SRNOS_INS_GRAVITY 9.80665f
#define SRNOS_INS_GYRO_STILL 0.0872664626f
static float norm3(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static bool finite3(const float *v) { return isfinite(v[0])&&isfinite(v[1])&&isfinite(v[2]); }
static uint32_t age(const SRNOS_SampleHeader *h,uint64_t now) {
    if(!h->acquisition_end_us || now<h->acquisition_end_us) return UINT32_MAX;
    uint64_t d=now-h->acquisition_end_us;return d>UINT32_MAX?UINT32_MAX:(uint32_t)d;
}
static void rotate(const float q[4],float v[3]) {
    float t[3]={2*(q[2]*v[2]-q[3]*v[1]),2*(q[3]*v[0]-q[1]*v[2]),2*(q[1]*v[1]-q[2]*v[0])};
    v[0]+=q[0]*t[0]+q[2]*t[2]-q[3]*t[1];
    v[1]+=q[0]*t[1]+q[3]*t[0]-q[1]*t[2];
    v[2]+=q[0]*t[2]+q[1]*t[1]-q[2]*t[0];
}
static void transform(SRNOS_INSCore *c,SRNOS_SensorId id,float v[3]) {
    SRNOS_BoardSensorToBoard(id,v,v);rotate(c->config.board_to_robot_wxyz,v);
}
static bool quiet(const float *a,const float *g) {
    float n=norm3(a);return n>=.9f*SRNOS_INS_GRAVITY&&n<=1.1f*SRNOS_INS_GRAVITY&&norm3(g)<SRNOS_INS_GYRO_STILL;
}
static bool observe(SRNOS_INSWindow *w,const float *a,const float *g,uint64_t now) {
    if(!quiet(a,g)) { memset(w,0,sizeof(*w));return false; }
    /* Bound variance history and avoid an ever-growing runtime accumulator. */
    if(w->count>=1000) memset(w,0,sizeof(*w));
    if(!w->count) w->start_us=now;
    ++w->count;
    for(unsigned i=0;i<6;i++) {
        float x=i<3?a[i]:g[i-3],d=x-w->mean[i];
        w->mean[i]+=d/(float)w->count;w->m2[i]+=d*(x-w->mean[i]);
    }
    float av=0,gv=0;
    for(unsigned i=0;i<3;i++){av+=w->m2[i];gv+=w->m2[i+3];}
    /* RMS over the 3*N axis components (not the norm of the 3-D residual). */
    if(av/(3.0f*(float)w->count)>(.03f*SRNOS_INS_GRAVITY)*(.03f*SRNOS_INS_GRAVITY)||
       gv/(3.0f*(float)w->count)>.00872664626f*.00872664626f) {
        memset(w,0,sizeof(*w));return false;
    }
    return true;
}
static float gain(const float *a,const float *g) {
    const float n=norm3(a)/SRNOS_INS_GRAVITY,w=norm3(g);
    float aw=n<=.7f||n>=1.3f?0:n<.9f?(n-.7f)/.2f:n<=1.1f?1:(1.3f-n)/.2f;
    float gw=w<=SRNOS_INS_GYRO_STILL?1:w>=1.396263402f?.08f:1-.92f*(w-SRNOS_INS_GYRO_STILL)/(1.396263402f-SRNOS_INS_GYRO_STILL);
    return aw*gw;
}
void SRNOS_INSCoreInit(SRNOS_INSCore *c,const SRNOS_INSConfig *cfg,uint64_t now) {
    memset(c,0,sizeof(*c));c->config=*cfg;c->probe_us=now;
    c->snapshot.status.phase=cfg->enabled?SRNOS_INS_PROBING:SRNOS_INS_DISABLED;
    c->snapshot.attitude.quaternion_wxyz[0]=1;
    SRNOS_MahonyInit(&c->filter,1,0,.001f);
}
void SRNOS_INSCoreSetCalibration(SRNOS_INSCore *c,const SRNOS_IMUCalibration values[2],uint8_t mask) {
    /* Startup-only publication, before this core begins observation/fusion. */
    memcpy(c->calibration,values,sizeof(c->calibration));
    c->calibration_mask=mask&3u;c->calibration_pending=false;
}
static void invalidate(SRNOS_INSCore *c) {
    c->snapshot.attitude.flags&=~SRNOS_INS_VALID;
    c->snapshot.attitude.source_mask=0;
    c->snapshot.attitude.weights[0]=c->snapshot.attitude.weights[1]=0;
}
bool SRNOS_INSCoreStep(SRNOS_INSCore *c,const SRNOS_IMUSample input[2],
    const SRNOS_MagneticSample *mag,const SRNOS_PressureSample *pressure,
    const SRNOS_HeaterStatus *heater,uint64_t now) {
    SRNOS_INSSnapshot *s=&c->snapshot;SRNOS_INSAttitude *p=&s->attitude;SRNOS_INSStatus *st=&s->status;
    uint64_t elapsed=c->last_step_us&&now>c->last_step_us?now-c->last_step_us:1000;
    bool gap=c->last_step_us && (now<=c->last_step_us||elapsed>5000);
    c->last_step_us=now;st->updated_us=now;++st->cycles;
    if(elapsed>st->max_period_us) st->max_period_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    if(elapsed>1500) ++st->missed_deadlines;
    st->heater=*heater;st->healthy_imu_mask=0;
    st->flags=SRNOS_INS_UNCALIBRATED|SRNOS_INS_RELATIVE_HEADING;
    st->calibrated=false;st->calibrated_imu_mask=c->calibration_mask;
    st->calibration_load_pending=c->calibration_pending;
    if(heater->enabled&&!heater->thermal_ready) st->flags|=SRNOS_INS_THERMAL_PENDING;
    if(heater->overtemperature) st->flags|=SRNOS_INS_HEATER_PROTECTION;
    if(heater->hardware_fault) st->flags|=SRNOS_INS_HEATER_FAULT;
    if(c->warmup_expired) st->flags|=SRNOS_INS_WARMUP_TIMEOUT;
    unsigned fresh_mask=0;
    for(unsigned n=0;n<2;n++) {
        s->imu[n]=input[n];
        if(c->calibration_mask&(1u<<n)) for(unsigned i=0;i<3;i++) {
            s->imu[n].gyro_rad_s[i]-=c->calibration[n].gyro_bias_rad_s[i];
            s->imu[n].accel_m_s2[i]=(s->imu[n].accel_m_s2[i]-c->calibration[n].accel_bias_m_s2[i])*c->calibration[n].accel_scale[i];
        }
        transform(c,(SRNOS_SensorId)n,s->imu[n].accel_m_s2);transform(c,(SRNOS_SensorId)n,s->imu[n].gyro_rad_s);
        st->sensors[n]=input[n].header.state;p->input_sequence[n]=input[n].header.sequence;
        p->input_age_us[n]=age(&input[n].header,now);
        bool valid=input[n].header.state==SRNOS_SENSOR_READY &&
            (input[n].header.valid_fields&3u)==3u && finite3(s->imu[n].accel_m_s2)&&finite3(s->imu[n].gyro_rad_s);
        if(valid&&p->input_age_us[n]<=5000) st->healthy_imu_mask|=(uint8_t)(1u<<n);
        if(valid&&p->input_age_us[n]<=2000) {fresh_mask|=1u<<n;if(!c->good_since[n])c->good_since[n]=now;}
        else c->good_since[n]=0;
    }
    s->magnetic=*mag;transform(c,SRNOS_SENSOR_LIS2MDL,s->magnetic.magnetic_uT);
    s->pressure=*pressure;st->sensors[2]=mag->header.state;st->sensors[3]=pressure->header.state;
    p->new_sample_mask=0;
    if(!c->config.enabled) {st->phase=SRNOS_INS_DISABLED;invalidate(c);return false;}
    /* The worker still samples and heats while AppInit resolves EEPROM. */
    if(c->calibration_pending) {st->phase=SRNOS_INS_PROBING;invalidate(c);return false;}
    if(!st->healthy_imu_mask) {
        invalidate(c);++st->missing_cycles;c->session_active=false;c->seeded=false;c->running=false;
        memset(&c->window,0,sizeof(c->window));
        st->phase=c->ever_available?SRNOS_INS_OFFLINE:SRNOS_INS_PROBING;
        if(!c->ever_available&&now-c->probe_us>=3000000) {st->phase=SRNOS_INS_FATAL;return true;}
        return false;
    }
    if(!c->session_active&&fresh_mask) {
        if(c->ever_available) ++st->recoveries;
        c->ever_available=c->session_active=true;c->thermal_start_us=now;c->warmup_expired=false;
        c->primary=fresh_mask&1?0:1;++st->epoch;p->epoch=st->epoch;
        st->phase=SRNOS_INS_WARMUP;
    }
    if(!c->session_active) return false;
    if(gap&&c->seeded) {c->seeded=false;c->running=false;memset(&c->window,0,sizeof(c->window));++st->epoch;p->epoch=st->epoch;invalidate(c);}
    unsigned selected=fresh_mask;
    if(fresh_mask==3) {
        uint64_t t0=input[0].header.acquisition_end_us,t1=input[1].header.acquisition_end_us;
        uint32_t skew=(uint32_t)(t0>t1?t0-t1:t1-t0);
        if(skew>st->max_input_skew_us) st->max_input_skew_us=skew;
        if(skew>1000) {selected=t0>=t1?1:2;st->flags|=SRNOS_INS_TIME_SKEW;}
    }
    bool predicted=!selected;
    if(selected) {
        if(!(selected&(1u<<c->primary))) c->primary=selected&1?0:1;
        unsigned other=1u-c->primary;
        p->weights[other]=0;p->weights[c->primary]=1;
        if(selected==3&&now-c->good_since[other]>=100000) {
            float w=(float)(now-c->good_since[other]-100000)/200000.0f;
            p->weights[other]=w>.5f?.5f:w;p->weights[c->primary]=1-p->weights[other];
        }
        p->source_mask=0;
        for(unsigned i=0;i<3;i++) p->accel_m_s2[i]=p->gyro_rad_s[i]=0;
        for(unsigned n=0;n<2;n++) if(p->weights[n]>0) {
            p->source_mask|=(uint8_t)(1u<<n);
            if(input[n].header.sequence!=c->used_sequence[n]) p->new_sample_mask|=(uint8_t)(1u<<n);
            c->used_sequence[n]=input[n].header.sequence;
            for(unsigned i=0;i<3;i++) {p->accel_m_s2[i]+=p->weights[n]*s->imu[n].accel_m_s2[i];p->gyro_rad_s[i]+=p->weights[n]*s->imu[n].gyro_rad_s[i];}
        }
        predicted=p->new_sample_mask==0;
    }
    if(!selected) {
        /* Predict only from a currently healthy input, never a failed former primary. */
        unsigned n=(st->healthy_imu_mask==3)?(p->input_age_us[0]<=p->input_age_us[1]?0:1):(st->healthy_imu_mask&1?0:1);
        p->source_mask=(uint8_t)(1u<<n);p->weights[n]=1;p->weights[1-n]=0;
        memcpy(p->gyro_rad_s,s->imu[n].gyro_rad_s,sizeof(p->gyro_rad_s));
        memcpy(p->accel_m_s2,s->imu[n].accel_m_s2,sizeof(p->accel_m_s2));
    }
    if(p->source_mask!=3) st->flags|=SRNOS_INS_SINGLE;
    if(p->source_mask && !(p->source_mask&~c->calibration_mask)) {
        st->calibrated=true;st->flags&=~SRNOS_INS_UNCALIBRATED;
    }
    if(predicted) st->flags|=SRNOS_INS_PREDICTED;
    if(!c->seeded) {
        invalidate(c);
        if(heater->enabled&&!heater->thermal_ready&&now-c->thermal_start_us<60000000) {
            st->phase=SRNOS_INS_WARMUP;memset(&c->window,0,sizeof(c->window));return false;
        }
        if(heater->enabled&&!heater->thermal_ready) {c->warmup_expired=true;st->flags|=SRNOS_INS_WARMUP_TIMEOUT;}
        st->phase=SRNOS_INS_OBSERVING;
        if(!selected||predicted) {memset(&c->window,0,sizeof(c->window));return false;}
        if(observe(&c->window,p->accel_m_s2,p->gyro_rad_s,now)&&c->window.count>=100&&now-c->window.start_us>=500000) {
            SRNOS_Axis3f a={c->window.mean[0],c->window.mean[1],c->window.mean[2]};
            SRNOS_MahonyInit(&c->filter,1,0,.001f);
            if(SRNOS_MahonySeedFromAccel(&c->filter,a)) {c->seeded=true;c->convergence_us=now;memset(&c->window,0,sizeof(c->window));st->phase=SRNOS_INS_CONVERGING;}
        }
        return false;
    }
    if(c->warmup_expired) st->flags|=SRNOS_INS_WARMUP_TIMEOUT;
    bool still=false;
    if(!predicted) still=observe(&c->window,p->accel_m_s2,p->gyro_rad_s,now);
    else memset(&c->window,0,sizeof(c->window));
    c->filter.dt=(float)elapsed*1e-6f;
    SRNOS_MahonySetGain(&c->filter,predicted?0:gain(p->accel_m_s2,p->gyro_rad_s),0);
    SRNOS_Axis3f a={p->accel_m_s2[0],p->accel_m_s2[1],p->accel_m_s2[2]},g={p->gyro_rad_s[0],p->gyro_rad_s[1],p->gyro_rad_s[2]};
    SRNOS_MahonyInput(&c->filter,g,a);SRNOS_MahonyUpdate(&c->filter);SRNOS_MahonyOutput(&c->filter);
    if(!c->running&&now-c->convergence_us>=2000000&&still&&now-c->window.start_us>=200000) c->running=true;
    if(!c->running) {st->phase=SRNOS_INS_CONVERGING;return false;}
    float q[4]={c->filter.q0,c->filter.q1,c->filter.q2,c->filter.q3},dot=0;
    for(unsigned i=0;i<4;i++) dot+=q[i]*p->quaternion_wxyz[i];
    for(unsigned i=0;i<4;i++) p->quaternion_wxyz[i]=dot<0?-q[i]:q[i];
    p->euler_rad[0]=c->filter.roll;p->euler_rad[1]=c->filter.pitch;p->euler_rad[2]=c->filter.yaw;
    p->flags=st->flags|SRNOS_INS_VALID;p->attitude_us=now;p->published_us=now;++p->sequence;++st->publications;
    if(predicted) ++st->predicted_cycles;else if(p->source_mask==3) ++st->dual_cycles;else ++st->single_cycles;
    st->flags=p->flags;
    st->phase=(st->flags&(SRNOS_INS_UNCALIBRATED|SRNOS_INS_SINGLE|SRNOS_INS_TIME_SKEW|SRNOS_INS_PREDICTED|SRNOS_INS_THERMAL_PENDING|SRNOS_INS_HEATER_PROTECTION|SRNOS_INS_HEATER_FAULT))?SRNOS_INS_DEGRADED:SRNOS_INS_RUNNING;
    return false;
}
