#include "input_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <math.h>

typedef struct {
    SRNOS_InputSnapshot snapshot;
    SRNOS_InputEvent events[SRNOS_INPUT_EVENT_DEPTH];
    uint32_t retained;
} input_state;
static SRNOS_InputDescriptor descriptors[SRNOS_INPUT_SOURCES];
static input_state *states[SRNOS_INPUT_SOURCES];
static uint8_t count;
static bool frozen, started;
static bool task_context(void)
{ return !xPortIsInsideInterrupt() && xTaskGetSchedulerState()==taskSCHEDULER_RUNNING; }
static bool startup(void)
{ return !xPortIsInsideInterrupt() && xTaskGetSchedulerState()==taskSCHEDULER_NOT_STARTED; }
static uint16_t channel_mask(unsigned n) { return (uint16_t)((1u<<n)-1u); }

bool SRNOS_InputNormalize(const SRNOS_InputChannelDescriptor *d,int16_t raw,int16_t *out)
{
    if(!d||!out||d->minimum>=d->center||d->center>=d->maximum)return false;
    int32_t delta=(int32_t)raw-d->center;
    int32_t span=delta<0?(int32_t)d->center-d->minimum:(int32_t)d->maximum-d->center;
    int32_t value=delta*10000/span;
    if(value>10000)value=10000;
    if(value< -10000)value=-10000;
    *out=(int16_t)(d->inverted?-value:value);return true;
}
SRNOS_InputResult SRNOS_InputRegister(const SRNOS_InputDescriptor *d,SRNOS_InputSource *out)
{
    if(!startup())return SRNOS_INPUT_CONTEXT;
    if(frozen)return SRNOS_INPUT_FROZEN;
    if(!d||!out||!d->name[0]||!memchr(d->name,0,sizeof(d->name))||
       !d->timeout_ms||d->timeout_ms>60000||d->channel_count>16||d->relative_mask>7)return SRNOS_INPUT_INVALID;
    for(unsigned i=0;i<d->channel_count;i++)
        if(d->channels[i].kind>SRNOS_INPUT_CHANNEL_POSITION)return SRNOS_INPUT_INVALID;
    for(unsigned i=0;i<count;i++)if(!strcmp(d->name,descriptors[i].name))return SRNOS_INPUT_INVALID;
    if(count==SRNOS_INPUT_SOURCES)return SRNOS_INPUT_FULL;
    descriptors[count]=*d;*out=count++;return SRNOS_INPUT_OK;
}
uint8_t SRNOS_InputCount(void) { return count; }
SRNOS_InputSource SRNOS_InputFind(const char *name)
{
    if(name)for(unsigned i=0;i<count;i++)if(!strcmp(name,descriptors[i].name))return (uint8_t)i;
    return SRNOS_INPUT_INVALID_SOURCE;
}
bool SRNOS_InputDescribe(SRNOS_InputSource source,SRNOS_InputDescriptor *out)
{
    if(source>=count||!out)return false;
    *out=descriptors[source];return true;
}
bool SRNOS_InputFreeze(void) { if(!startup()||frozen)return false;frozen=true;return true; }
uint32_t SRNOS_InputHeapBudget(void) { return count*(sizeof(input_state)+32u); }
bool SRNOS_InputStart(void)
{
    if(!startup()||!frozen||started)return false;
    for(unsigned i=0;i<count;i++) {
        states[i]=pvPortMalloc(sizeof(input_state));
        if(!states[i]) {
            for(unsigned n=0;n<i;n++){vPortFree(states[n]);states[n]=NULL;}
            return false;
        }
        memset(states[i],0,sizeof(input_state));
    }
    started=true;return true;
}
static void event(input_state *s,SRNOS_InputEvent *e)
{
    e->sequence=++s->snapshot.event_sequence;e->epoch=s->snapshot.epoch;
    s->events[(e->sequence-1u)%SRNOS_INPUT_EVENT_DEPTH]=*e;
    if(s->retained<SRNOS_INPUT_EVENT_DEPTH)++s->retained;
}
SRNOS_InputResult SRNOS_InputPublish(SRNOS_InputSource source,const SRNOS_InputFrame *f,uint32_t received)
{
    if(!task_context()||!started)return SRNOS_INPUT_CONTEXT;
    if(source>=count||!f)return SRNOS_INPUT_INVALID;
    const SRNOS_InputDescriptor *d=&descriptors[source];
    if((f->channel_mask&~channel_mask(d->channel_count))||(f->buttons_valid&~d->buttons)||
       (f->buttons&~f->buttons_valid)||(f->relative_mask&~d->relative_mask)||(f->flags&~7u)||
       (f->flags&&(f->request_enable||f->request_disable))||
       (f->request_enable&&f->request_disable)||
       (f->update_planar_velocity&&(!d->planar_velocity||f->channel_mask||
        !isfinite(f->planar_velocity.vx)||!isfinite(f->planar_velocity.vy)||
        !isfinite(f->planar_velocity.wz))))return SRNOS_INPUT_INVALID;
    uint32_t now=SRNOS_TimeMs();
    if(now-received>d->timeout_ms)return SRNOS_INPUT_STALE;
    taskENTER_CRITICAL();
    input_state *s=states[source];SRNOS_InputSnapshot *v=&s->snapshot;
    bool sync=!v->seen||v->flags||received-v->received_ms>d->timeout_ms;
    SRNOS_InputEvent e={.received_ms=received};
    if(f->flags) {
        if(!v->seen||!v->flags){++v->epoch;e.flags=SRNOS_INPUT_EVENT_SYNC|f->flags;event(s,&e);}
        v->channels_valid=0;v->buttons_valid=0;v->relative_valid=0;v->enable_epoch=0;v->disable_epoch=0;
        v->planar_velocity_valid=false;v->motion_kind=SRNOS_INPUT_MOTION_NONE;
    } else {
        if(sync) {
            ++v->epoch;v->channels_valid=0;v->buttons_valid=0;v->relative_valid=0;v->enable_epoch=0;v->disable_epoch=0;
            v->planar_velocity_valid=false;v->motion_kind=SRNOS_INPUT_MOTION_NONE;
            e.flags=SRNOS_INPUT_EVENT_SYNC;
        }
        const uint8_t motion=f->update_planar_velocity?SRNOS_INPUT_MOTION_PLANAR_VELOCITY:
            f->channel_mask?SRNOS_INPUT_MOTION_CHANNELS:SRNOS_INPUT_MOTION_NONE;
        if(motion && motion!=v->motion_kind) {
            ++v->motion_epoch;v->motion_kind=motion;
            v->channels_valid=0;v->planar_velocity_valid=false;v->enable_epoch=0;
        }
        if(f->update_planar_velocity) {
            v->planar_velocity=f->planar_velocity;v->planar_velocity_ms=received;
            v->planar_velocity_valid=true;
        }
        for(unsigned i=0;i<d->channel_count;i++)if(f->channel_mask&(1u<<i)) {
            v->raw[i]=f->raw[i];v->channel_ms[i]=received;v->channels_valid|=(uint16_t)(1u<<i);
        }
        if(f->update_buttons) {
            const uint64_t known=(!sync&&received-v->buttons_ms<=d->timeout_ms)?v->buttons_valid&f->buttons_valid:0;
            e.pressed=(f->buttons&~v->buttons)&known;
            e.released=(v->buttons&~f->buttons)&known;
            if(known!=f->buttons_valid)e.flags|=SRNOS_INPUT_EVENT_SYNC;
            v->buttons=f->buttons;v->buttons_valid=f->buttons_valid;v->buttons_ms=received;
        }
        if(f->relative_mask) {
            v->relative_valid=f->relative_mask;v->relative_ms=received;
            for(unsigned i=0;i<3;i++)if(f->relative_mask&(1u<<i)) {
                v->relative_total[i]+=(uint32_t)f->relative[i];e.relative[i]=f->relative[i];
                if(f->relative[i])e.relative_mask|=(uint8_t)(1u<<i);
            }
        }
        if(f->request_enable) {
            ++v->enable_sequence;v->enable_ms=received;v->enable_epoch=v->epoch;
        }
        if(f->request_disable) {
            ++v->disable_sequence;v->disable_ms=received;v->disable_epoch=v->epoch;
            v->enable_epoch=0;
        }
        if(e.flags||e.pressed||e.released||e.relative_mask)event(s,&e);
    }
    v->seen=true;v->received_ms=received;v->flags=f->flags;++v->sequence;
    taskEXIT_CRITICAL();return SRNOS_INPUT_OK;
}
SRNOS_InputResult SRNOS_InputInvalidate(SRNOS_InputSource source,uint8_t flags)
{
    if(!flags||flags&~7u)return SRNOS_INPUT_INVALID;
    SRNOS_InputFrame f={.flags=flags};return SRNOS_InputPublish(source,&f,SRNOS_TimeMs());
}
SRNOS_InputResult SRNOS_InputReadSnapshot(SRNOS_InputSource source,SRNOS_InputSnapshot *out)
{
    if(!task_context()||!started)return SRNOS_INPUT_CONTEXT;
    if(source>=count||!out)return SRNOS_INPUT_INVALID;
    taskENTER_CRITICAL();
    *out=states[source]->snapshot;
    uint32_t now=SRNOS_TimeMs();
    taskEXIT_CRITICAL();
    const SRNOS_InputDescriptor *d=&descriptors[source];
    out->online=out->seen&&!out->flags&&now-out->received_ms<=d->timeout_ms;
    out->normalized_valid=0;
    for(unsigned i=0;i<16;i++) {
        if(!out->online||now-out->channel_ms[i]>d->timeout_ms)out->channels_valid&=(uint16_t)~(1u<<i);
        if((out->channels_valid&(1u<<i))&&SRNOS_InputNormalize(&d->channels[i],out->raw[i],&out->normalized[i]))
            out->normalized_valid|=(uint16_t)(1u<<i);
    }
    if(!out->online||now-out->buttons_ms>d->timeout_ms)out->buttons_valid=0;
    if(!out->online||now-out->relative_ms>d->timeout_ms)out->relative_valid=0;
    if(!out->online||now-out->planar_velocity_ms>d->timeout_ms)out->planar_velocity_valid=false;
    return SRNOS_INPUT_OK;
}
SRNOS_InputResult SRNOS_InputReadEvent(SRNOS_InputSource source,uint32_t *cursor,SRNOS_InputEvent *out)
{
    if(!task_context()||!started)return SRNOS_INPUT_CONTEXT;
    if(source>=count||!cursor||!out)return SRNOS_INPUT_INVALID;
    taskENTER_CRITICAL();
    input_state *s=states[source];uint32_t pending=s->snapshot.event_sequence-*cursor;
    SRNOS_InputResult r=SRNOS_INPUT_OK;
    if(!pending)r=SRNOS_INPUT_EMPTY;
    else if(pending>=0x80000000u)r=SRNOS_INPUT_INVALID;
    else if(pending>s->retained){*cursor=s->snapshot.event_sequence-s->retained;r=SRNOS_INPUT_OVERRUN;}
    else {*out=s->events[*cursor%SRNOS_INPUT_EVENT_DEPTH];++*cursor;}
    taskEXIT_CRITICAL();return r;
}
