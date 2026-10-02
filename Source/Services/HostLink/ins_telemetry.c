#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Services/HostLink/INSTelemetry.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include <string.h>
typedef struct {
    union {SRNOS_INSAttitude attitude;SRNOS_MagneticSample magnetic;SRNOS_PressureSample pressure;} value;
    SRNOS_INSPhase phase;
} item_t;
typedef struct {
    item_t *items;
    uint32_t capacity,head,tail,high_water,enqueued,overflow,sent,failed,discarded,last_sequence;
    uint16_t stream_id,record_bytes,period_us;
    bool enabled,seen;
} stream_t;
static item_t poses[64],magnets[16],pressures[8],batch[8];
static stream_t streams[3]={
    {.items=poses,.capacity=64,.stream_id=SRNOS_INS_STREAM_ATTITUDE,.record_bytes=108,.period_us=1000},
    {.items=magnets,.capacity=16,.stream_id=SRNOS_INS_STREAM_MAGNETIC,.record_bytes=52,.period_us=10000},
    {.items=pressures,.capacity=8,.stream_id=SRNOS_INS_STREAM_PRESSURE,.record_bytes=44,.period_us=62500}
};
static bool registered;
static uint8_t encoded[432];
static void u32(uint8_t *b,size_t cap,size_t at,uint32_t v) {(void)SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(b,cap),at,v);}
static void u64(uint8_t *b,size_t cap,size_t at,uint64_t v) {(void)SRNOS_Endian_StoreLE64(SRNOS_Memory_MutableBytes(b,cap),at,v);}
static void floats(uint8_t *b,size_t cap,size_t at,const float *v,unsigned n) {
    for(unsigned i=0;i<n;i++){uint32_t bits;memcpy(&bits,&v[i],4);u32(b,cap,at+4*i,bits);}
}
static void header(uint8_t *b,size_t cap,const SRNOS_SampleHeader *h) {
    memset(b,0,cap);b[0]=1;b[1]=(uint8_t)h->state;u32(b,cap,4,h->sequence);u32(b,cap,8,h->valid_fields);
    u64(b,cap,12,h->acquisition_start_us);u64(b,cap,20,h->acquisition_end_us);
}
bool SRNOS_INSEncodeAttitude(const SRNOS_INSAttitude *p,SRNOS_INSPhase phase,uint8_t *out,size_t cap) {
    if(!p||!out||cap<108)return false;
    cap=108;memset(out,0,cap);out[0]=1;out[1]=(uint8_t)phase;out[2]=p->source_mask;out[3]=p->new_sample_mask;
    u32(out,cap,4,p->sequence);u32(out,cap,8,p->epoch);u32(out,cap,12,p->flags);
    u64(out,cap,16,p->attitude_us);u64(out,cap,24,p->published_us);
    for(unsigned i=0;i<2;i++){u32(out,cap,32+4*i,p->input_sequence[i]);u32(out,cap,40+4*i,p->input_age_us[i]);}
    floats(out,cap,48,p->weights,2);floats(out,cap,56,p->quaternion_wxyz,4);floats(out,cap,72,p->euler_rad,3);
    floats(out,cap,84,p->gyro_rad_s,3);floats(out,cap,96,p->accel_m_s2,3);return true;
}
bool SRNOS_INSEncodeMagnetic(const SRNOS_MagneticSample *s,uint8_t *out,size_t cap) {
    if(!s||!out||cap<52)return false;cap=52;header(out,cap,&s->header);
    for(unsigned i=0;i<3;i++)(void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(out,cap),28+2*i,(uint16_t)s->raw_magnetic[i]);
    (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(out,cap),34,(uint16_t)s->raw_temperature);
    floats(out,cap,36,s->magnetic_uT,3);floats(out,cap,48,&s->temperature_c,1);return true;
}
bool SRNOS_INSEncodePressure(const SRNOS_PressureSample *s,uint8_t *out,size_t cap) {
    if(!s||!out||cap<44)return false;cap=44;header(out,cap,&s->header);
    u32(out,cap,28,(uint32_t)s->raw_pressure);u32(out,cap,32,(uint32_t)s->raw_temperature);
    floats(out,cap,36,&s->pressure_pa,1);floats(out,cap,40,&s->temperature_c,1);return true;
}
void SRNOS_INSTelemetryObserve(const SRNOS_INSSnapshot *s) {
    if(!s||!registered)return;
    const uint32_t sequence[3]={s->attitude.sequence,s->magnetic.header.sequence,s->pressure.header.sequence};
    const bool valid[3]={(s->attitude.flags&SRNOS_INS_VALID)!=0,
        s->magnetic.header.state==SRNOS_SENSOR_READY&&(s->magnetic.header.valid_fields&SRNOS_SENSOR_MAG),
        s->pressure.header.state==SRNOS_SENSOR_READY&&(s->pressure.header.valid_fields&SRNOS_SENSOR_PRESSURE)};
    for(unsigned i=0;i<3;i++) {
        stream_t *st=&streams[i];
        taskENTER_CRITICAL();
        bool changed=!st->seen||sequence[i]!=st->last_sequence;
        st->seen=true;st->last_sequence=sequence[i];
        if(valid[i] && st->enabled && changed) {
            if(st->head-st->tail==st->capacity)++st->overflow;
            else {
                item_t *item=&st->items[st->head%st->capacity];item->phase=s->status.phase;
                if(i==0)item->value.attitude=s->attitude;
                else if(i==1)item->value.magnetic=s->magnetic;
                else item->value.pressure=s->pressure;
                ++st->head;++st->enqueued;
                if(st->head-st->tail>st->high_water)st->high_water=st->head-st->tail;
            }
        }
        taskEXIT_CRITICAL();
    }
}
static void configure(void *user,bool enabled,uint32_t period) {
    (void)period;stream_t *s=user;taskENTER_CRITICAL();s->discarded+=s->head-s->tail;s->tail=s->head;s->enabled=enabled;taskEXIT_CRITICAL();
}
static void delivered(void *user,uint32_t count,SRNOS_HostLinkDelivery result) {
    stream_t *s=user;taskENTER_CRITICAL();
    if(result==SRNOS_HOST_LINK_DELIVERED)s->sent+=count;else if(result==SRNOS_HOST_LINK_FAILED)s->failed+=count;else s->discarded+=count;
    taskEXIT_CRITICAL();
}
static uint64_t timestamp(const stream_t *s,const item_t *item) {
    return s->stream_id==0x102?item->value.attitude.attitude_us:s->stream_id==0x103?item->value.magnetic.header.acquisition_end_us:item->value.pressure.header.acquisition_end_us;
}
static uint32_t sequence(const stream_t *s,const item_t *item) {
    return s->stream_id==0x102?item->value.attitude.sequence:s->stream_id==0x103?item->value.magnetic.header.sequence:item->value.pressure.header.sequence;
}
static void pump(void *user,uint32_t now_ms) {
    (void)now_ms;stream_t *s=user;uint64_t now=SRNOS_TimeGetUptimeUs();unsigned maximum=sizeof(encoded)/s->record_bytes;
    if(maximum>8)maximum=8;
    taskENTER_CRITICAL();unsigned count=s->head-s->tail;
    if(!count||(count<maximum&&now-timestamp(s,&s->items[s->tail%s->capacity])<4000)){taskEXIT_CRITICAL();return;}
    if(count>maximum)count=maximum;
    const uint32_t tail=s->tail;taskEXIT_CRITICAL();
    for(unsigned i=0;i<count;i++) {
        taskENTER_CRITICAL();batch[i]=s->items[(tail+i)%s->capacity];taskEXIT_CRITICAL();
    }
    bool ok=true;
    for(unsigned i=0;i<count;i++) {
        uint8_t *out=encoded+i*s->record_bytes;
        if(s->stream_id==0x102)ok=ok&&SRNOS_INSEncodeAttitude(&batch[i].value.attitude,batch[i].phase,out,s->record_bytes);
        else if(s->stream_id==0x103)ok=ok&&SRNOS_INSEncodeMagnetic(&batch[i].value.magnetic,out,s->record_bytes);
        else ok=ok&&SRNOS_INSEncodePressure(&batch[i].value.pressure,out,s->record_bytes);
    }
    SRNOS_HostLinkResult result=ok?SRNOS_HostLinkPublishTelemetry(s->stream_id,sequence(s,&batch[0]),(uint32_t)timestamp(s,&batch[0]),s->period_us,(uint8_t)count,encoded,count*s->record_bytes):SRNOS_HOST_LINK_ERR_PARAM;
    if(result==SRNOS_HOST_LINK_ERR_FULL||result==SRNOS_HOST_LINK_ERR_BUSY)return;
    taskENTER_CRITICAL();s->tail+=count;if(result!=SRNOS_HOST_LINK_OK)s->failed+=count;taskEXIT_CRITICAL();
}
static uint16_t status(void *user,const SRNOS_ProtocolView *request,uint8_t *out,size_t cap,size_t *len,uint8_t *flags) {
    (void)user;*len=0;*flags=0;if(request->payload_len)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    if(cap<256)return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    SRNOS_INSStatus st;SRNOS_INSGetStatus(&st);memset(out,0,256);cap=256;
    out[0]=1;out[1]=(uint8_t)st.phase;out[2]=st.healthy_imu_mask;
    out[3]=(uint8_t)(st.heater.enabled|st.heater.thermal_ready<<1|st.heater.overtemperature<<2|st.heater.hardware_fault<<3|st.heater.feedback_missing<<4);
    u32(out,cap,4,st.flags);u32(out,cap,8,st.epoch);u64(out,cap,12,st.updated_us);
    const uint32_t values[11]={st.cycles,st.publications,st.dual_cycles,st.single_cycles,st.predicted_cycles,st.missing_cycles,st.recoveries,st.missed_deadlines,st.max_execution_us,st.max_period_us,st.max_copy_us};
    for(unsigned i=0;i<11;i++)u32(out,cap,20+4*i,values[i]);
    out[64]=(uint8_t)st.heater.source;out[65]=st.heater.hot_sensor_mask;
    for(unsigned i=0;i<4;i++)out[68+i]=(uint8_t)st.sensors[i];
    const float temperatures[3]={st.heater.target_c,st.heater.feedback_c,st.heater.duty};floats(out,cap,72,temperatures,3);
    u32(out,cap,84,st.heater.protection_count);u32(out,cap,88,st.heater.protection_recoveries);u32(out,cap,92,st.heater.source_selections);
    const SRNOS_SystemConfig *c=SRNOS_SystemGetConfig();if(c)floats(out,cap,96,c->ins.board_to_robot_wxyz,4);
    for(unsigned i=0;i<3;i++) {
        stream_t *s=&streams[i];unsigned p=112+48*i;
        taskENTER_CRITICAL();
        out[p]=(uint8_t)s->stream_id;out[p+1]=(uint8_t)(s->stream_id>>8);out[p+2]=s->enabled;
        uint32_t counters[10]={s->last_sequence,s->head-s->tail,s->high_water,s->enqueued,s->overflow,s->sent,s->failed,s->discarded,s->capacity,0};
        taskEXIT_CRITICAL();for(unsigned j=0;j<10;j++)u32(out,cap,p+4+4*j,counters[j]);
    }
    *len=256;return SRNOS_HOST_LINK_STATUS_OK;
}
SRNOS_HostLinkResult SRNOS_INSTelemetryRegister(SRNOS_HostLinkRegistration *link,void *user) {
    (void)user;if(registered)return SRNOS_HOST_LINK_ERR_BUSY;
    for(unsigned i=0;i<3;i++) {
        stream_t *s=&streams[i];SRNOS_HostLinkTelemetryConfig config={
            .stream_id=s->stream_id,.max_sample_size=sizeof(encoded),.required_period_us=s->period_us,.poll_interval_ms=1,.policy=SRNOS_HOST_LINK_FIFO,
            .on_config=configure,.poll=pump,.on_delivery=delivered,.user=s};
        SRNOS_HostLinkResult r=SRNOS_HostLinkRegisterTelemetryEx(&config);if(r!=SRNOS_HOST_LINK_OK)return r;
    }
    SRNOS_HostLinkResult r=SRNOS_HostLinkRegisterHandler(link,SRNOS_HOST_LINK_SERVICE_TELEMETRY,SRNOS_INS_TELEMETRY_STATUS_MESSAGE,
        SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),SRNOS_HOST_LINK_QOS_RELIABLE,status,NULL);
    registered=r==SRNOS_HOST_LINK_OK;return r;
}
