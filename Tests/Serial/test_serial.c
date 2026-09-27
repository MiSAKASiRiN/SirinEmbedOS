#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Referee/referee.h"
#include "SirinEmbedOS/Services/RemoteControl/DR16/bsp_dr16.h"
#include "SirinEmbedOS/Services/RemoteControl/VT03/bsp_vt03.h"
#include "SirinEmbedOS/Devices/H12H/lib_serial_servo_h12h.h"
/* Exercise the real state machine, replacing only RTOS and hardware edges. */
#include "../../../Source/IO/Serial/serial.c"
typedef struct {uint32_t capacity,size,count,head,tail;uint8_t data[8][sizeof(packet_t)];} fake_queue;
static uint32_t clock_ms;
static osKernelState_t kernel=osKernelReady;
static unsigned tx_calls,tx_pending,complete_at;
static uint8_t sent[512],reply[16];static size_t sent_size,reply_size;
static bool generate_tc=true,inject_response=false;
static bool try_intruder;static unsigned intruder_attempts;static int current_task=1;
static SRNOS_SerialChannel *transaction_channel;
typedef struct {int owner;unsigned depth;} fake_mutex;
osKernelState_t osKernelGetState(void){return kernel;}
uint32_t osKernelGetTickCount(void){return clock_ms;}uint32_t osKernelGetTickFreq(void){return 1000;}
osMutexId_t osMutexNew(const osMutexAttr_t *a){assert(a->attr_bits&osMutexRecursive);return calloc(1,sizeof(fake_mutex));}
osStatus_t osMutexAcquire(osMutexId_t id,uint32_t t){(void)t;fake_mutex *m=id;if(!m||(m->owner&&m->owner!=current_task))return osError;m->owner=current_task;m->depth++;return osOK;}
osStatus_t osMutexRelease(osMutexId_t id){fake_mutex *m=id;assert(m&&m->owner==current_task&&m->depth);if(!--m->depth)m->owner=0;return osOK;}
osMessageQueueId_t osMessageQueueNew(uint32_t n,uint32_t z,const void *a){(void)a;fake_queue *q=calloc(1,sizeof(*q));q->capacity=n;q->size=z;return q;}
osStatus_t osMessageQueuePut(osMessageQueueId_t id,const void *p,uint8_t pri,uint32_t t){(void)pri;(void)t;fake_queue *q=id;if(q->count==q->capacity)return osError;memcpy(q->data[q->head],p,q->size);q->head=(q->head+1)%q->capacity;q->count++;return osOK;}
osStatus_t osMessageQueueGet(osMessageQueueId_t id,void *p,uint8_t *pri,uint32_t t){(void)pri;(void)t;fake_queue *q=id;if(!q->count)return osError;memcpy(p,q->data[q->tail],q->size);q->tail=(q->tail+1)%q->capacity;q->count--;return osOK;}
osThreadId_t osThreadNew(void (*f)(void*),void *p,const osThreadAttr_t *a){(void)f;(void)p;(void)a;return (void*)1;}
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f){(void)t;return f;}uint32_t osThreadFlagsWait(uint32_t f,uint32_t m,uint32_t t){(void)f;(void)m;(void)t;return 0;}
int SRNOS_SerialBoardIsInISR(void){return 0;}
int SRNOS_SerialBoardStart(const SRNOS_SerialConfig *c){(void)c;return 0;}
int SRNOS_SerialBoardTransmit(SRNOS_SerialPortId p,const uint8_t *d,size_t n){memcpy(sent,d,n);sent_size=n;tx_calls++;tx_pending=p;complete_at=clock_ms+3;return 0;}
void SRNOS_SerialBoardAbortTx(SRNOS_SerialPortId p){(void)p;tx_pending=0;}
int SRNOS_SerialBoardRecover(SRNOS_SerialPortId p){(void)p;return 0;}
osStatus_t osDelay(uint32_t n){
 for(uint32_t i=0;i<n;i++){clock_ms++;serial_service_step();
  if(try_intruder){current_task=2;uint8_t bad=0xef;
   assert(SRNOS_SerialWrite(transaction_channel,&bad,1,0)==SRNOS_SERIAL_TIMEOUT);
   intruder_attempts++;current_task=1;
  }
  if(tx_pending&&generate_tc&&clock_ms>=complete_at){unsigned p=tx_pending;tx_pending=0;SRNOS_SerialOnTxComplete(p);
   if(inject_response)SRNOS_SerialOnRx(p,reply,reply_size);}}
 return osOK;
}
static int match(const uint8_t *data,size_t n,void *ctx){(void)ctx;return n>=3&&data[0]==0xaa&&data[2]==0xcc?3:0;}
bool RefereeTX_BindChannel(SRNOS_SerialChannel *channel){return channel!=NULL;}
bool RefereeTX_Send(const uint8_t *p,uint16_t n,TickType_t timeout){(void)p;(void)n;(void)timeout;return true;}
static uint16_t test_crc(const uint8_t *p,size_t n,unsigned bits,uint16_t poly,uint16_t init){uint16_t v=init;while(n--){v^=*p++;for(unsigned i=0;i<8;i++)v=(v&1)?(v>>1)^poly:v>>1;}return v&((1u<<bits)-1);}
static void test_referee(void){
 uint8_t wire[22]={0xa5,13,0,7,0,1,2};wire[4]=(uint8_t)test_crc(wire,4,8,0x8c,0xff);
 wire[7]=3;wire[8]=2;wire[9]=100;wire[11]=200;wire[19]=7;
 uint16_t crc=test_crc(wire,20,16,0x8408,0xffff);wire[20]=crc;wire[21]=crc>>8;
 clock_ms=100;Referee_Feed(wire,8,100);Referee_Feed(wire+8,14,100);
 referee_snapshot_t snap;Referee_GetSnapshot(&snap);assert(snap.robot_state.robot_id==3&&snap.robot_state.current_HP==100);
 wire[7]=4;Referee_Feed(wire,22,100);Referee_GetSnapshot(&snap);assert(snap.robot_state.robot_id==3); /* bad CRC ignored */
 crc=test_crc(wire,20,16,0x8408,0xffff);wire[20]=crc;wire[21]=crc>>8;Referee_Feed(wire,22,100);
 Referee_GetSnapshot(&snap);assert(snap.robot_state.robot_id==4);puts("Referee fragmented frames, CRC rejection and recovery: PASS");
}
static void set_bits(uint8_t *p,unsigned offset,unsigned bits,unsigned value){for(unsigned i=0;i<bits;i++)if(value&(1u<<i))p[(offset+i)/8]|=1u<<((offset+i)%8);}
static void test_codecs(void){
 uint8_t frame[18]={0};for(unsigned i=0;i<4;i++)set_bits(frame,11*i,11,1024);
 set_bits(frame,44,2,1);set_bits(frame,46,2,3);set_bits(frame,128,11,1024);
 assert(DR16_FrameSanity(frame));dr16_dec_t d;DR16_Decode18(frame,&d);assert(d.ch[0]==1024&&d.ch[4]==1024&&d.s1==1&&d.s2==3);
 dr16_framer_t framer={0};dr16_raw_t out[2];uint32_t drops=0;
 assert(DR16_FramerFeed(&framer,frame,7,out,2,&drops)==0);
 assert(DR16_FramerFeed(&framer,frame+7,11,out,2,&drops)==1);
 uint8_t noise[18]={0};assert(DR16_FramerFeed(&framer,noise,18,out,2,&drops)==0);
 assert(DR16_FramerFeed(&framer,frame,18,out,2,&drops)==1);assert(drops>0);
 uint8_t v[21]={0xa9,0x53};for(unsigned i=0;i<4;i++)set_bits(v,16+11*i,11,1024);
 set_bits(v,60,2,1);set_bits(v,65,11,1024);set_bits(v,80,16,37);
 uint16_t crc=VT03_CalcCrc(v,19);v[19]=crc;v[20]=crc>>8;assert(VT03_FrameSanity(v));
 vt03_dec_t decoded;assert(VT03_Decode21(v,&decoded)&&decoded.mx==37);
 vt03_framer_t vf={0};vt03_raw_t vo[2];assert(VT03_FramerFeed(&vf,v,5,vo,2,&drops)==0);
 assert(VT03_FramerFeed(&vf,v+5,16,vo,2,&drops)==1);
 v[20]^=1;assert(!VT03_FrameSanity(v));assert(VT03_FramerFeed(&vf,v,21,vo,2,&drops)==0);
 v[20]^=1;assert(VT03_FramerFeed(&vf,v,21,vo,2,&drops)==1);
 puts("DR16/VT03 framing, corruption recovery and field decoding: PASS");
}
extern SRNOS_SystemResult SRNOS_SystemFreeze(const SRNOS_SystemConfig *);
static void dummy_task(void *p){(void)p;}
static void test_system_config(void){
 SRNOS_SystemConfig config;SRNOS_SystemConfigDefaults(&config);
 assert(config.serial_count==0&&SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_OK);
 assert(SRNOS_SystemGetConfig()==NULL&&!SRNOS_SystemConfigurationFrozen());
 config.remote.dr16_port=SRNOS_SERIAL_USART6;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.serial[0]=(SRNOS_SerialConfig){SRNOS_SERIAL_USART6,100000,8,2,SRNOS_SERIAL_PARITY_EVEN,SRNOS_SERIAL_DUPLEX,true,true,true};
 config.serial_count=1;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_OK);
 config.serial[0].baud_rate=115200;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);config.serial[0].baud_rate=100000;
 config.remote.idle_ms=0;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.remote.idle_ms=config.remote.offline_ms;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);config.remote.idle_ms=50;
 config.referee_port=SRNOS_SERIAL_USART6;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_CONFLICT);
 config.referee_port=SRNOS_SERIAL_NONE;config.threads[0]=(SRNOS_ThreadConfig){"Controller",dummy_task,NULL,2048,24};config.thread_count=1;
 assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_OK);
 config.threads[0].stack_bytes=1025;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.threads[0].stack_bytes=2048;config.threads[0].priority=40;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.threads[0].priority=24;config.thread_count=SRNOS_SYSTEM_MAX_THREADS+1;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.thread_count=1;config.usb_enabled=false;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);config.usb_enabled=true;
 SRNOS_SystemConfig oversized=config;oversized.thread_count=SRNOS_SYSTEM_MAX_THREADS;
 for(unsigned i=0;i<SRNOS_SYSTEM_MAX_THREADS;i++)oversized.threads[i]=(SRNOS_ThreadConfig){"BudgetTest",dummy_task,NULL,32768,24};
 assert(SRNOS_SystemHeapBudget(&oversized)>SRNOS_SystemHeapBudget(&config));
 assert(SRNOS_SystemValidate(&oversized)==SRNOS_SYSTEM_RESOURCE);
 config.sensors.lsm_enabled=true;config.sensors.lis_enabled=true;config.sensors.spa_enabled=true;
 uint32_t full_budget=SRNOS_SystemHeapBudget(&config);
 config.sensors.lsm_enabled=false;assert(full_budget-SRNOS_SystemHeapBudget(&config)==2560);config.sensors.lsm_enabled=true;
 assert(config.ins.enabled && config.sensors.heater_enabled && config.sensors.heater_target_c==60);
 assert(!config.load_parameters && !config.host_link_storage_enabled);
 config.sensors.heater_target_c=75; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_OK);
 config.sensors.heater_target_c=75.01f; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.sensors.heater_target_c=NAN; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.sensors.heater_target_c=19.9f; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.sensors.heater_target_c=60; config.sensors.bmi_period_ms=2; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.sensors.bmi_period_ms=1;
 config.ins.board_to_robot_wxyz[0]=0; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.ins.board_to_robot_wxyz[0]=INFINITY; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);
 config.ins.board_to_robot_wxyz[0]=2; assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_OK);
 config.sensors.lsm_period_ms=0;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);config.sensors.lsm_period_ms=1;
 config.sensors.environment_period_ms=1;assert(SRNOS_SystemValidate(&config)==SRNOS_SYSTEM_INVALID);config.sensors.environment_period_ms=10;
 assert(SRNOS_SystemFreeze(&config)==SRNOS_SYSTEM_OK);config.serial[0].baud_rate=12345;config.threads[0].stack_bytes=4096;
 const SRNOS_SystemConfig *saved=SRNOS_SystemGetConfig();assert(saved&&saved->serial[0].baud_rate==100000&&saved->threads[0].stack_bytes==2048);
 assert(SRNOS_SystemGetConfig()->ins.board_to_robot_wxyz[0]==1 && SRNOS_SystemGetConfig()->sensors.lsm_enabled);
 assert(SRNOS_SystemFreeze(&config)==SRNOS_SYSTEM_FROZEN&&SRNOS_SystemConfigurationFrozen());
 puts("System defaults, role conflicts, missing ports, thread budgets and freeze isolation: PASS");
}
int main(void){
 test_system_config();test_codecs();
 SRNOS_SerialConfig config={SRNOS_SERIAL_USART6,100000,8,2,SRNOS_SERIAL_PARITY_EVEN,SRNOS_SERIAL_DUPLEX,true,true,true};
 assert(SRNOS_SerialValidate(NULL,0)==0);
 SRNOS_SerialConfig duplicate[2]={config,config};assert(SRNOS_SerialValidate(duplicate,2)==SRNOS_SERIAL_CONFLICT);
 config.port=SRNOS_SERIAL_UART7;assert(SRNOS_SerialValidate(&config,1)==SRNOS_SERIAL_ARGUMENT);
 config.port=SRNOS_SERIAL_USART6;
 SRNOS_SerialConfig all[2]={config,{SRNOS_SERIAL_UART8,115200,8,1,SRNOS_SERIAL_PARITY_NONE,SRNOS_SERIAL_DUPLEX,true,true,false}};
 assert(SRNOS_SerialConfigure(all,2)==0);assert(SRNOS_SerialConfigure(&config,1)==SRNOS_SERIAL_FROZEN);
 SRNOS_SerialChannel *c=SRNOS_SerialGetChannel(config.port);assert(c);
 assert(SRNOS_SerialStart()==SRNOS_SERIAL_STATE); /* unclaimed ports cannot start */
 assert(SRNOS_SerialClaim(c,&config)==0);assert(SRNOS_SerialGetChannel(config.port)==NULL);assert(SRNOS_SerialClaim(c,&duplicate)==SRNOS_SERIAL_CONFLICT);
 assert(Referee_BindChannel(SRNOS_SerialGetChannel(SRNOS_SERIAL_UART8))&&Referee_Init());
 assert(SRNOS_SerialStart()==0);kernel=osKernelRunning;test_referee();
 assert(SRNOS_SerialClaim(c,&config)==SRNOS_SERIAL_FROZEN);
 size_t cursor=0;assert(SRNOS_SerialDmaDelta(&cursor,128,256)==128);
 assert(SRNOS_SerialDmaDelta(&cursor,256,256)==128);
 assert(SRNOS_SerialDmaDelta(&cursor,256,256)==0); /* TC followed by duplicate IDLE */
 assert(SRNOS_SerialDmaDelta(&cursor,5,256)==5);
 SRNOS_SerialDmaCursor dma={0};
 assert(SRNOS_SerialDmaAdvance(&dma,0,256,true)==256); /* complete ring without IDLE */
 assert(SRNOS_SerialDmaAdvance(&dma,200,256,false)==200);
 assert(SRNOS_SerialDmaAdvance(&dma,10,256,false)==66); /* IDLE after wrap, before TC IRQ */
 assert(SRNOS_SerialDmaAdvance(&dma,10,256,true)==0); /* delayed TC cannot duplicate */
 assert(SRNOS_SerialDmaAdvance(&dma,10,256,false)==0);
 uint8_t bytes[1100];memset(bytes,0x52,sizeof(bytes));SRNOS_SerialOnRx(config.port,bytes,sizeof(bytes));
 size_t got=0;assert(SRNOS_SerialRead(c,bytes,4,&got,0)==0&&got==4);
 SRNOS_SerialStats stats;assert(SRNOS_SerialGetStats(c,&stats)==0&&stats.rx_overflow==76);
 assert(SRNOS_SerialDiscardRx(c)==0);
 uint8_t msg[]={1,2,3};assert(SRNOS_SerialWrite(c,msg,3,0)==0);msg[0]=9;
 serial_service_step();assert(sent_size==3&&sent[0]==1); /* no caller buffer retained */
 generate_tc=false;assert(SRNOS_SerialFlush(c,2)==SRNOS_SERIAL_TIMEOUT);assert(c->active);
 generate_tc=true;assert(SRNOS_SerialFlush(c,20)==0); /* only TC completes */
 reply[0]=0xaa;reply[1]=0xbb;reply[2]=0xcc;reply_size=3;inject_response=true;
 uint8_t response[16];SRNOS_SerialTransaction t={msg,3,response,sizeof(response),30,10,3500,match,NULL};
 transaction_channel=c;try_intruder=true;
 assert(SRNOS_SerialTransceive(c,&t,&got)==0&&got==3&&response[2]==0xcc);
 try_intruder=false;assert(intruder_attempts>0); /* another task cannot insert TX during response wait */
 inject_response=false;assert(SRNOS_SerialTransceive(c,&t,&got)==SRNOS_SERIAL_TIMEOUT);
 t.rx_capacity=0;assert(SRNOS_SerialTransceive(c,&t,&got)==0&&got==0); /* broadcast */
 SRNOS_SerialOnError(config.port);serial_service_step();assert(c->stats.recoveries==1);
 assert(tx_calls==4);
 serial_servo_h12h_t servo={c,30};int16_t position=0;inject_response=true;
 uint8_t response_frame[]={0x55,0x55,7,5,28,0x34,0x02,0};
 uint8_t sum=0;for(unsigned i=2;i<7;i++)sum+=response_frame[i];response_frame[7]=(uint8_t)~sum;
 memcpy(reply,response_frame,sizeof(response_frame));reply_size=sizeof(response_frame);
 assert(serial_servo_h12h_read_position(&servo,7,&position)==0&&position==0x234);
 reply[2]=8;assert(serial_servo_h12h_read_position(&servo,7,&position)==SERIAL_SERVO_H12H_ERR_ID);
 puts("H12H matching, payload decode and wrong-device rejection: PASS");
 generate_tc=false;inject_response=false;t.queue_timeout_ms=1;t.rx_capacity=0;t.silence_us=0;
 assert(SRNOS_SerialTransceive(c,&t,&got)==SRNOS_SERIAL_TIMEOUT&&c->cancel_tx);
 assert(SRNOS_SerialWrite(c,msg,3,0)==SRNOS_SERIAL_STATE); /* no successor before abort/drain */
 serial_service_step();assert(!c->cancel_tx&&!c->active&&c->stats.tx_completed==c->stats.tx_accepted);
 puts("Serial configuration, DMA cursor, overflow, copy, TC and transactions: PASS");
 return 0;
}
