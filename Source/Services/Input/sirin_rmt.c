#include "SirinEmbedOS/Services/SirinRMT.h"
#include "SirinEmbedOS/Services/RemoteControl/VT02/VT02.h"
#include "input_internal.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include <string.h>
#include <math.h>
#include <float.h>
_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,
    "SirinRMT requires IEEE754 binary32");

bool SRNOS_SirinRMTDecode(const uint8_t *data,size_t size,SRNOS_SirinRMTPacket *out)
{
    if(!data||!out||size!=30||memcmp(data,"SRMT",4)||data[4]!=1||data[5]>6)return false;
    SRNOS_SirinRMTPacket p={0};SRNOS_Memory_ByteSpan b=SRNOS_Memory_Bytes(data,size);
    p.kind=(SRNOS_SirinRMTKind)data[5];
    (void)SRNOS_Endian_LoadLE32(b,6,&p.session);(void)SRNOS_Endian_LoadLE32(b,10,&p.sequence);
    if(!p.session)return false;
    if(p.kind==SRNOS_SIRIN_RMT_CHANNELS) {
        unsigned first=data[14],count=data[15];
        if(!count||count>7||first+count>16)return false;
        for(unsigned i=0;i<count;i++) {
            uint16_t raw;(void)SRNOS_Endian_LoadLE16(b,16+2*i,&raw);
            int32_t value=raw<32768?raw:(int32_t)raw-65536;
            if(value< -10000||value>10000)return false;
            p.frame.raw[first+i]=(int16_t)value;p.frame.channel_mask|=(uint16_t)(1u<<(first+i));
        }
        for(unsigned i=16+2*count;i<30;i++)if(data[i])return false;
    } else if(p.kind==SRNOS_SIRIN_RMT_BUTTONS) {
        p.frame.update_buttons=true;
        (void)SRNOS_Endian_LoadLE64(b,14,&p.frame.buttons_valid);
        (void)SRNOS_Endian_LoadLE64(b,22,&p.frame.buttons);
        if(p.frame.buttons&~p.frame.buttons_valid)return false;
    } else if(p.kind==SRNOS_SIRIN_RMT_RELATIVE) {
        p.frame.relative_mask=data[14];
        if(!data[14]||data[14]>7||data[15]||data[16]||data[17])return false;
        for(unsigned i=0;i<3;i++) {
            uint32_t raw;(void)SRNOS_Endian_LoadLE32(b,18+4*i,&raw);
            p.frame.relative[i]=raw<=INT32_MAX?(int32_t)raw:-(int32_t)(UINT32_MAX-raw)-1;
            if(!(data[14]&(1u<<i))&&raw)return false;
        }
    } else if(p.kind==SRNOS_SIRIN_RMT_PLANAR_VELOCITY) {
        float values[3];
        for(unsigned i=0;i<3;i++) {
            uint32_t raw;(void)SRNOS_Endian_LoadLE32(b,14+4*i,&raw);
            memcpy(&values[i],&raw,4);if(!isfinite(values[i]))return false;
        }
        for(unsigned i=26;i<30;i++)if(data[i])return false;
        p.frame.update_planar_velocity=true;
        p.frame.planar_velocity=(SRNOS_InputPlanarVelocity){values[0],values[1],values[2]};
    } else {
        for(unsigned i=14;i<30;i++)if(data[i])return false;
        if(p.kind==SRNOS_SIRIN_RMT_ENABLE)p.frame.request_enable=true;
        else if(p.kind==SRNOS_SIRIN_RMT_DISABLE)p.frame.request_disable=true;
        else p.frame.flags=SRNOS_INPUT_DISABLED;
    }
    *out=p;return true;
}
bool SRNOS_SirinRMTEncode(const SRNOS_SirinRMTPacket *p,uint8_t out[30])
{
    if(!p||!out||!p->session||p->kind>SRNOS_SIRIN_RMT_PLANAR_VELOCITY)return false;
    uint8_t data[30]={0};memcpy(data,"SRMT",4);data[4]=1;data[5]=(uint8_t)p->kind;
    SRNOS_Memory_MutableByteSpan b=SRNOS_Memory_MutableBytes(data,30);
    (void)SRNOS_Endian_StoreLE32(b,6,p->session);(void)SRNOS_Endian_StoreLE32(b,10,p->sequence);
    if(p->kind==SRNOS_SIRIN_RMT_CHANNELS) {
        unsigned first=0,count=0;
        while(first<16&&!(p->frame.channel_mask&(1u<<first)))++first;
        while(first+count<16&&(p->frame.channel_mask&(1u<<(first+count))))++count;
        if(!count||count>7||p->frame.channel_mask!=(uint16_t)(((1u<<count)-1u)<<first))return false;
        data[14]=(uint8_t)first;data[15]=(uint8_t)count;
        for(unsigned i=0;i<count;i++)(void)SRNOS_Endian_StoreLE16(b,16+2*i,(uint16_t)p->frame.raw[first+i]);
    } else if(p->kind==SRNOS_SIRIN_RMT_BUTTONS) {
        (void)SRNOS_Endian_StoreLE64(b,14,p->frame.buttons_valid);
        (void)SRNOS_Endian_StoreLE64(b,22,p->frame.buttons);
    } else if(p->kind==SRNOS_SIRIN_RMT_RELATIVE) {
        data[14]=p->frame.relative_mask;
        for(unsigned i=0;i<3;i++)(void)SRNOS_Endian_StoreLE32(b,18+4*i,(uint32_t)p->frame.relative[i]);
    } else if(p->kind==SRNOS_SIRIN_RMT_PLANAR_VELOCITY) {
        const float values[3]={p->frame.planar_velocity.vx,p->frame.planar_velocity.vy,p->frame.planar_velocity.wz};
        for(unsigned i=0;i<3;i++) {
            uint32_t raw;memcpy(&raw,&values[i],4);(void)SRNOS_Endian_StoreLE32(b,14+4*i,raw);
        }
    }
    SRNOS_SirinRMTPacket check;if(!SRNOS_SirinRMTDecode(data,30,&check))return false;
    memcpy(out,data,30);return true;
}

typedef struct {
    SRNOS_InputSource source;
    SRNOS_MessageOrigin owner;
    uint32_t session,sequence,last_ms,timeout_ms;
    bool seen;
} receiver;
static receiver receivers[4];
static unsigned receiver_count;
void SRNOS_SirinRMTInvalidateVT02(uint16_t endpoint)
{
    for(unsigned i=0;i<receiver_count;i++)
        if(receivers[i].owner.transport==SRNOS_MESSAGE_VT02&&receivers[i].owner.endpoint==endpoint)
            (void)SRNOS_InputInvalidate(receivers[i].source,SRNOS_INPUT_FRAME_LOST);
}
static bool same_peer(const SRNOS_MessageOrigin *a,const SRNOS_MessageOrigin *b)
{
    return a->transport==b->transport&&a->endpoint==b->endpoint&&a->peer_port==b->peer_port&&
        !memcmp(a->peer_address,b->peer_address,4);
}
static SRNOS_MessageResult receive(const SRNOS_MessageInfo *m,const uint8_t *data,void *user)
{
    receiver *r=user;SRNOS_SirinRMTPacket p;
    if(!SRNOS_SirinRMTDecode(data,m->length,&p))return SRNOS_MESSAGE_INVALID;
    uint32_t now=SRNOS_TimeMs();
    if(now-m->received_ms>r->timeout_ms)return SRNOS_MESSAGE_INVALID;
    bool acquire=!r->seen||now-r->last_ms>r->timeout_ms;
    if(!acquire) {
        uint32_t delta=p.sequence-r->sequence;
        if(!same_peer(&r->owner,&m->origin)||p.session!=r->session||!delta||delta>=0x80000000u)
            return SRNOS_MESSAGE_INVALID;
    }
    if(acquire) {
        if(SRNOS_InputInvalidate(r->source,SRNOS_INPUT_DISABLED)!=SRNOS_INPUT_OK)return SRNOS_MESSAGE_CONTEXT;
    }
    if(SRNOS_InputPublish(r->source,&p.frame,m->received_ms)!=SRNOS_INPUT_OK)return SRNOS_MESSAGE_INVALID;
    r->seen=true;r->owner=m->origin;r->session=p.session;r->sequence=p.sequence;r->last_ms=m->received_ms;
    return SRNOS_MESSAGE_OK;
}
SRNOS_InputResult SRNOS_SirinRMTRegister(const char *name,uint8_t transport,uint16_t endpoint,
    uint32_t timeout,SRNOS_InputSource *out)
{
    const bool vt02=transport==SRNOS_MESSAGE_VT02;
    if(!name||!out||strlen(name)>=32||!transport||(!vt02&&transport>8)||(transport&(transport-1u))||
       (vt02&&!endpoint)||
       receiver_count==4)return SRNOS_INPUT_INVALID;
    SRNOS_InputDescriptor d={.timeout_ms=timeout,.channel_count=16,.relative_mask=7,.buttons=UINT64_MAX,.planar_velocity=true};
    memcpy(d.name,name,strlen(name)+1);
    for(unsigned i=0;i<16;i++)d.channels[i]=(SRNOS_InputChannelDescriptor){-10000,0,10000,SRNOS_INPUT_CHANNEL_AXIS,false};
    receiver *r=&receivers[receiver_count];
    SRNOS_InputResult result=SRNOS_InputRegister(&d,&r->source);
    if(result!=SRNOS_INPUT_OK)return result;
    SRNOS_MessageRouteConfig route={.transports=transport,.endpoint=endpoint,
        .service=vt02?SRNOS_VT02_CUSTOM_SERVICE:SRNOS_SIRIN_RMT_SERVICE,
        .type=vt02?SRNOS_VT02_CUSTOM_MESSAGE:SRNOS_SIRIN_RMT_MESSAGE,
        .max_payload=30,.max_age_ms=(uint16_t)timeout,.handler=receive,.user=r};
    SRNOS_MessageRoute handle;
    if(SRNOS_MessageRegister(&route,&handle)!=SRNOS_MESSAGE_OK)return SRNOS_INPUT_FULL;
    r->owner.transport=transport;r->owner.endpoint=endpoint;
    r->timeout_ms=timeout;*out=r->source;++receiver_count;return SRNOS_INPUT_OK;
}
