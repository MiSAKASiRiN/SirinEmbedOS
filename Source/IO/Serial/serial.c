#include "serial_internal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#define RX_SIZE 1024u
#define TX_DEPTH 4u
typedef struct { uint16_t size; uint8_t bytes[SRNOS_SERIAL_MAX_WRITE]; } packet_t;
struct SRNOS_SerialChannel {
 SRNOS_SerialConfig config;
 const void *owner;
 bool configured;
 volatile bool active, tx_done, error, cancel_tx;
 uint32_t tx_started_ms;
 volatile uint32_t last_activity_ms;
 volatile uint32_t rx_head, rx_tail;
 uint8_t rx[RX_SIZE];
 osMutexId_t gate;
 osMessageQueueId_t tx_queue;
 volatile SRNOS_SerialStats stats;
};
static SRNOS_SerialChannel channels[SRNOS_SERIAL_PORT_COUNT];
static bool frozen, started;
static osThreadId_t worker;
static uint32_t ticks(uint32_t ms) {
 if(ms==UINT32_MAX) return osWaitForever;
 return (uint32_t)(((uint64_t)ms*osKernelGetTickFreq()+999u)/1000u);
}
static uint32_t now_ms(void) { return (uint32_t)((uint64_t)osKernelGetTickCount()*1000u/osKernelGetTickFreq()); }
static bool task_context(void) { return !SRNOS_SerialBoardIsInISR() && osKernelGetState()==osKernelRunning; }
static bool valid(const SRNOS_SerialChannel *c) {
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) if(c==&channels[i]) return c->configured;
 return false;
}
SRNOS_SerialStatus SRNOS_SerialValidate(const SRNOS_SerialConfig *cfg,size_t n) {
 if((n&&!cfg)||n>=SRNOS_SERIAL_PORT_COUNT) return SRNOS_SERIAL_ARGUMENT;
 uint32_t used=0;
 for(size_t i=0;i<n;i++) {
  const SRNOS_SerialConfig *c=&cfg[i];
  if(c->port<=SRNOS_SERIAL_NONE||c->port>=SRNOS_SERIAL_PORT_COUNT||!c->baud_rate||c->baud_rate>2000000u||
    (c->data_bits!=7&&c->data_bits!=8)||(c->stop_bits!=1&&c->stop_bits!=2)||
    c->parity>SRNOS_SERIAL_PARITY_ODD||c->parity<0||(!c->rx_enabled&&!c->tx_enabled)||
    c->mode!=(c->port==SRNOS_SERIAL_UART7?SRNOS_SERIAL_RS485:SRNOS_SERIAL_DUPLEX)) return SRNOS_SERIAL_ARGUMENT;
  if(used&(1u<<c->port)) return SRNOS_SERIAL_CONFLICT;
  used|=1u<<c->port;
 }
 return SRNOS_SERIAL_OK;
}
SRNOS_SerialStatus SRNOS_SerialConfigure(const SRNOS_SerialConfig *cfg,size_t n) {
 if(frozen) return SRNOS_SERIAL_FROZEN;
 SRNOS_SerialStatus s=SRNOS_SerialValidate(cfg,n); if(s) return s;
 for(size_t i=0;i<n;i++){channels[cfg[i].port].config=cfg[i];channels[cfg[i].port].configured=true;}
 frozen=true; return SRNOS_SERIAL_OK;
}
static SRNOS_SerialChannel *lookup(SRNOS_SerialPortId p) {
 return p>SRNOS_SERIAL_NONE&&p<SRNOS_SERIAL_PORT_COUNT&&channels[p].configured?&channels[p]:NULL;
}
SRNOS_SerialChannel *SRNOS_SerialGetChannel(SRNOS_SerialPortId p) {
 SRNOS_SerialChannel *c=lookup(p);
 return !started&&c&&!c->owner?c:NULL;
}
const SRNOS_SerialConfig *SRNOS_SerialBoardConfig(SRNOS_SerialPortId p) {
 const SRNOS_SerialChannel *c=lookup(p);return c?&c->config:NULL;
}
const SRNOS_SerialConfig *SRNOS_SerialGetConfig(const SRNOS_SerialChannel *c) { return valid(c)?&c->config:NULL; }
SRNOS_SerialStatus SRNOS_SerialClaim(SRNOS_SerialChannel *c,const void *owner) {
 if(started) return SRNOS_SERIAL_FROZEN;
 if(!valid(c)||!owner) return SRNOS_SERIAL_ARGUMENT;
 if(c->owner&&c->owner!=owner) return SRNOS_SERIAL_CONFLICT;
 c->owner=owner;return SRNOS_SERIAL_OK;
}
size_t SRNOS_SerialDmaDelta(size_t *cursor,size_t pos,size_t size) {
 if(!cursor||!size||pos>size||*cursor>=size) return 0;
 pos%=size; size_t delta=(pos+size-*cursor)%size; *cursor=pos; return delta;
}
size_t SRNOS_SerialDmaAdvance(SRNOS_SerialDmaCursor *c,size_t pos,size_t size,bool complete) {
 if(!c||!size||pos>size||c->position>=size)return 0;
 pos%=size;size_t old=c->position;
 size_t n=SRNOS_SerialDmaDelta(&c->position,pos,size);
 if(pos<old)c->wrap_accounted=true;
 if(complete){if(!c->wrap_accounted)n+=size;c->wrap_accounted=false;}
 return n;
}
void SRNOS_SerialOnRx(SRNOS_SerialPortId p,const uint8_t *data,size_t len) {
 SRNOS_SerialChannel *c=lookup(p); if(!c)return;
 for(size_t i=0;i<len;i++) {
  uint32_t head=__atomic_load_n(&c->rx_head,__ATOMIC_RELAXED);
  uint32_t tail=__atomic_load_n(&c->rx_tail,__ATOMIC_ACQUIRE);
  if(head-tail==RX_SIZE){c->stats.rx_overflow++;continue;}
  c->rx[head%RX_SIZE]=data[i];__atomic_store_n(&c->rx_head,head+1u,__ATOMIC_RELEASE);
  c->stats.rx_bytes++;
 }
 c->last_activity_ms=now_ms();
}
void SRNOS_SerialOnTxComplete(SRNOS_SerialPortId p) {
 SRNOS_SerialChannel *c=lookup(p);if(!c)return;
 c->tx_done=true; c->last_activity_ms=now_ms();
 if(worker)osThreadFlagsSet(worker,1u);
}
void SRNOS_SerialOnError(SRNOS_SerialPortId p) {
 SRNOS_SerialChannel *c=lookup(p);if(!c)return;
 c->stats.hardware_errors++;c->stats.hardware_ready=false;c->stats.last_error=SRNOS_SERIAL_IO;c->error=true;if(worker)osThreadFlagsSet(worker,1u);
}
static void serial_service_step(void) {
 packet_t packet;
  for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
   SRNOS_SerialChannel *c=&channels[i]; if(!c->configured)continue;
   if(c->cancel_tx) {
    if(c->active){SRNOS_SerialBoardAbortTx(c->config.port);c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;}
    c->tx_done=false;
    while(osMessageQueueGet(c->tx_queue,&packet,NULL,0)==osOK){c->stats.tx_errors++;c->stats.tx_completed++;}
    c->cancel_tx=false;
   }
   if(c->error) {
    c->error=false;
    if(c->active){SRNOS_SerialBoardAbortTx(c->config.port);c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;}
    c->tx_done=false;
    if(SRNOS_SerialBoardRecover(c->config.port)){c->error=true;c->stats.hardware_ready=false;c->stats.last_error=SRNOS_SERIAL_IO;}
    else {c->stats.recoveries++;c->stats.hardware_ready=true;c->stats.last_error=SRNOS_SERIAL_OK;}
   }
   if(c->active&&c->tx_done) {c->tx_done=false;c->active=false;c->stats.tx_completed++;}
   if(c->active && now_ms()-c->tx_started_ms > 100u+(SRNOS_SERIAL_MAX_WRITE*12000u/c->config.baud_rate)) {
    SRNOS_SerialBoardAbortTx(c->config.port); c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;
   }
   if(!c->active&&!c->error&&osMessageQueueGet(c->tx_queue,&packet,NULL,0)==osOK) {
    c->tx_done=false;c->active=true;c->tx_started_ms=now_ms();
    if(SRNOS_SerialBoardTransmit(c->config.port,packet.bytes,packet.size)) {
     c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;
    }
   }
  }
}
static void serial_worker(void *arg) {
 (void)arg;
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
  if(channels[i].configured){
   if(SRNOS_SerialBoardStart(&channels[i].config)){channels[i].error=true;channels[i].stats.last_error=SRNOS_SERIAL_IO;}
   else {channels[i].stats.hardware_ready=true;channels[i].stats.last_error=SRNOS_SERIAL_OK;}
  }
 }
 for(;;) {
  serial_service_step();
  osThreadFlagsWait(1u,osFlagsWaitAny,ticks(1));
 }
}
SRNOS_SerialStatus SRNOS_SerialStart(void) {
 if(started)return SRNOS_SERIAL_FROZEN;
 if(!frozen||osKernelGetState()!=osKernelReady)return SRNOS_SERIAL_STATE;
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++)if(channels[i].configured&&!channels[i].owner)return SRNOS_SERIAL_STATE;
 started=true; bool any=false;
 const osMutexAttr_t mutex={.name="Serial",.attr_bits=osMutexRecursive|osMutexPrioInherit};
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
  SRNOS_SerialChannel *c=&channels[i];if(!c->configured)continue;any=true;
  c->gate=osMutexNew(&mutex);c->tx_queue=osMessageQueueNew(TX_DEPTH,sizeof(packet_t),NULL);
  if(!c->gate||!c->tx_queue)return SRNOS_SERIAL_IO;
 }
 if(any){const osThreadAttr_t attr={.name="Serial",.stack_size=2048,.priority=osPriorityAboveNormal};
  worker=osThreadNew(serial_worker,NULL,&attr);if(!worker)return SRNOS_SERIAL_IO;}
 return SRNOS_SERIAL_OK;
}
static SRNOS_SerialStatus lock(SRNOS_SerialChannel *c,uint32_t timeout) {
 if(!valid(c)||!started||!task_context())return SRNOS_SERIAL_STATE;
 return osMutexAcquire(c->gate,ticks(timeout))==osOK?SRNOS_SERIAL_OK:SRNOS_SERIAL_TIMEOUT;
}
static size_t pop(SRNOS_SerialChannel *c,uint8_t *out,size_t n) {
 size_t i=0;uint32_t tail=__atomic_load_n(&c->rx_tail,__ATOMIC_RELAXED);
 uint32_t head=__atomic_load_n(&c->rx_head,__ATOMIC_ACQUIRE);
 while(i<n&&tail!=head){out[i++]=c->rx[tail%RX_SIZE];tail++;}
 __atomic_store_n(&c->rx_tail,tail,__ATOMIC_RELEASE);return i;
}
SRNOS_SerialStatus SRNOS_SerialRead(SRNOS_SerialChannel *c,void *out,size_t cap,size_t *received,uint32_t timeout) {
 if(!received||!out||!cap)return SRNOS_SERIAL_ARGUMENT;*received=0;
 uint32_t start=now_ms(),remaining=timeout;
 for(;;){
  SRNOS_SerialStatus s=lock(c,remaining);if(s)return s;
  if(!c->config.rx_enabled){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
  *received=pop(c,out,cap);
  osMutexRelease(c->gate);
  if(*received)return SRNOS_SERIAL_OK;
  uint32_t elapsed=now_ms()-start;
  if(timeout==0||elapsed>=timeout)return SRNOS_SERIAL_TIMEOUT;
  /* A streaming receiver must not block concurrent full-duplex TX while idle.
   * Transaction callers hold the same gate for the entire response window. */
  osDelay(ticks(1));
  elapsed=now_ms()-start;
  if(timeout!=UINT32_MAX){if(elapsed>=timeout)return SRNOS_SERIAL_TIMEOUT;remaining=timeout-elapsed;}
 }
}
SRNOS_SerialStatus SRNOS_SerialWrite(SRNOS_SerialChannel *c,const void *data,size_t n,uint32_t timeout) {
 if(!data||!n||n>SRNOS_SERIAL_MAX_WRITE)return SRNOS_SERIAL_ARGUMENT;
 SRNOS_SerialStatus s=lock(c,timeout);if(s)return s;
 if(!c->config.tx_enabled||c->cancel_tx){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
 packet_t p={.size=(uint16_t)n};memcpy(p.bytes,data,n);
 /* Worker runs independently; accepted counter may lag completion only while this lock is held. */
 if(osMessageQueuePut(c->tx_queue,&p,0,ticks(timeout))!=osOK)s=SRNOS_SERIAL_TIMEOUT;
 else {c->stats.tx_accepted++;osThreadFlagsSet(worker,1u);}
 osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialFlush(SRNOS_SerialChannel *c,uint32_t timeout) {
 SRNOS_SerialStatus s=lock(c,timeout);if(s)return s;uint32_t start=now_ms();
 while(c->stats.tx_completed!=c->stats.tx_accepted){if(now_ms()-start>=timeout){s=SRNOS_SERIAL_TIMEOUT;break;}osDelay(ticks(1));}
 if(!s&&c->stats.tx_errors)s=SRNOS_SERIAL_IO;
 osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialDiscardRx(SRNOS_SerialChannel *c) {
 SRNOS_SerialStatus s=lock(c,0);if(s)return s;
 __atomic_store_n(&c->rx_tail,__atomic_load_n(&c->rx_head,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
 osMutexRelease(c->gate);return SRNOS_SERIAL_OK;
}
SRNOS_SerialStatus SRNOS_SerialTransceive(SRNOS_SerialChannel *c,const SRNOS_SerialTransaction *t,size_t *received) {
 if(!t||!received||(t->rx_capacity&&!t->rx_data))return SRNOS_SERIAL_ARGUMENT;*received=0;
 SRNOS_SerialStatus s=lock(c,t->queue_timeout_ms);if(s)return s;
 if((t->rx_capacity&&!c->config.rx_enabled)||c->cancel_tx){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
 uint32_t errors=c->stats.tx_errors;
 /* Drain prior queued output before the response window is reserved. */
 s=SRNOS_SerialFlush(c,t->queue_timeout_ms);
 if(s==SRNOS_SERIAL_IO&&c->stats.tx_errors==errors)s=SRNOS_SERIAL_OK;
 if(s)goto done;
 uint32_t silence=(t->silence_us+999u)/1000u;
 uint32_t wait_start=now_ms();
 while(now_ms()-c->last_activity_ms<silence){
  if(now_ms()-wait_start>=t->queue_timeout_ms){s=SRNOS_SERIAL_TIMEOUT;goto done;}osDelay(ticks(1));
 }
 SRNOS_SerialDiscardRx(c);
 s=SRNOS_SerialWrite(c,t->tx_data,t->tx_size,t->queue_timeout_ms);if(s)goto done;
 s=SRNOS_SerialFlush(c,t->queue_timeout_ms);
 if(s==SRNOS_SERIAL_TIMEOUT){c->cancel_tx=true;osThreadFlagsSet(worker,1u);}
 if(s==SRNOS_SERIAL_IO&&c->stats.tx_errors==errors)s=SRNOS_SERIAL_OK;
 if(s||!t->rx_capacity)goto done;
 uint32_t start=now_ms(),last=start;
 for(;;){
  size_t n=pop(c,(uint8_t*)t->rx_data+*received,t->rx_capacity-*received);*received+=n;if(n)last=now_ms();
  if(t->match){int m=t->match(t->rx_data,*received,t->match_context);if(m>0){if((size_t)m>*received)s=SRNOS_SERIAL_IO;else *received=(size_t)m;break;}if(m<0){s=SRNOS_SERIAL_IO;break;}}
  else if(*received&&now_ms()-last>=(silence>2?silence:2))break;
  if(*received==t->rx_capacity){s=SRNOS_SERIAL_OVERFLOW;break;}
  if(now_ms()-start>=t->response_timeout_ms){s=SRNOS_SERIAL_TIMEOUT;break;}
  osDelay(ticks(1));
 }
 done:osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialGetStats(SRNOS_SerialChannel *c,SRNOS_SerialStats *out) {
 if(!valid(c)||!out||!task_context())return SRNOS_SERIAL_ARGUMENT;
 taskENTER_CRITICAL();*out=c->stats;taskEXIT_CRITICAL();return SRNOS_SERIAL_OK;
}
