#include "thread_internal.h"
#include "rs485_internal.h"
#include "SirinEmbedOS/IO/RS485/RS485.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "cmsis_os2.h"
#include <cstring>
static void router_worker(void *argument);
namespace {
constexpr size_t max_endpoints=16,max_adu=256;
constexpr uint32_t queue_depth=8,worker_stack_bytes=3072;
struct endpoint {bool used;uint8_t unit;SRNOS_RS485RouterProto proto;void *owner,*user;SRNOS_RS485RouterRawMatchFn match;SRNOS_RS485RouterOnRxFn receive;};
struct request {SRNOS_RS485RouterHandle handle;uint16_t size;uint8_t bytes[max_adu];};
endpoint endpoints[max_endpoints];
SRNOS_SerialChannel *channel;
osMessageQueueId_t queue;
bool frozen=false;
uint32_t response_timeout_ms=100;
SRNOS_RS485RouterStats statistics{};
void increment(uint32_t SRNOS_RS485RouterStats::*field)
{
    taskENTER_CRITICAL();
    ++(statistics.*field);
    taskEXIT_CRITICAL();
}
uint32_t now_ms(){return uint32_t(uint64_t(osKernelGetTickCount())*1000/osKernelGetTickFreq());}
uint32_t ticks(uint32_t ms){return ms==UINT32_MAX?osWaitForever:uint32_t((uint64_t(ms)*osKernelGetTickFreq()+999)/1000);}
uint16_t crc(const uint8_t *p,size_t n){uint16_t v=0xffff;while(n--){v^=*p++;for(unsigned i=0;i<8;i++)v=(v&1)?(v>>1)^0xa001:v>>1;}return v;}
endpoint *lookup(SRNOS_RS485RouterHandle h){return h>0&&h<=max_endpoints&&endpoints[h-1].used?&endpoints[h-1]:nullptr;}
int register_endpoint(uint8_t port,uint8_t unit,SRNOS_RS485RouterProto proto,void *owner,
 SRNOS_RS485RouterRawMatchFn match,SRNOS_RS485RouterOnRxFn receive,void *user,SRNOS_RS485RouterHandle *out){
 if(frozen)return SRNOS_RS485_ROUTER_ERR_STATE;
 if(port!=SRNOS_RS485_ROUTER_PORT_UART7||!owner||!out)return SRNOS_RS485_ROUTER_ERR_PARAM;
 if(!channel)channel=SRNOS_SerialGetChannel(SRNOS_SERIAL_UART7);
 if(!channel||SRNOS_SerialClaim(channel,&endpoints)!=SRNOS_SERIAL_OK)return SRNOS_RS485_ROUTER_ERR_STATE;
 for(auto &e:endpoints)if(e.used&&(e.owner==owner||(proto==SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU&&e.proto==proto&&e.unit==unit)))return SRNOS_RS485_ROUTER_ERR_DUP;
 for(size_t i=0;i<max_endpoints;i++)if(!endpoints[i].used){endpoints[i]={true,unit,proto,owner,user,match,receive};*out=i+1;return 0;}
 return SRNOS_RS485_ROUTER_ERR_FULL;
}
struct match_context {uint8_t unit,fc;};
int SRNOS_ModbusMatch(const uint8_t *data,size_t n,void *context){
 const auto *m=static_cast<match_context*>(context);if(n<4)return 0;
 if(data[0]!=m->unit||(data[1]&0x7fu)!=m->fc)return -1;
 size_t expected=0;
 if(data[1]&0x80)expected=5;
 else if(data[1]>=1&&data[1]<=4)expected=size_t(data[2])+5;
 else if(data[1]==5||data[1]==6||data[1]==15||data[1]==16)expected=8;
 else {return crc(data,n)==0?int(n):0;}
 if(n<expected)return 0;
 return crc(data,expected)==0?int(expected):-1;
}
int send(SRNOS_RS485RouterHandle h,const void *data,uint16_t n,uint32_t timeout,SRNOS_RS485RouterProto proto){
 if(!SRNOS_KernelTaskContext())return SRNOS_RS485_ROUTER_ERR_STATE;
 auto *e=lookup(h);if(!e||!queue||!data||!n||n>max_adu)return SRNOS_RS485_ROUTER_ERR_PARAM;
 if(e->proto!=proto)return SRNOS_RS485_ROUTER_ERR_MODE;
 const auto *bytes=static_cast<const uint8_t*>(data);
 if(proto==SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU&&(n<4||crc(bytes,n)!=0))return SRNOS_RS485_ROUTER_ERR_CRC;
 if(proto==SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU&&bytes[0]!=0&&bytes[0]!=e->unit)return SRNOS_RS485_ROUTER_ERR_PARAM;
 request r{};r.handle=h;r.size=n;std::memcpy(r.bytes,data,n);
 if(osMessageQueuePut(queue,&r,0,ticks(timeout))!=osOK){increment(&SRNOS_RS485RouterStats::tx_dropped);return SRNOS_RS485_ROUTER_ERR_FULL;}
 return 0; /* preserves the existing asynchronous enqueue contract */
}
}
extern "C" int SRNOS_RS485RouterRegisterRaw(uint8_t p,void *o,SRNOS_RS485RouterRawMatchFn m,SRNOS_RS485RouterOnRxFn f,void *u,SRNOS_RS485RouterHandle *h){return register_endpoint(p,0,SRNOS_RS485_ROUTER_PROTO_RAW,o,m,f,u,h);}
extern "C" int SRNOS_RS485RouterRegisterModbus(uint8_t p,uint8_t id,void *o,SRNOS_RS485RouterOnRxFn f,void *u,SRNOS_RS485RouterHandle *h){if(id>247)return SRNOS_RS485_ROUTER_ERR_PARAM;return register_endpoint(p,id,SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU,o,nullptr,f,u,h);}
extern "C" int SRNOS_RS485RouterUnregister(SRNOS_RS485RouterHandle h){if(frozen)return SRNOS_RS485_ROUTER_ERR_STATE;auto *e=lookup(h);if(!e)return SRNOS_RS485_ROUTER_ERR_PARAM;*e={};return 0;}
extern "C" int SRNOS_RS485RouterSendRaw(SRNOS_RS485RouterHandle h,const void *p,uint16_t n,uint32_t t){return send(h,p,n,t,SRNOS_RS485_ROUTER_PROTO_RAW);}
extern "C" int SRNOS_RS485RouterSendModbusAdu(SRNOS_RS485RouterHandle h,const void *p,uint16_t n,uint32_t t){return send(h,p,n,t,SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU);}
extern "C" bool SRNOS_RS485RouterIsReady(uint8_t p){return p==1&&queue;}
extern "C" bool SRNOS_RS485RouterGetStats(uint8_t port,SRNOS_RS485RouterStats *out)
{
    if(port!=SRNOS_RS485_ROUTER_PORT_UART7 || !out || !SRNOS_KernelTaskContext())return false;
    taskENTER_CRITICAL();
    *out=statistics;
    taskEXIT_CRITICAL();
    return true;
}
extern "C" uint32_t SRNOS_RS485HeapBudget(bool configured)
{
    if(!configured)return 0;
    const uint32_t resources=worker_stack_bytes+queue_depth*sizeof(request)+
        sizeof(StaticQueue_t)+sizeof(StaticTask_t)+1024u;
    return resources>8192u?resources:8192u;
}
extern "C" bool SRNOS_RS485IsFrozen(void){return frozen;}
extern "C" int SRNOS_RS485Start(uint32_t timeout){
 if(frozen||!timeout||osKernelGetState()!=osKernelReady)return SRNOS_RS485_ROUTER_ERR_STATE;
 frozen=true;response_timeout_ms=timeout;
 if(!channel)return 0;
 queue=osMessageQueueNew(queue_depth,sizeof(request),nullptr);if(!queue)return SRNOS_RS485_ROUTER_ERR_FULL;
 const osThreadAttr_t attr={"SRNOS_RS485",0,nullptr,0,nullptr,worker_stack_bytes,osPriorityHigh7,0,0};
 return SRNOS_KernelThreadNew(router_worker,nullptr,&attr)?0:SRNOS_RS485_ROUTER_ERR_FULL;
}
static void router_worker(void *arg){
 (void)arg;request r{};uint8_t response[max_adu],unsolicited[max_adu];size_t pending=0;
 uint32_t last_rx=0;
 bool have_request=false;
 auto wait=[&]()->bool {have_request=osMessageQueueGet(queue,&r,nullptr,ticks(1))==osOK;return true;};
 auto step=[&](){
  if(!have_request){
   size_t n=0;
   if(SRNOS_SerialRead(channel,unsolicited+pending,sizeof(unsolicited)-pending,&n,0)==SRNOS_SERIAL_OK){pending+=n;last_rx=now_ms();}
   const auto *cfg=SRNOS_SerialGetConfig(channel);
   uint32_t char_bits=1+cfg->data_bits+cfg->stop_bits+(cfg->parity!=SRNOS_SERIAL_PARITY_NONE);
   uint32_t gap=cfg->baud_rate>19200?2:(3500u*char_bits+cfg->baud_rate-1)/cfg->baud_rate;
   if(pending&&(pending==sizeof(unsolicited)||now_ms()-last_rx>=gap)){
    SRNOS_RS485RouterRxFrame frame{1,SRNOS_RS485_ROUTER_PROTO_RAW,unsolicited,uint16_t(pending),last_rx,true,0,0};
    bool delivered=false;
    for(size_t i=0;i<max_endpoints;i++){auto &ep=endpoints[i];
     if(ep.used&&ep.proto==SRNOS_RS485_ROUTER_PROTO_RAW&&ep.receive&&(!ep.match||ep.match(&frame,ep.user))){
      ep.receive(i+1,&frame,ep.user);delivered=true;
     }
    }
    if(delivered)increment(&SRNOS_RS485RouterStats::rx_frames);else increment(&SRNOS_RS485RouterStats::rx_unhandled);
    pending=0;
   }
   return;
  }
  /* A queued transaction owns its response window; never carry unrelated
   * partial bytes from the idle listener into that window. */
  if(pending){increment(&SRNOS_RS485RouterStats::rx_dropped);pending=0;}
  auto *e=lookup(r.handle);if(!e)return;
  const auto *cfg=SRNOS_SerialGetConfig(channel);
  /* Modbus requires 3.5 characters below 19200, fixed >=1750us above it. */
  uint32_t bits=1+cfg->data_bits+cfg->stop_bits+(cfg->parity!=SRNOS_SERIAL_PARITY_NONE);
  uint32_t silence=cfg->baud_rate>19200?1750:(3500000u*bits+cfg->baud_rate-1)/cfg->baud_rate;
  bool modbus=e->proto==SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU;
  match_context match{r.bytes[0],uint8_t(r.bytes[1]&0x7f)};
  bool expect=e->receive&&(!modbus||r.bytes[0]!=0);
  SRNOS_SerialTransaction t{r.bytes,r.size,response,expect?sizeof(response):0,
   1000,response_timeout_ms,silence,modbus?SRNOS_ModbusMatch:nullptr,&match};
  size_t n=0;auto s=SRNOS_SerialTransceive(channel,&t,&n);
  if(s!=SRNOS_SERIAL_OK){increment(&SRNOS_RS485RouterStats::rx_dropped);return;}
  increment(&SRNOS_RS485RouterStats::tx_frames);if(!n)return;
  SRNOS_RS485RouterRxFrame frame{1,e->proto,response,uint16_t(n),now_ms(),!modbus||crc(response,n)==0,
   modbus?response[0]:uint8_t(0),modbus?response[1]:uint8_t(0)};
  if(e->match&&!e->match(&frame,e->user)){increment(&SRNOS_RS485RouterStats::rx_unhandled);return;}
  increment(&SRNOS_RS485RouterStats::rx_frames);e->receive(r.handle,&frame,e->user);

 };
 SRNOS_KernelRunCpp(step,wait);
}
