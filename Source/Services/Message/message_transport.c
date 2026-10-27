#include "message_internal.h"
#include "SirinEmbedOS/Services/SirinRMT.h"
#include "SirinEmbedOS/Services/Network.h"
#include "SirinEmbedOS/Services/VideoLink.h"
#include "SirinEmbedOS/Services/SirinLink/Codec/COBS.h"
#include "../Network/network_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

typedef struct {
    uint8_t encoded[SRNOS_PROTOCOL_MAX_COBS_LEN],raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    size_t length;
    uint32_t last_byte_ms;
    bool discard;
    SRNOS_SerialStats stats;
} uart_state;
static SRNOS_MessageTransportConfig config;
static SRNOS_SerialChannel *uart;
static uart_state *rx;
static bool prepared,started;
bool SRNOS_MessageTransportPrepare(const SRNOS_MessageTransportConfig *c)
{
    if(!c||prepared||xPortIsInsideInterrupt()||xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)return false;
    if(c->uart_port) {
        uart=SRNOS_SerialGetChannel(c->uart_port);
        const SRNOS_SerialConfig *s=SRNOS_SerialGetConfig(uart);
        if(!s||!s->rx_enabled||!s->tx_enabled||s->mode!=SRNOS_SERIAL_DUPLEX||
           s->data_bits!=8||s->stop_bits!=1||s->parity!=SRNOS_SERIAL_PARITY_NONE||
           SRNOS_SerialClaim(uart,&config)!=SRNOS_SERIAL_OK)return false;
    }
    if(c->udp_port&&!SRNOS_NetworkBindMessages(c->udp_port))return false;
    config=*c;prepared=true;return true;
}
uint32_t SRNOS_MessageTransportHeapBudget(void) {return uart?sizeof(uart_state)+32u:0;}
bool SRNOS_MessageTransportStart(void)
{
    if(!prepared||started)return false;
    if(uart){rx=pvPortMalloc(sizeof(*rx));if(!rx)return false;memset(rx,0,sizeof(*rx));}
    started=true;return true;
}
bool SRNOS_MessageHasUART(void) {return uart!=NULL;}
SRNOS_MessageResult SRNOS_MessageReceiveWire(const SRNOS_MessageOrigin *origin,const uint8_t *raw,size_t n,uint32_t now)
{
    SRNOS_ProtocolView view;
    if(!origin||SRNOS_ProtocolParse(raw,n,&view)!=SRNOS_PROTOCOL_OK||
       view.header.kind!=SRNOS_PROTOCOL_KIND_EVENT||view.header.flags)return SRNOS_MESSAGE_INVALID;
    SRNOS_MessageInfo info={.origin=*origin,.sequence=view.header.sequence,.received_ms=now,
        .length=(uint16_t)view.payload_len,.service=view.header.service,.type=view.header.message};
    return SRNOS_MessageDispatch(&info,view.payload);
}
SRNOS_MessageResult SRNOS_MessageReceiveVideo(const uint8_t *data,size_t n,uint32_t now)
{
    if(!data||n!=30)return SRNOS_MESSAGE_INVALID;
    SRNOS_MessageInfo m={.origin={.transport=SRNOS_MESSAGE_VIDEO},.received_ms=now};
    if(!memcmp(data,"SRMT",4)) {
        m.service=SRNOS_SIRIN_RMT_SERVICE;m.type=SRNOS_SIRIN_RMT_MESSAGE;m.length=30;
        (void)SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(data,n),10,&m.sequence);
        return SRNOS_MessageDispatch(&m,data);
    }
    /* Compact envelope: SM,version,service,type,length,seq32,payload[20]. */
    if(data[0]!='S'||data[1]!='M'||data[2]!=1||data[5]>20)return SRNOS_MESSAGE_INVALID;
    for(unsigned i=10+data[5];i<30;i++)if(data[i])return SRNOS_MESSAGE_INVALID;
    m.service=data[3];m.type=data[4];m.length=data[5];
    (void)SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(data,n),6,&m.sequence);
    return SRNOS_MessageDispatch(&m,data+10);
}
void SRNOS_MessagePollUART(void)
{
    if(!uart||!rx)return;
    SRNOS_SerialStats stats;
    if(SRNOS_SerialGetStats(uart,&stats)!=SRNOS_SERIAL_OK)return;
    if(stats.rx_overflow!=rx->stats.rx_overflow||stats.hardware_errors!=rx->stats.hardware_errors||
       stats.recoveries!=rx->stats.recoveries) {
        rx->length=0;rx->discard=true;(void)SRNOS_SerialDiscardRx(uart);
    }
    rx->stats=stats;
    uint32_t now=SRNOS_TimeMs();
    if(rx->length&&now-rx->last_byte_ms>100){rx->length=0;rx->discard=true;}
    SRNOS_MessageOrigin origin={.transport=SRNOS_MESSAGE_UART,.endpoint=(uint16_t)config.uart_port};
    uint8_t bytes[128];
    for(unsigned batch=0;batch<8;batch++) {
        size_t n=0;if(SRNOS_SerialRead(uart,bytes,sizeof(bytes),&n,0)!=SRNOS_SERIAL_OK||!n)break;
        if(SRNOS_SerialGetStats(uart,&stats)!=SRNOS_SERIAL_OK||
           stats.rx_overflow!=rx->stats.rx_overflow||stats.hardware_errors!=rx->stats.hardware_errors||
           stats.recoveries!=rx->stats.recoveries) {
            rx->stats=stats;rx->length=0;rx->discard=true;(void)SRNOS_SerialDiscardRx(uart);return;
        }
        rx->stats=stats;rx->last_byte_ms=now;
        for(size_t i=0;i<n;i++) {
            if(!bytes[i]) {
                if(!rx->discard&&rx->length) {
                    size_t decoded=0;
                    if(SRNOS_COBSDecode(rx->encoded,rx->length,rx->raw,sizeof(rx->raw),&decoded)==SRNOS_COBS_OK&&decoded)(void)SRNOS_MessageReceiveWire(&origin,rx->raw,decoded,now);
                }
                rx->length=0;rx->discard=false;
            } else if(!rx->discard) {
                if(rx->length==sizeof(rx->encoded)){rx->length=0;rx->discard=true;}
                else rx->encoded[rx->length++]=bytes[i];
            }
        }
    }
}
static uint16_t host_receive(void *user,const SRNOS_ProtocolView *v,uint8_t *response,
    size_t capacity,size_t *length,uint8_t *flags)
{
    (void)user;(void)response;(void)capacity;*length=0;*flags=0;
    if(v->header.flags)return SRNOS_SIRIN_LINK_STATUS_BAD_FLAGS;
    SRNOS_MessageInfo m={.origin={.transport=SRNOS_MESSAGE_HOST},.sequence=v->header.sequence,
        .received_ms=SRNOS_TimeMs(),.length=(uint16_t)v->payload_len,.service=v->header.service,.type=v->header.message};
    SRNOS_MessageResult r=SRNOS_MessageDispatch(&m,v->payload);
    return r==SRNOS_MESSAGE_OK?SRNOS_SIRIN_LINK_STATUS_OK:
        r==SRNOS_MESSAGE_FULL?SRNOS_SIRIN_LINK_STATUS_BUSY:SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT;
}
SRNOS_SirinLinkResult SRNOS_MessageRegisterHost(SRNOS_SirinLinkRegistration *link)
{
    if(!config.host_enabled)return SRNOS_SIRIN_LINK_OK;
    for(unsigned i=0;;i++) {
        const SRNOS_MessageRouteConfig *r=SRNOS_MessageRouteAt(i);if(!r)break;
        if(!(r->transports&SRNOS_MESSAGE_HOST))continue;
        bool duplicate=false;
        for(unsigned n=0;n<i;n++) {
            const SRNOS_MessageRouteConfig *p=SRNOS_MessageRouteAt(n);
            if((p->transports&SRNOS_MESSAGE_HOST)&&p->service==r->service&&p->type==r->type)duplicate=true;
        }
        if(duplicate)continue;
        SRNOS_SirinLinkResult result=SRNOS_SirinLinkRegisterHandler(link,r->service,r->type,
            SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_EVENT),SRNOS_SIRIN_LINK_QOS_RELIABLE,host_receive,NULL);
        if(result!=SRNOS_SIRIN_LINK_OK)return result;
        result=SRNOS_SirinLinkRegisterTxTopic(link,r->service,r->type,SRNOS_SIRIN_LINK_QOS_RELIABLE);
        if(result!=SRNOS_SIRIN_LINK_OK)return result;
    }
    return SRNOS_SIRIN_LINK_OK;
}
SRNOS_MessageResult SRNOS_MessageSend(const SRNOS_MessageOrigin *to,uint8_t service,uint8_t type,
    uint32_t sequence,const void *data,uint16_t length)
{
    if(xPortIsInsideInterrupt()||xTaskGetSchedulerState()!=taskSCHEDULER_RUNNING||!started)return SRNOS_MESSAGE_CONTEXT;
    if(!to||(!data&&length)||length>SRNOS_MESSAGE_PAYLOAD_MAX)return SRNOS_MESSAGE_INVALID;
    if(to->transport==SRNOS_MESSAGE_HOST) {
        if(!config.host_enabled)return SRNOS_MESSAGE_INVALID;
        return SRNOS_SirinLinkPublishEvent(service,type,0,sequence,data,length)==SRNOS_SIRIN_LINK_OK?SRNOS_MESSAGE_OK:SRNOS_MESSAGE_FULL;
    }
    if(to->transport==SRNOS_MESSAGE_UART) {
        if(!uart||to->endpoint!=config.uart_port)return SRNOS_MESSAGE_INVALID;
        uint8_t frame[SRNOS_PROTOCOL_MAX_FRAME_LEN];size_t n=0;
        if(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_EVENT,0,service,type,sequence,data,length,frame,sizeof(frame),&n)!=SRNOS_PROTOCOL_OK)
            return SRNOS_MESSAGE_INVALID;
        return SRNOS_SerialWrite(uart,frame,n,0)==SRNOS_SERIAL_OK?SRNOS_MESSAGE_OK:SRNOS_MESSAGE_FULL;
    }
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];size_t n=0;
    if(SRNOS_ProtocolPackRaw(SRNOS_PROTOCOL_KIND_EVENT,0,service,type,sequence,data,length,raw,sizeof(raw),&n)!=SRNOS_PROTOCOL_OK)
        return SRNOS_MESSAGE_INVALID;
    if(to->transport==SRNOS_MESSAGE_UDP) {
        if(!config.udp_port||to->endpoint!=config.udp_port)return SRNOS_MESSAGE_INVALID;
        return SRNOS_UDPSend(config.udp_port,to->peer_address,to->peer_port,raw,(uint16_t)n)==SRNOS_NETWORK_OK?
            SRNOS_MESSAGE_OK:SRNOS_MESSAGE_IO;
    }
    if(to->transport==SRNOS_MESSAGE_VIDEO) {
        if(n>300)return SRNOS_MESSAGE_INVALID;
        memset(raw+n,0,300-n);
        return SRNOS_VideoLinkSend(raw)==SRNOS_VIDEO_LINK_OK?SRNOS_MESSAGE_OK:SRNOS_MESSAGE_FULL;
    }
    return SRNOS_MESSAGE_INVALID;
}
