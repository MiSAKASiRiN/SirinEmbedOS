#include "message_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
typedef struct {
    SRNOS_MessageRouteConfig config;
    SRNOS_MessageStats stats;
    uint8_t *storage;
    uint16_t stride, head;
} route;
static route routes[SRNOS_MESSAGE_ROUTES];
static unsigned count;
static uint32_t bytes;
static bool frozen,started;
static bool startup(void) {return !xPortIsInsideInterrupt()&&xTaskGetSchedulerState()==taskSCHEDULER_NOT_STARTED;}
static bool runtime(void) {return !xPortIsInsideInterrupt()&&xTaskGetSchedulerState()==taskSCHEDULER_RUNNING&&started;}
SRNOS_MessageResult SRNOS_MessageRegister(const SRNOS_MessageRouteConfig *c,SRNOS_MessageRoute *out)
{
    if(!startup())return SRNOS_MESSAGE_CONTEXT;
    if(frozen)return SRNOS_MESSAGE_FROZEN;
    if(!c||!out||!c->transports||(c->transports&~31u)||c->max_payload>SRNOS_MESSAGE_PAYLOAD_MAX||
       ((c->transports&SRNOS_MESSAGE_HOST)&&(c->service==0||c->service==3))||
       c->policy>SRNOS_MESSAGE_LATEST||c->max_age_ms>60000||
       (c->handler?c->depth!=0:(!c->depth||c->depth>16))||
       (!c->handler&&c->policy==SRNOS_MESSAGE_LATEST&&c->depth!=1))return SRNOS_MESSAGE_INVALID;
    const unsigned stride=(sizeof(SRNOS_MessageInfo)+c->max_payload+7u)&~7u;
    const unsigned allocation=c->depth?stride*c->depth+32u:0;
    if(count==SRNOS_MESSAGE_ROUTES||bytes+allocation>SRNOS_MESSAGE_QUEUE_BYTES_MAX)return SRNOS_MESSAGE_FULL;
    routes[count].config=*c;routes[count].stride=(uint16_t)stride;
    bytes+=allocation;*out=(uint8_t)count++;return SRNOS_MESSAGE_OK;
}
const SRNOS_MessageRouteConfig *SRNOS_MessageRouteAt(unsigned i)
{return i<count?&routes[i].config:NULL;}
bool SRNOS_MessageFreeze(void) {if(!startup()||frozen)return false;frozen=true;return true;}
uint32_t SRNOS_MessageHeapBudget(void) {return bytes;}
bool SRNOS_MessageStart(void)
{
    if(!startup()||!frozen||started)return false;
    for(unsigned i=0;i<count;i++)if(routes[i].config.depth) {
        routes[i].storage=pvPortMalloc(routes[i].stride*routes[i].config.depth);
        if(!routes[i].storage) {
            for(unsigned n=0;n<i;n++){vPortFree(routes[n].storage);routes[n].storage=NULL;}
            return false;
        }
    }
    started=true;return true;
}
SRNOS_MessageResult SRNOS_MessageDispatch(const SRNOS_MessageInfo *m,const void *data)
{
    if(!runtime())return SRNOS_MESSAGE_CONTEXT;
    if(!m||(!data&&m->length)||m->length>SRNOS_MESSAGE_PAYLOAD_MAX||
       !m->origin.transport||(m->origin.transport&~31u)||
       (m->origin.transport&(m->origin.transport-1u)))return SRNOS_MESSAGE_INVALID;
    unsigned matched=0,accepted=0,full=0;
    for(unsigned i=0;i<count;i++) {
        route *r=&routes[i];const SRNOS_MessageRouteConfig *c=&r->config;
        if(!(c->transports&m->origin.transport)||c->service!=m->service||c->type!=m->type||
           (c->endpoint&&c->endpoint!=m->origin.endpoint))continue;
        ++matched;
        bool expired=c->max_age_ms&&SRNOS_TimeMs()-m->received_ms>c->max_age_ms;
        taskENTER_CRITICAL();
        if(m->length>c->max_payload||expired) {
            ++r->stats.dropped;if(expired)++r->stats.expired;
            taskEXIT_CRITICAL();continue;
        }
        if(c->handler) {
            taskEXIT_CRITICAL();
            SRNOS_MessageResult result=c->handler(m,data,c->user);
            taskENTER_CRITICAL();
            if(result==SRNOS_MESSAGE_OK){++r->stats.delivered;++accepted;}
            else {++r->stats.handler_errors;++r->stats.dropped;if(result==SRNOS_MESSAGE_FULL)++full;}
        } else {
            if(r->stats.queued==c->depth&&c->policy==SRNOS_MESSAGE_FIFO){++r->stats.dropped;++full;}
            else {
                if(r->stats.queued==c->depth){r->stats.queued=0;++r->stats.replaced;}
                uint8_t *slot=r->storage+((r->head+r->stats.queued)%c->depth)*r->stride;
                memcpy(slot,m,sizeof(*m));if(m->length)memcpy(slot+sizeof(*m),data,m->length);
                ++r->stats.queued;++r->stats.delivered;++accepted;
                if(r->stats.queued>r->stats.high_water)r->stats.high_water=r->stats.queued;
            }
        }
        taskEXIT_CRITICAL();
    }
    return accepted?SRNOS_MESSAGE_OK:full?SRNOS_MESSAGE_FULL:matched?SRNOS_MESSAGE_REJECTED:SRNOS_MESSAGE_NO_ROUTE;
}
SRNOS_MessageResult SRNOS_MessageReceive(SRNOS_MessageRoute handle,SRNOS_MessageInfo *out,void *data,size_t capacity)
{
    if(!runtime())return SRNOS_MESSAGE_CONTEXT;
    if(handle>=count||!out||(!data&&capacity)||routes[handle].config.handler)return SRNOS_MESSAGE_INVALID;
    route *r=&routes[handle];taskENTER_CRITICAL();
    while(r->stats.queued) {
        uint8_t *slot=r->storage+r->head*r->stride;SRNOS_MessageInfo info;
        memcpy(&info,slot,sizeof(info));
        if(r->config.max_age_ms&&SRNOS_TimeMs()-info.received_ms>r->config.max_age_ms) {
            r->head=(r->head+1u)%r->config.depth;--r->stats.queued;++r->stats.expired;continue;
        }
        if(info.length>capacity){taskEXIT_CRITICAL();return SRNOS_MESSAGE_BUFFER_SMALL;}
        *out=info;if(info.length)memcpy(data,slot+sizeof(info),info.length);
        r->head=(r->head+1u)%r->config.depth;--r->stats.queued;
        taskEXIT_CRITICAL();return SRNOS_MESSAGE_OK;
    }
    taskEXIT_CRITICAL();return SRNOS_MESSAGE_EMPTY;
}
SRNOS_MessageResult SRNOS_MessageGetStats(SRNOS_MessageRoute handle,SRNOS_MessageStats *out)
{
    if(!runtime())return SRNOS_MESSAGE_CONTEXT;
    if(handle>=count||!out)return SRNOS_MESSAGE_INVALID;
    taskENTER_CRITICAL();*out=routes[handle].stats;taskEXIT_CRITICAL();return SRNOS_MESSAGE_OK;
}
