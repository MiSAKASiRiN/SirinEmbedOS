#include "thread_internal.h"
#include "serial_internal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#define SRNOS_SERIAL_RX_SIZE 1024u
#define SRNOS_SERIAL_TX_DEPTH 4u
typedef struct { uint16_t size; uint8_t bytes[SRNOS_SERIAL_MAX_WRITE]; } SRNOS_SerialPacket;
struct SRNOS_SerialChannel {
 SRNOS_SerialConfig config;
 const void *owner;
 bool configured;
 volatile bool active, tx_done, error, cancel_tx;
 uint32_t tx_started_ms, recover_after_ms;
 volatile uint32_t last_activity_ms;
 volatile uint32_t rx_head, rx_tail;
 uint8_t rx[SRNOS_SERIAL_RX_SIZE];
 osMutexId_t gate;
 osMessageQueueId_t tx_queue;
 osSemaphoreId_t rx_event, tx_event;
 StaticSemaphore_t rx_event_storage, tx_event_storage;
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
/* One deadline covers lock acquisition and the entire public operation. */
static bool timeout_valid(uint32_t ms) { return ms<=INT32_MAX || ms==UINT32_MAX; }
static uint32_t remaining(uint32_t start,uint32_t timeout) {
 if(timeout==UINT32_MAX)return UINT32_MAX;
 uint32_t elapsed=now_ms()-start;return elapsed>=timeout?0:timeout-elapsed;
}
static void notify(osSemaphoreId_t event) { if(event)(void)osSemaphoreRelease(event); }
static void wait_event(osSemaphoreId_t event,uint32_t timeout) {
 if(timeout)(void)osSemaphoreAcquire(event,ticks(timeout));
}
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
  if(head-tail==SRNOS_SERIAL_RX_SIZE){c->stats.rx_overflow++;continue;}
  c->rx[head%SRNOS_SERIAL_RX_SIZE]=data[i];__atomic_store_n(&c->rx_head,head+1u,__ATOMIC_RELEASE);
  c->stats.rx_bytes++;
 }
 c->last_activity_ms=now_ms();
 if(len)notify(c->rx_event);
}
void SRNOS_SerialOnTxComplete(SRNOS_SerialPortId p) {
 SRNOS_SerialChannel *c=lookup(p);if(!c)return;
 c->tx_done=true; c->last_activity_ms=now_ms();
 notify(c->tx_event);
 if(worker)osThreadFlagsSet(worker,1u);
}
void SRNOS_SerialOnError(SRNOS_SerialPortId p) {
 SRNOS_SerialChannel *c=lookup(p);if(!c)return;
 c->stats.hardware_errors++;c->stats.hardware_ready=false;c->stats.last_error=SRNOS_SERIAL_IO;if(!c->error)c->recover_after_ms=now_ms();c->error=true;notify(c->rx_event);notify(c->tx_event);if(worker)osThreadFlagsSet(worker,1u);
}
static void serial_service_step(void) {
 SRNOS_SerialPacket packet;
  for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
   SRNOS_SerialChannel *c=&channels[i]; if(!c->configured)continue;
   uint32_t before=c->stats.tx_completed;
   if(c->cancel_tx) {
    if(c->active){SRNOS_SerialBoardAbortTx(c->config.port);c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;}
    c->tx_done=false;
    while(osMessageQueueGet(c->tx_queue,&packet,NULL,0)==osOK){c->stats.tx_errors++;c->stats.tx_completed++;}
    c->cancel_tx=false;
   }
   if(c->error && (int32_t)(now_ms()-c->recover_after_ms)>=0) {
    c->error=false;
    if(c->active){SRNOS_SerialBoardAbortTx(c->config.port);c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;}
    c->tx_done=false;
    if(SRNOS_SerialBoardRecover(c->config.port)){c->error=true;c->recover_after_ms=now_ms()+100u;c->stats.hardware_ready=false;c->stats.last_error=SRNOS_SERIAL_IO;}
    else {c->stats.recoveries++;c->stats.hardware_ready=true;c->stats.last_error=SRNOS_SERIAL_OK;}
   }
   if(c->active&&c->tx_done) {c->tx_done=false;c->active=false;c->stats.tx_completed++;}
   if(c->active && now_ms()-c->tx_started_ms > 100u+(SRNOS_SERIAL_MAX_WRITE*12000u/c->config.baud_rate)) {
    SRNOS_SerialBoardAbortTx(c->config.port); c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;
   }
   if(!c->active)c->tx_done=false;
   if(!c->active&&!c->error&&osMessageQueueGet(c->tx_queue,&packet,NULL,0)==osOK) {
    c->tx_done=false;c->active=true;c->tx_started_ms=now_ms();
    if(SRNOS_SerialBoardTransmit(c->config.port,packet.bytes,packet.size)) {
     c->active=false;c->stats.tx_errors++;c->stats.tx_completed++;
    }
   }
   if(c->stats.tx_completed!=before)notify(c->tx_event);
  }
}
static void serial_step(void *arg){(void)arg;serial_service_step();}
static uint32_t serial_next_wait(void) {
 uint32_t now=now_ms(),wait=UINT32_MAX;
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
  SRNOS_SerialChannel *c=&channels[i];if(!c->configured)continue;
  if(c->cancel_tx || c->tx_done || (!c->active&&!c->error&&osMessageQueueGetCount(c->tx_queue)))return 0;
  uint32_t deadline=0;
  if(c->error)deadline=c->recover_after_ms;
  else if(c->active)deadline=c->tx_started_ms+101u+(SRNOS_SERIAL_MAX_WRITE*12000u/c->config.baud_rate);
  else continue;
  int32_t left=(int32_t)(deadline-now);if(left<=0)return 0;
  if((uint32_t)left<wait)wait=(uint32_t)left;
 }
 return wait;
}
static bool serial_wait(void *arg){(void)arg;(void)osThreadFlagsWait(1u,osFlagsWaitAny,ticks(serial_next_wait()));return true;}
static void serial_worker(void *arg) {
 (void)arg;
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
  if(channels[i].configured){
   if(SRNOS_SerialBoardStart(&channels[i].config)){channels[i].error=true;channels[i].stats.last_error=SRNOS_SERIAL_IO;}
   else {channels[i].stats.hardware_ready=true;channels[i].stats.last_error=SRNOS_SERIAL_OK;}
  }
 }
 const SRNOS_InternalLoop loop={serial_step,serial_wait,NULL,0,false};
 SRNOS_KernelRunLoop(&loop);
}
SRNOS_SerialStatus SRNOS_SerialStart(void) {
 if(started)return SRNOS_SERIAL_FROZEN;
 if(!frozen||osKernelGetState()!=osKernelReady)return SRNOS_SERIAL_STATE;
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++)if(channels[i].configured&&!channels[i].owner)return SRNOS_SERIAL_STATE;
 started=true; bool any=false;
 const osMutexAttr_t mutex={.name="SRNOS_Serial",.attr_bits=osMutexRecursive|osMutexPrioInherit};
 for(size_t i=1;i<SRNOS_SERIAL_PORT_COUNT;i++) {
  SRNOS_SerialChannel *c=&channels[i];if(!c->configured)continue;any=true;
  c->gate=osMutexNew(&mutex);c->tx_queue=osMessageQueueNew(SRNOS_SERIAL_TX_DEPTH,sizeof(SRNOS_SerialPacket),NULL);
  const osSemaphoreAttr_t rx_attr={.cb_mem=&c->rx_event_storage,.cb_size=sizeof(c->rx_event_storage)};
  const osSemaphoreAttr_t tx_attr={.cb_mem=&c->tx_event_storage,.cb_size=sizeof(c->tx_event_storage)};
  c->rx_event=osSemaphoreNew(1,0,&rx_attr);c->tx_event=osSemaphoreNew(1,0,&tx_attr);
  if(!c->gate||!c->tx_queue||!c->rx_event||!c->tx_event)return SRNOS_SERIAL_IO;
 }
 if(any){const osThreadAttr_t attr={.name="SRNOS_Serial",.stack_size=2048,.priority=osPriorityHigh7};
  worker=SRNOS_KernelThreadNew(serial_worker,NULL,&attr);if(!worker)return SRNOS_SERIAL_IO;}
 return SRNOS_SERIAL_OK;
}
static SRNOS_SerialStatus lock(SRNOS_SerialChannel *c,uint32_t timeout) {
 if(!valid(c)||!started||!task_context())return SRNOS_SERIAL_STATE;
 return osMutexAcquire(c->gate,ticks(timeout))==osOK?SRNOS_SERIAL_OK:SRNOS_SERIAL_TIMEOUT;
}
static size_t pop(SRNOS_SerialChannel *c,uint8_t *out,size_t n) {
 size_t i=0;uint32_t tail=__atomic_load_n(&c->rx_tail,__ATOMIC_RELAXED);
 uint32_t head=__atomic_load_n(&c->rx_head,__ATOMIC_ACQUIRE);
 while(i<n&&tail!=head){out[i++]=c->rx[tail%SRNOS_SERIAL_RX_SIZE];tail++;}
 __atomic_store_n(&c->rx_tail,tail,__ATOMIC_RELEASE);return i;
}
SRNOS_SerialStatus SRNOS_SerialRead(SRNOS_SerialChannel *c,void *out,size_t cap,size_t *received,uint32_t timeout) {
 if(!received||!out||!cap||!timeout_valid(timeout))return SRNOS_SERIAL_ARGUMENT;*received=0;
 uint32_t start=now_ms();
 for(;;){
  SRNOS_SerialStatus s=lock(c,remaining(start,timeout));if(s)return s;
  if(!c->config.rx_enabled){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
  *received=pop(c,out,cap);
  if(c->rx_head!=c->rx_tail)notify(c->rx_event);
  osMutexRelease(c->gate);
  if(*received)return SRNOS_SERIAL_OK;
  uint32_t left=remaining(start,timeout);if(!left)return SRNOS_SERIAL_TIMEOUT;
  /* Binary token persists if RX races the predicate check and wait. Read
   * releases the gate, so an idle receiver cannot block full-duplex TX. */
  wait_event(c->rx_event,left);
 }
}
SRNOS_SerialStatus SRNOS_SerialWrite(SRNOS_SerialChannel *c,const void *data,size_t n,uint32_t timeout) {
 if(!data||!n||n>SRNOS_SERIAL_MAX_WRITE||!timeout_valid(timeout))return SRNOS_SERIAL_ARGUMENT;
 uint32_t start=now_ms();SRNOS_SerialStatus s=lock(c,timeout);if(s)return s;
 if(!c->config.tx_enabled||c->cancel_tx){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
 SRNOS_SerialPacket p={.size=(uint16_t)n};memcpy(p.bytes,data,n);
 if(osMessageQueuePut(c->tx_queue,&p,0,ticks(remaining(start,timeout)))!=osOK)s=SRNOS_SERIAL_TIMEOUT;
 else {c->stats.tx_accepted++;osThreadFlagsSet(worker,1u);}
 osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialFlush(SRNOS_SerialChannel *c,uint32_t timeout) {
 if(!timeout_valid(timeout))return SRNOS_SERIAL_ARGUMENT;
 uint32_t start=now_ms();SRNOS_SerialStatus s=lock(c,timeout);if(s)return s;
 while(c->stats.tx_completed!=c->stats.tx_accepted){
  uint32_t left=remaining(start,timeout);if(!left){s=SRNOS_SERIAL_TIMEOUT;break;}
  wait_event(c->tx_event,left);
 }
 if(!s&&c->stats.tx_errors)s=SRNOS_SERIAL_IO;
 osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialDiscardRx(SRNOS_SerialChannel *c) {
 SRNOS_SerialStatus s=lock(c,0);if(s)return s;
 __atomic_store_n(&c->rx_tail,__atomic_load_n(&c->rx_head,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
 osMutexRelease(c->gate);return SRNOS_SERIAL_OK;
}
SRNOS_SerialStatus SRNOS_SerialTransceive(SRNOS_SerialChannel *c,const SRNOS_SerialTransaction *t,size_t *received) {
 if(!t||!received||(t->rx_capacity&&!t->rx_data)||!timeout_valid(t->queue_timeout_ms)||
    !timeout_valid(t->response_timeout_ms))return SRNOS_SERIAL_ARGUMENT;
 *received=0;
 uint32_t queued=now_ms();SRNOS_SerialStatus s=lock(c,t->queue_timeout_ms);if(s)return s;
 if((t->rx_capacity&&!c->config.rx_enabled)||c->cancel_tx){osMutexRelease(c->gate);return SRNOS_SERIAL_STATE;}
 uint32_t errors=c->stats.tx_errors;
 s=SRNOS_SerialFlush(c,remaining(queued,t->queue_timeout_ms));
 if(s==SRNOS_SERIAL_IO&&c->stats.tx_errors==errors)s=SRNOS_SERIAL_OK;
 if(s)goto done;
 uint32_t silence=t->silence_us/1000u+(t->silence_us%1000u!=0);
 for(;;){
  uint32_t quiet=now_ms()-c->last_activity_ms;if(quiet>=silence)break;
  uint32_t left=remaining(queued,t->queue_timeout_ms);if(!left){s=SRNOS_SERIAL_TIMEOUT;goto done;}
  uint32_t until_quiet=silence-quiet;if(until_quiet<left)left=until_quiet;
  wait_event(c->rx_event,left);
 }
 SRNOS_SerialDiscardRx(c);
 s=SRNOS_SerialWrite(c,t->tx_data,t->tx_size,remaining(queued,t->queue_timeout_ms));if(s)goto done;
 s=SRNOS_SerialFlush(c,remaining(queued,t->queue_timeout_ms));
 if(s==SRNOS_SERIAL_TIMEOUT){c->cancel_tx=true;osThreadFlagsSet(worker,1u);}
 if(s==SRNOS_SERIAL_IO&&c->stats.tx_errors==errors)s=SRNOS_SERIAL_OK;
 if(s||!t->rx_capacity)goto done;
 uint32_t start=now_ms(),last=start;
 for(;;){
  size_t n=pop(c,(uint8_t*)t->rx_data+*received,t->rx_capacity-*received);*received+=n;if(n)last=now_ms();
  if(t->match){int m=t->match(t->rx_data,*received,t->match_context);if(m>0){if((size_t)m>*received)s=SRNOS_SERIAL_IO;else *received=(size_t)m;break;}if(m<0){s=SRNOS_SERIAL_IO;break;}}
  else if(*received&&now_ms()-last>=(silence>2?silence:2))break;
  if(*received==t->rx_capacity){s=SRNOS_SERIAL_OVERFLOW;break;}
  uint32_t left=remaining(start,t->response_timeout_ms);if(!left){s=SRNOS_SERIAL_TIMEOUT;break;}
  if(!t->match&&*received){uint32_t gap=(silence>2?silence:2)-(now_ms()-last);if(gap<left)left=gap;}
  wait_event(c->rx_event,left);
 }
 done:osMutexRelease(c->gate);return s;
}
SRNOS_SerialStatus SRNOS_SerialGetStats(SRNOS_SerialChannel *c,SRNOS_SerialStats *out) {
 if(!valid(c)||!out||!task_context())return SRNOS_SERIAL_ARGUMENT;
 taskENTER_CRITICAL();*out=c->stats;taskEXIT_CRITICAL();return SRNOS_SERIAL_OK;
}
