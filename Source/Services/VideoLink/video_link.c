#include "SirinEmbedOS/Services/VideoLink.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "video_link_internal.h"
#include "../Message/message_internal.h"
#include "../Telemetry/telemetry_internal.h"
#include "codec.h"
#include "FreeRTOS.h"
#include "task.h"

typedef struct { SRNOS_TelemetryHandle handle; uint16_t id,period_ms,max_age_ms; uint32_t next_ms; } route_t;
static route_t routes[SRNOS_VIDEO_LINK_CAPACITY];
static SRNOS_SerialChannel *channel;
static uint16_t route_count,route_cursor,descriptor_cursor;
static bool frozen,tx_seen,ack_pending;
static uint8_t a5_sequence;
static uint32_t packet_sequence,last_tx_ms,next_directory_ms;
static uint8_t ack[30],packet[300];
static SRNOS_VideoLinkStats stats;

static bool task_context(void) {
    return xPortIsInsideInterrupt()!=pdTRUE && xTaskGetSchedulerState()==taskSCHEDULER_RUNNING;
}
bool SRNOS_VideoLinkPrepare(SRNOS_SerialChannel *c) {
    const SRNOS_SerialConfig *config=SRNOS_SerialGetConfig(c);
    if(channel||frozen||!config||!config->rx_enabled||!config->tx_enabled||
       config->mode!=SRNOS_SERIAL_DUPLEX)return false;
    channel=c;return true; /* VT03 remains the sole Serial RX owner. */
}
void SRNOS_VideoLinkFreeze(void) { frozen=true; }
SRNOS_VideoLinkResult SRNOS_VideoLinkRegisterTelemetry(SRNOS_TelemetryHandle h,uint16_t id,uint16_t period,uint16_t age) {
    if(frozen)return SRNOS_VIDEO_LINK_FROZEN;
    if(xPortIsInsideInterrupt()==pdTRUE||xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)return SRNOS_VIDEO_LINK_CONTEXT;
    if(!channel)return SRNOS_VIDEO_LINK_DISABLED;
    if(h>=SRNOS_TelemetryCount()||!id||period<SRNOS_VIDEO_LINK_TX_INTERVAL_MS||period>60000||period%SRNOS_VIDEO_LINK_TX_INTERVAL_MS||age<20||age>60000||
       route_count==SRNOS_VIDEO_LINK_CAPACITY)return SRNOS_VIDEO_LINK_INVALID;
    for(unsigned i=0;i<route_count;i++)if(routes[i].id==id||routes[i].handle==h)return SRNOS_VIDEO_LINK_INVALID;
    routes[route_count++]=(route_t){h,id,period,age,0};return SRNOS_VIDEO_LINK_OK;
}
void SRNOS_VideoLinkOnFrame(uint16_t cmd,const uint8_t *data,uint16_t length,uint8_t seq,void *user) {
    (void)user;(void)seq;
    if(!channel||cmd!=0x0311||length!=30)return;
    const uint32_t now=SRNOS_TimeMs();
    taskENTER_CRITICAL();stats.rx_messages++;stats.last_rx_ms=now;taskEXIT_CRITICAL();
    /* Service-owned requests never enter a chassis/control callback. */
    if(!memcmp(data,"SRVL",4)&&data[4]==1&&(data[5]==0x80||data[5]==0x81)&&!data[6]&&!data[7]) {
        if(ack_pending){taskENTER_CRITICAL();stats.rx_dropped++;taskEXIT_CRITICAL();}
        memcpy(ack,data,30);ack_pending=true;
        if(data[5]==0x80){descriptor_cursor=0;next_directory_ms=now+5000u;}
        return;
    }
    if(SRNOS_MessageReceiveVideo(data,length,now)!=SRNOS_MESSAGE_OK) {
        taskENTER_CRITICAL();stats.rx_dropped++;taskEXIT_CRITICAL();
    }
}
SRNOS_VideoLinkResult SRNOS_VideoLinkSend(const uint8_t *data) {
    if(!task_context())return SRNOS_VIDEO_LINK_CONTEXT;
    if(!channel||!frozen)return SRNOS_VIDEO_LINK_DISABLED;
    if(!data)return SRNOS_VIDEO_LINK_INVALID;
    uint32_t now=SRNOS_TimeMs();uint8_t seq;
    taskENTER_CRITICAL();
    if(tx_seen&&now-last_tx_ms<SRNOS_VIDEO_LINK_TX_INTERVAL_MS){stats.tx_busy++;taskEXIT_CRITICAL();return SRNOS_VIDEO_LINK_BUSY;}
    tx_seen=true;last_tx_ms=now;seq=a5_sequence++;
    taskEXIT_CRITICAL();
    uint8_t frame[309];SRNOS_VideoLinkEncode(0x0310,data,300,seq,frame);
    SRNOS_SerialStatus status=SRNOS_SerialWrite(channel,frame,sizeof(frame),0);
    taskENTER_CRITICAL();
    if(status==SRNOS_SERIAL_OK){stats.tx_frames++;stats.last_tx_ms=now;}
    else stats.tx_errors++;
    taskEXIT_CRITICAL();
    return status==SRNOS_SERIAL_OK?SRNOS_VIDEO_LINK_OK:SRNOS_VIDEO_LINK_BUSY;
}
SRNOS_VideoLinkResult SRNOS_VideoLinkGetStats(SRNOS_VideoLinkStats *out) {
    if(!task_context())return SRNOS_VIDEO_LINK_CONTEXT;
    if(!out)return SRNOS_VIDEO_LINK_INVALID;
    taskENTER_CRITICAL();*out=stats;taskEXIT_CRITICAL();return SRNOS_VIDEO_LINK_OK;
}
static void header(uint8_t kind,uint32_t now) {
    memset(packet,0,sizeof(packet));memcpy(packet,"SRVL",4);packet[4]=1;packet[5]=kind;
    SRNOS_VideoLinkPut32(packet+8,packet_sequence);SRNOS_VideoLinkPut32(packet+12,now);
}
void SRNOS_VideoLinkStep(uint32_t ignored_ms) {
    (void)ignored_ms;
    if(!channel||!frozen)return;
    const uint64_t now_us=SRNOS_TimeUs();const uint32_t now=SRNOS_TimeMs();
    taskENTER_CRITICAL();const bool limited=tx_seen&&now-last_tx_ms<SRNOS_VIDEO_LINK_TX_INTERVAL_MS;taskEXIT_CRITICAL();
    if(limited)return;
    uint16_t selected[17];unsigned selected_count=0;
    header(1,now);
    for(unsigned n=0;n<route_count&&selected_count<17;n++) {
        unsigned i=(route_cursor+n)%route_count;route_t *r=&routes[i];
        if((int32_t)(now-r->next_ms)<0)continue;
        const SRNOS_TelemetryDescriptor *d=SRNOS_TelemetryDescribe(r->handle);
        SRNOS_TelemetrySample s={0};
        if(!d||!SRNOS_TelemetryRead(r->handle,&s))continue;
        uint8_t *p=packet+16+selected_count*16;
        SRNOS_VideoLinkPut16(p,r->id);p[2]=(uint8_t)d->type;
        p[3]=(uint8_t)(s.valid&&s.sequence&&s.sampled_us<=now_us&&now_us-s.sampled_us<=(uint64_t)r->max_age_ms*1000u);
        SRNOS_VideoLinkPut32(p+4,(uint32_t)(s.sampled_us/1000u));
        SRNOS_VideoLinkPut32(p+8,s.sequence);
        uint32_t bits=0;
        if(d->type==SRNOS_TELEMETRY_FLOAT32)memcpy(&bits,&s.value.f32,4);
        else if(d->type==SRNOS_TELEMETRY_INT32)bits=(uint32_t)s.value.i32;
        else if(d->type==SRNOS_TELEMETRY_UINT32)bits=s.value.u32;
        else bits=s.value.boolean?1u:0u;
        SRNOS_VideoLinkPut32(p+12,bits);selected[selected_count++]=(uint16_t)i;
    }
    if(selected_count) {
        packet[6]=(uint8_t)selected_count;
        if(SRNOS_VideoLinkSend(packet)==SRNOS_VIDEO_LINK_OK) {
            for(unsigned n=0;n<selected_count;n++) {
                route_t *r=&routes[selected[n]];
                /* Keep nominal cadence; skip missed slots instead of bursting. */
                r->next_ms+=((now-r->next_ms)/r->period_ms+1u)*r->period_ms;
            }
            route_cursor=(selected[selected_count-1]+1u)%route_count;
            packet_sequence++;taskENTER_CRITICAL();stats.sample_packets++;taskEXIT_CRITICAL();
        }
        return;
    }
    if(ack_pending) {
        header(3,now);memcpy(packet+16,ack,30);
        if(SRNOS_VideoLinkSend(packet)==SRNOS_VIDEO_LINK_OK){ack_pending=false;packet_sequence++;}
        return;
    }
    if((int32_t)(now-next_directory_ms)>=0){descriptor_cursor=0;next_directory_ms=now+5000u;}
    if(descriptor_cursor<route_count) {
        const route_t *r=&routes[descriptor_cursor];
        const SRNOS_TelemetryDescriptor *d=SRNOS_TelemetryDescribe(r->handle);
        if(!d)return;
        header(2,now);packet[6]=1;SRNOS_VideoLinkPut16(packet+16,r->id);packet[18]=(uint8_t)d->type;
        packet[19]=(uint8_t)strlen(d->name);packet[20]=(uint8_t)strlen(d->unit);
        SRNOS_VideoLinkPut16(packet+22,r->period_ms);SRNOS_VideoLinkPut32(packet+24,r->max_age_ms);
        memcpy(packet+28,d->name,packet[19]);memcpy(packet+28+packet[19],d->unit,packet[20]);
        if(SRNOS_VideoLinkSend(packet)==SRNOS_VIDEO_LINK_OK) {
            descriptor_cursor++;packet_sequence++;taskENTER_CRITICAL();stats.descriptor_packets++;taskEXIT_CRITICAL();
        }
    }
}
