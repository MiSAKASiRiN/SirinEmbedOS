#include "host_comm_internal.h"
#include "thread_internal.h"
#include "Services/Telemetry/telemetry_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include "cmsis_os2.h"

#include <string.h>

#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
static uint32_t boot_lock_depth, boot_lock_key;
static void host_enter_critical(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        if (boot_lock_depth++ == 0u) { boot_lock_key=__get_PRIMASK(); __disable_irq(); }
    } else { taskENTER_CRITICAL(); }
}
static void host_exit_critical(void)
{
    if (boot_lock_depth) {
        if (--boot_lock_depth == 0u) __set_PRIMASK(boot_lock_key);
    } else { taskEXIT_CRITICAL(); }
}
#define SRNOS_SIRIN_LINK_ENTER_CRITICAL() host_enter_critical()
#define SRNOS_SIRIN_LINK_EXIT_CRITICAL() host_exit_critical()
#else
#define SRNOS_SIRIN_LINK_ENTER_CRITICAL() taskENTER_CRITICAL()
#define SRNOS_SIRIN_LINK_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif

#define SRNOS_SIRIN_LINK_NOTIFY_RX          (1u << 0)
#define SRNOS_SIRIN_LINK_RX_BUDGET_BYTES    1024u
#define SRNOS_SIRIN_LINK_WORK_DEPTH         4u
#define SRNOS_SIRIN_LINK_COMPLETION_DEPTH   4u
#define SRNOS_SIRIN_LINK_EXEC_COUNT         8u
#define SRNOS_SIRIN_LINK_TELEMETRY_COUNT    12u
#define SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS 32u
#define SRNOS_SIRIN_LINK_SOURCE_V2_TOTAL_HZ 8000u
#define SRNOS_SIRIN_LINK_SOURCE_BATCH_MAX 16u
#define SRNOS_SIRIN_LINK_SOURCE_DATA_HEADER_SIZE 12u
#define SRNOS_SIRIN_LINK_SOURCE_RECORD_HEADER_SIZE 4u
#define SRNOS_SIRIN_LINK_FNV_OFFSET         2166136261u
#define SRNOS_SIRIN_LINK_FNV_PRIME          16777619u

enum {
    SRNOS_SIRIN_LINK_EXEC_MESSAGE_CALL = 0x01u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SUBSCRIBE = 0x01u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SAMPLE = 0x02u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_LIST = 0x03u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE = 0x04u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL = 0x05u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL = 0x06u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA = 0x07u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_LIST_V2 = 0x10u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE_V2 = 0x11u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA_V2 = 0x12u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_V2 = 0x13u,
    SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL_V2 = 0x14u,
};

typedef enum {
    SRNOS_SIRIN_LINK_WORK_EXEC = 0u,
    SRNOS_SIRIN_LINK_WORK_DEFERRED = 1u,
} work_kind_t;

typedef struct {
    uint32_t session;
    uint8_t kind;
    uint8_t service;
    uint8_t message;
    uint8_t reserved;
    uint16_t command_id;
    uint16_t payload_len;
    uint32_t sequence;
    SRNOS_SirinLinkDeferredFn deferred;
    void *user;
    uint8_t payload[SRNOS_SIRIN_LINK_WORK_PAYLOAD_MAX];
} work_item_t;

typedef struct {
    uint32_t session;
    uint8_t service;
    uint8_t message;
    uint8_t flags;
    uint8_t reserved;
    uint16_t status;
    uint16_t body_len;
    uint32_t sequence;
    uint8_t body[SRNOS_SIRIN_LINK_RESPONSE_BODY_MAX];
} completion_t;

typedef struct {
    uint8_t used;
    uint16_t command_id;
    SRNOS_SirinLinkExecFn handler;
    void *user;
} exec_entry_t;

typedef struct {
    uint8_t used;
    uint8_t enabled;
    uint16_t stream_id;
    uint16_t max_sample_size;
    SRNOS_SirinLinkTelemetryConfig extended;
    uint32_t period_us;
    uint32_t next_poll_ms;
    SRNOS_SirinLinkTelemetryConfigFn on_config;
    void *user;
} telemetry_entry_t;

typedef struct {
    const SRNOS_TelemetryDescriptor *descriptor;
    uint16_t rate_hz;
    uint8_t version;
} telemetry_source_entry_t;
typedef struct {
    uint32_t active_mask, phase_milli;
    uint16_t rate_hz;
    uint8_t version;
} telemetry_rate_group_t;

typedef struct {
    uint8_t used;
    uint16_t channel;
    uint8_t *storage;
    size_t capacity;
} control_registration_t;

struct SRNOS_SirinLinkRegistration { SRNOS_SirinLinkContext *context; bool active; };
static SRNOS_SirinLinkRegistration s_registration;
static SRNOS_SIRIN_LINK_ALIGN32 SRNOS_SirinLinkContext s_link;
static SRNOS_SirinLinkConfig s_config;
static osMessageQueueId_t s_work_queue;
static osMessageQueueId_t s_completion_queue;
static TaskHandle_t s_comm_task;
static TaskHandle_t s_worker_task;
static exec_entry_t s_exec[SRNOS_SIRIN_LINK_EXEC_COUNT];
static telemetry_entry_t s_telemetry[SRNOS_SIRIN_LINK_TELEMETRY_COUNT];
static telemetry_source_entry_t
    s_sources[SRNOS_SIRIN_LINK_SOURCE_COUNT_MAX];
static telemetry_rate_group_t s_source_groups[SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS];
static control_registration_t s_control[SRNOS_SIRIN_LINK_CONTROL_CHANNEL_COUNT];
static uint8_t s_source_payload[SRNOS_PROTOCOL_MAX_PAYLOAD];
static SRNOS_SirinLinkSourceSnapshot s_source_stats;
static uint32_t s_deferred_high_water;
static uint32_t s_last_rx_overflow;
static uint32_t s_last_snapshot;
static uint32_t s_source_immediate_mask;
static uint32_t s_source_last_poll_ms;
static uint32_t s_source_sequence;
static uint32_t s_source_catalog_hash;
static uint8_t s_source_count;
static uint8_t s_transport_connected;
static uint8_t s_configured;
static uint8_t s_link_ready;
static uint8_t s_started;
static uint32_t s_session, s_transport_generation;
static unsigned s_poll_cursor;
static completion_t s_pending_completion;
static bool s_completion_pending, s_rx_budget_exhausted;
static void wake_comm(void) { if(s_comm_task && xTaskGetCurrentTaskHandle()!=s_comm_task) xTaskNotifyGive(s_comm_task); }
static uint32_t telemetry_poll_interval(const telemetry_entry_t *entry)
{
    if(entry->extended.poll_interval_ms) return entry->extended.poll_interval_ms;
    const uint32_t us=entry->period_us;
    return us ? us/1000u + ((us%1000u)!=0u) : 1u;
}

static uint32_t link_now_ms(void *user)
{
    (void)user;
    return s_config.now_ms(s_config.clock_user);
}

static uint32_t link_cycles(void *user)
{
    (void)user;
    return (s_config.cycles != NULL) ?
        s_config.cycles(s_config.clock_user) : 0u;
}

static void link_lock(void *user)
{
    (void)user;
    SRNOS_SIRIN_LINK_ENTER_CRITICAL();
}

static void link_unlock(void *user)
{
    (void)user;
    SRNOS_SIRIN_LINK_EXIT_CRITICAL();
}

static SRNOS_SirinLinkCoreTxResult transport_submit(void *user,
                                             const uint8_t *data,
                                             size_t length)
{
    (void)user;
    return s_config.transport.tx_submit(s_config.transport.user, data, length);
}

static exec_entry_t *find_exec(uint16_t command_id)
{
    for (size_t i = 0u; i < SRNOS_SIRIN_LINK_EXEC_COUNT; ++i) {
        if ((s_exec[i].used != 0u) && (s_exec[i].command_id == command_id)) {
            return &s_exec[i];
        }
    }
    return NULL;
}

static telemetry_entry_t *find_telemetry(uint16_t stream_id)
{
    for (size_t i = 0u; i < SRNOS_SIRIN_LINK_TELEMETRY_COUNT; ++i) {
        if ((s_telemetry[i].used != 0u) &&
            (s_telemetry[i].stream_id == stream_id)) {
            return &s_telemetry[i];
        }
    }
    return NULL;
}

_Static_assert(SRNOS_SIRIN_LINK_SOURCE_COUNT_MAX==SRNOS_TELEMETRY_CAPACITY,"source masks");
_Static_assert(16u+SRNOS_SIRIN_LINK_SOURCE_BATCH_MAX*20u<=SRNOS_PROTOCOL_MAX_PAYLOAD,"source packet");
static uint8_t source_value_size(SRNOS_TelemetryType type)
{ return type==SRNOS_TELEMETRY_BOOL ? 1u : 4u; }
static uint32_t fnv_byte(uint32_t hash,uint8_t value)
{ return (hash^value)*SRNOS_SIRIN_LINK_FNV_PRIME; }
static uint32_t fnv_u32(uint32_t hash,uint32_t value)
{ for(unsigned i=0;i<32;i+=8)hash=fnv_byte(hash,(uint8_t)(value>>i));return hash; }
static uint32_t freeze_source_catalog(void)
{
    s_source_count=(uint8_t)SRNOS_TelemetryCount();
    uint32_t hash=fnv_u32(SRNOS_SIRIN_LINK_FNV_OFFSET,s_source_count);
    for(unsigned i=0;i<s_source_count;++i) {
        const SRNOS_TelemetryDescriptor *d=SRNOS_TelemetryDescribe((uint16_t)i);
        s_sources[i].descriptor=d;
        hash=fnv_byte(hash,(uint8_t)d->type);hash=fnv_byte(hash,source_value_size(d->type));
        hash=fnv_byte(hash,(uint8_t)strlen(d->name));
        for(const char *p=d->name;*p;++p)hash=fnv_byte(hash,(uint8_t)*p);
        hash=fnv_byte(hash,(uint8_t)strlen(d->unit));
        for(const char *p=d->unit;*p;++p)hash=fnv_byte(hash,(uint8_t)*p);
    }
    s_source_stats.registered_count=s_source_count;
    return s_source_catalog_hash=hash;
}
static uint32_t mask_population(uint32_t mask)
{ uint32_t n=0;while(mask){mask&=mask-1u;++n;}return n; }
static void discard_source_data(void)
{
    (void)SRNOS_SirinLinkCoreDiscardTopic(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA);
    (void)SRNOS_SirinLinkCoreDiscardTopic(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA_V2);
}
static void rebuild_source_groups(void)
{
    /* Retain stable group slots (hence coalescing keys) across rate changes.
     * There can be no more active (version,rate) pairs than registered sources. */
    for(unsigned g=0;g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;++g)s_source_groups[g].active_mask=0;
    for(unsigned i=0;i<s_source_count;++i) {
        const telemetry_source_entry_t *s=&s_sources[i];if(!s->rate_hz)continue;
        unsigned g=0;
        for(;g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;++g)
            if(s_source_groups[g].rate_hz==s->rate_hz && s_source_groups[g].version==s->version)break;
        if(g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS)s_source_groups[g].active_mask|=UINT32_C(1)<<i;
    }
    for(unsigned g=0;g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;++g)
        if(!s_source_groups[g].active_mask)memset(&s_source_groups[g],0,sizeof(s_source_groups[g]));
    s_source_stats.active_count=0;s_source_stats.total_rate_hz=0;
    for(unsigned i=0;i<s_source_count;++i) {
        const telemetry_source_entry_t *s=&s_sources[i];if(!s->rate_hz)continue;
        unsigned g=0;
        for(;g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;++g)
            if(s_source_groups[g].rate_hz==s->rate_hz && s_source_groups[g].version==s->version)break;
        if(g==SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS) {
            for(g=0;s_source_groups[g].active_mask;++g) { }
            s_source_groups[g].rate_hz=s->rate_hz;s_source_groups[g].version=s->version;
        }
        s_source_groups[g].active_mask|=UINT32_C(1)<<i;
        ++s_source_stats.active_count;s_source_stats.total_rate_hz+=s->rate_hz;
    }
}
static void clear_source_subscriptions(void)
{
    for(unsigned i=0;i<s_source_count;++i){s_sources[i].rate_hz=0;s_sources[i].version=0;}
    s_source_immediate_mask=0;rebuild_source_groups();
}
static void clear_all_telemetry_subscriptions(void)
{
    clear_source_subscriptions();
    for(unsigned i=0;i<SRNOS_SIRIN_LINK_TELEMETRY_COUNT;++i) {
        telemetry_entry_t *e=&s_telemetry[i];if(!e->used || !e->enabled)continue;
        e->enabled=0;if(e->on_config)e->on_config(e->user,false,e->period_us);
    }
    (void)SRNOS_SirinLinkCoreDiscardTopic(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SAMPLE);
    discard_source_data();
}
static uint16_t build_source_summary(uint8_t *response,size_t cap,size_t *length)
{
    if(cap<7)return SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
    SRNOS_Memory_MutableByteSpan out=SRNOS_Memory_MutableBytes(response,cap);
    SRNOS_Endian_StoreLE32(out,0,s_source_catalog_hash);response[4]=(uint8_t)s_source_stats.active_count;
    SRNOS_Endian_StoreLE16(out,5,(uint16_t)s_source_stats.total_rate_hz);*length=7;return SRNOS_SIRIN_LINK_STATUS_OK;
}
static bool source_v2(const SRNOS_ProtocolView *request)
{ return request->header.message>=SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_LIST_V2; }
static bool source_hash_matches(const SRNOS_ProtocolView *r)
{
    uint32_t hash=0;return SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(r->payload,r->payload_len),0,&hash) && hash==s_source_catalog_hash;
}
static uint16_t handle_source_list(void *user,const SRNOS_ProtocolView *r,uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*flags=0;
    if(r->payload_len!=3 || !r->payload[2])return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    uint16_t cursor=0;SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(r->payload,r->payload_len),0,&cursor);
    if(cursor>s_source_count)return SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT;
    if(cap<9)return SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
    SRNOS_Memory_MutableByteSpan out=SRNOS_Memory_MutableBytes(response,cap);
    SRNOS_Endian_StoreLE32(out,0,s_source_catalog_hash);SRNOS_Endian_StoreLE16(out,4,s_source_count);
    size_t pos=9;uint8_t count=0;bool v2=source_v2(r);
    for(;cursor<s_source_count && count<r->payload[2];++cursor) {
        const telemetry_source_entry_t *s=&s_sources[cursor];const SRNOS_TelemetryDescriptor *d=s->descriptor;
        size_t nl=strlen(d->name),ul=strlen(d->unit),header=v2?11u:7u;
        if(pos+header+nl+ul>cap)break;
        SRNOS_Endian_StoreLE16(out,pos,cursor);response[pos+2]=(uint8_t)d->type;response[pos+3]=source_value_size(d->type);
        if(v2) {
            SRNOS_Endian_StoreLE16(out,pos+4,d->max_hz);SRNOS_Endian_StoreLE16(out,pos+6,s->rate_hz);
            response[pos+8]=s->version;response[pos+9]=(uint8_t)nl;response[pos+10]=(uint8_t)ul;
        } else {response[pos+4]=s->version==1?(uint8_t)s->rate_hz:0;response[pos+5]=(uint8_t)nl;response[pos+6]=(uint8_t)ul;}
        pos+=header;memcpy(response+pos,d->name,nl);pos+=nl;memcpy(response+pos,d->unit,ul);pos+=ul;++count;
    }
    if(!count && cursor<s_source_count)return SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
    uint16_t next=cursor<s_source_count?cursor:UINT16_MAX;
    SRNOS_Endian_StoreLE16(out,6,next);response[8]=count;*flags=next!=UINT16_MAX?SRNOS_PROTOCOL_FLAG_MORE:0;
    *length=pos;return SRNOS_SIRIN_LINK_STATUS_OK;
}
static uint16_t handle_source_subscribe(void *user,const SRNOS_ProtocolView *r,uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*flags=0;bool v2=source_v2(r);size_t width=v2?4u:3u;
    if(r->payload_len<5 || !r->payload[4] || r->payload[4]>SRNOS_TELEMETRY_CAPACITY ||
        r->payload_len!=5u+r->payload[4]*width)return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    if(!source_hash_matches(r))return SRNOS_SIRIN_LINK_STATUS_CONFLICT;
    uint16_t rates[SRNOS_TELEMETRY_CAPACITY]={0};uint8_t versions[SRNOS_TELEMETRY_CAPACITY]={0};uint32_t seen=0;
    for(unsigned i=0;i<s_source_count;++i){rates[i]=s_sources[i].rate_hz;versions[i]=s_sources[i].version;}
    for(unsigned n=0;n<r->payload[4];++n) {
        size_t pos=5u+n*width;uint16_t id=0,rate=0;
        SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(r->payload,r->payload_len),pos,&id);
        if(v2)SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(r->payload,r->payload_len),pos+2,&rate);else rate=r->payload[pos+2];
        if(id>=s_source_count)return SRNOS_SIRIN_LINK_STATUS_NOT_FOUND;
        uint32_t bit=UINT32_C(1)<<id;if(seen&bit)return SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT;seen|=bit;
        if(rate<5 || rate%5 || rate>(v2?1000u:100u) || rate>s_sources[id].descriptor->max_hz)return SRNOS_SIRIN_LINK_STATUS_OUT_OF_RANGE;
        rates[id]=rate;versions[id]=v2?2:1;
    }
    uint32_t total=0,v1_total=0;
    for(unsigned i=0;i<s_source_count;++i){total+=rates[i];if(versions[i]==1)v1_total+=rates[i];}
    if(total>SRNOS_SIRIN_LINK_SOURCE_V2_TOTAL_HZ || v1_total>SRNOS_SIRIN_LINK_SOURCE_RATE_TOTAL_HZ)return SRNOS_SIRIN_LINK_STATUS_OUT_OF_RANGE;
    for(unsigned i=0;i<s_source_count;++i)if(rates[i]!=s_sources[i].rate_hz || versions[i]!=s_sources[i].version) {
        s_source_immediate_mask|=UINT32_C(1)<<i;s_sources[i].rate_hz=rates[i];s_sources[i].version=versions[i];
    }
    rebuild_source_groups();discard_source_data();return build_source_summary(response,cap,length);
}
static uint16_t handle_source_cancel(void *user,const SRNOS_ProtocolView *r,uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*flags=0;
    if(r->payload_len<5 || !r->payload[4] || r->payload[4]>SRNOS_TELEMETRY_CAPACITY || r->payload_len!=5u+r->payload[4]*2u)return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    if(!source_hash_matches(r))return SRNOS_SIRIN_LINK_STATUS_CONFLICT;
    uint32_t mask=0;
    for(unsigned n=0;n<r->payload[4];++n) {
        uint16_t id=0;SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(r->payload,r->payload_len),5u+n*2u,&id);
        if(id>=s_source_count || !s_sources[id].rate_hz || s_sources[id].version!=(source_v2(r)?2:1))return SRNOS_SIRIN_LINK_STATUS_NOT_FOUND;
        uint32_t bit=UINT32_C(1)<<id;if(mask&bit)return SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT;mask|=bit;
    }
    for(unsigned i=0;i<s_source_count;++i)if(mask&(UINT32_C(1)<<i)){s_sources[i].rate_hz=0;s_sources[i].version=0;}
    s_source_immediate_mask&=~mask;rebuild_source_groups();discard_source_data();return build_source_summary(response,cap,length);
}
static uint16_t handle_source_cancel_all(void *user,const SRNOS_ProtocolView *r,uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*flags=0;if(r->payload_len!=4)return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    if(!source_hash_matches(r))return SRNOS_SIRIN_LINK_STATUS_CONFLICT;
    for(unsigned i=0;i<s_source_count;++i)if(s_sources[i].version==(source_v2(r)?2:1)) {
        s_sources[i].rate_hz=0;s_sources[i].version=0;s_source_immediate_mask&=~(UINT32_C(1)<<i);
    }
    rebuild_source_groups();discard_source_data();return build_source_summary(response,cap,length);
}
static void record_source_cycles(uint32_t start)
{
    if(!s_config.cycles)return;uint32_t elapsed=link_cycles(NULL)-start;
    s_source_stats.cycles_total+=elapsed;if(elapsed>s_source_stats.cycles_max)s_source_stats.cycles_max=elapsed;
    SRNOS_SirinLinkCoreRecordProcessingCycles(&s_link,elapsed);
}
static void publish_source_batch(unsigned group,unsigned block,uint32_t mask,uint32_t now)
{
    bool v2=s_source_groups[group].version==2;size_t pos=v2?16u:12u;uint8_t count=0;
    SRNOS_Memory_MutableByteSpan out=SRNOS_Memory_MutableBytes(s_source_payload,sizeof(s_source_payload));
    SRNOS_Endian_StoreLE32(out,0,s_source_catalog_hash);
    if(v2) {SRNOS_Endian_StoreLE64(out,4,SRNOS_TimeUs());memset(s_source_payload+12,0,4);}
    else {SRNOS_Endian_StoreLE32(out,4,now);memset(s_source_payload+8,0,4);}
    for(unsigned i=block*16u;i<s_source_count && i<(block+1u)*16u;++i) {
        if(!(mask&(UINT32_C(1)<<i)))continue;
        const SRNOS_TelemetryDescriptor *d=s_sources[i].descriptor;SRNOS_TelemetrySample sample={0};
        bool available=SRNOS_TelemetryRead((uint16_t)i,&sample) && sample.valid;
        SRNOS_Endian_StoreLE16(out,pos,(uint16_t)i);s_source_payload[pos+2]=(uint8_t)d->type;
        if(v2) {
            s_source_payload[pos+3]=available?1u:0u;SRNOS_Endian_StoreLE32(out,pos+4,sample.sequence);
            SRNOS_Endian_StoreLE64(out,pos+8,sample.sampled_us);pos+=16;
        } else {s_source_payload[pos+3]=available?source_value_size(d->type):0u;pos+=4;}
        if(!available)++s_source_stats.read_failures;
        if(v2 || available) {
            if(d->type==SRNOS_TELEMETRY_BOOL)s_source_payload[pos++]=sample.value.boolean?1u:0u;
            else {uint32_t raw=0;memcpy(&raw,&sample.value,4);SRNOS_Endian_StoreLE32(out,pos,raw);pos+=4;}
        }
        ++count;
    }
    if(!count)return;s_source_payload[v2?12:8]=count;++s_source_stats.batches;s_source_stats.records+=count;
    uint8_t message=v2?SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA_V2:SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA;
    if(SRNOS_SirinLinkCorePublish(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,message,(uint16_t)(group*2u+block),
        ++s_source_sequence,s_source_payload,pos)!=SRNOS_SIRIN_LINK_OK)++s_source_stats.publish_failures;
}
static void poll_telemetry_sources(uint32_t now)
{
    uint32_t cycles=s_config.cycles?link_cycles(NULL):0,elapsed=now-s_source_last_poll_ms;
    s_source_last_poll_ms=now;if(!s_source_stats.active_count)return;
    uint32_t immediate=s_source_immediate_mask;s_source_immediate_mask=0;
    for(unsigned g=0;g<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;++g) {
        telemetry_rate_group_t *group=&s_source_groups[g];if(!group->active_mask)continue;
        uint64_t phase=group->phase_milli+(uint64_t)elapsed*group->rate_hz;
        uint32_t periods=(uint32_t)(phase/1000u);group->phase_milli=(uint32_t)(phase%1000u);
        uint32_t mask=immediate&group->active_mask;if(periods)mask=group->active_mask;
        if(periods>1)s_source_stats.missed_periods+=(periods-1u)*mask_population(group->active_mask);
        if(mask&UINT32_C(0xFFFF))publish_source_batch(g,0,mask,now);
        if(mask&UINT32_C(0xFFFF0000))publish_source_batch(g,1,mask,now);
    }
    record_source_cycles(cycles);
}


static SRNOS_SirinLinkResult enqueue_work(work_item_t *work)
{
    /* The caller owns this local item; the queue copies it before returning. */
    work->session=s_session;
    if ((s_work_queue == NULL) ||
        (osMessageQueuePut(s_work_queue, work, 0u, 0u) != osOK)) {
        return SRNOS_SIRIN_LINK_ERR_FULL;
    }
    const uint32_t depth = osMessageQueueGetCount(s_work_queue);
    if (depth > s_deferred_high_water) {
        s_deferred_high_water = depth;
    }
    return SRNOS_SIRIN_LINK_OK;
}

static uint16_t handle_exec(void *user,
                            const SRNOS_ProtocolView *request,
                            uint8_t *response,
                            size_t response_cap,
                            size_t *response_len,
                            uint8_t *response_flags)
{
    (void)user;
    (void)response;
    (void)response_cap;
    *response_len = 0u;
    *response_flags = 0u;
    if ((request->payload_len < 2u) ||
        ((request->payload_len - 2u) > SRNOS_SIRIN_LINK_WORK_PAYLOAD_MAX)) {
        return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    }
    uint16_t command_id = 0u;
    if (!SRNOS_Endian_LoadLE16(
            SRNOS_Memory_Bytes(request->payload, request->payload_len),
            0u, &command_id)) return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    if (find_exec(command_id) == NULL) {
        return SRNOS_SIRIN_LINK_STATUS_NOT_FOUND;
    }
    work_item_t work;
    memset(&work, 0, sizeof(work));
    work.kind = SRNOS_SIRIN_LINK_WORK_EXEC;
    work.service = SRNOS_SIRIN_LINK_SERVICE_EXEC;
    work.message = request->header.message;
    work.command_id = command_id;
    work.sequence = request->header.sequence;
    work.payload_len = (uint16_t)(request->payload_len - 2u);
    if (work.payload_len != 0u) {
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(work.payload,
                                                     sizeof(work.payload)),
                             SRNOS_Memory_Bytes(&request->payload[2],
                                             work.payload_len));
    }
    return (enqueue_work(&work) == SRNOS_SIRIN_LINK_OK) ?
        SRNOS_SIRIN_LINK_STATUS_DEFERRED_INTERNAL : SRNOS_SIRIN_LINK_STATUS_BUSY;
}

static uint16_t handle_subscribe(void *user,
                                 const SRNOS_ProtocolView *request,
                                 uint8_t *response,
                                 size_t response_cap,
                                 size_t *response_len,
                                 uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if ((request->payload_len != 7u) || (response_cap < 7u)) {
        return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint16_t stream_id = 0u;
    uint32_t period_us = 0u;
    if (!SRNOS_Endian_LoadLE16(payload, 0u, &stream_id) ||
        !SRNOS_Endian_LoadLE32(payload, 2u, &period_us)) {
        return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    }
    const bool enabled = request->payload[6] != 0u;
    telemetry_entry_t *entry = find_telemetry(stream_id);
    if (entry == NULL) {
        return SRNOS_SIRIN_LINK_STATUS_NOT_FOUND;
    }
    if (request->payload[6] > 1u || (enabled && entry->extended.required_period_us &&
        period_us != entry->extended.required_period_us)) return SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT;
    if(entry->extended.validate) {
        uint16_t result=entry->extended.validate(entry->user,enabled,period_us);
        if(result!=SRNOS_SIRIN_LINK_STATUS_OK) return result;
    }
    (void)SRNOS_SirinLinkCoreDiscardStream(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SAMPLE,stream_id);
    entry->enabled = enabled ? 1u : 0u;
    entry->period_us = period_us;
    entry->next_poll_ms=link_now_ms(NULL);
    if (entry->on_config != NULL) {
        entry->on_config(entry->user, enabled, period_us);
    }
    (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(response, response_cap),
                         SRNOS_Memory_Bytes(request->payload,
                                         request->payload_len));
    *response_len = request->payload_len;
    return SRNOS_SIRIN_LINK_STATUS_OK;
}

static SRNOS_SirinLinkResult start_link(void)
{
    const SRNOS_SirinLinkCoreConfig link_config = {
        .now_ms = link_now_ms,
        .cycles = (s_config.cycles != NULL) ? link_cycles : NULL,
        .tx_submit = transport_submit,
        .lock = link_lock,
        .unlock = link_unlock,
        .tx_storage = s_config.tx_storage,
        .tx_storage_size = s_config.tx_storage_size,
        .user = NULL,
    };
    SRNOS_SirinLinkResult result = SRNOS_SirinLinkCoreInit(&s_link, &link_config);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_EXEC, SRNOS_SIRIN_LINK_EXEC_MESSAGE_CALL,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_exec, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY, SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SUBSCRIBE,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_subscribe, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterTxTopic(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY, SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SAMPLE,
        SRNOS_SIRIN_LINK_QOS_TELEMETRY);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_LIST,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_source_list, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_source_subscribe, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_source_cancel, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterHandler(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL,
        SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_SIRIN_LINK_QOS_RELIABLE, handle_source_cancel_all, NULL);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    result = SRNOS_SirinLinkCoreRegisterTxTopic(
        &s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA, SRNOS_SIRIN_LINK_QOS_TELEMETRY);
    if (result != SRNOS_SIRIN_LINK_OK) {
        return result;
    }
    const struct {uint8_t message; SRNOS_SirinLinkHandlerFn handler;} source_v2_routes[]={
        {SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_LIST_V2,handle_source_list},
        {SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE_V2,handle_source_subscribe},
        {SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_V2,handle_source_cancel},
        {SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL_V2,handle_source_cancel_all}};
    for(unsigned i=0;i<sizeof(source_v2_routes)/sizeof(source_v2_routes[0]);++i) {
        result=SRNOS_SirinLinkCoreRegisterHandler(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
            source_v2_routes[i].message,SRNOS_SIRIN_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
            SRNOS_SIRIN_LINK_QOS_RELIABLE,source_v2_routes[i].handler,NULL);
        if(result!=SRNOS_SIRIN_LINK_OK)return result;
    }
    result=SRNOS_SirinLinkCoreRegisterTxTopic(&s_link,SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
        SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SOURCE_DATA_V2,SRNOS_SIRIN_LINK_QOS_TELEMETRY);
    if(result!=SRNOS_SIRIN_LINK_OK)return result;
    if (s_config.project_setup != NULL) {
        s_registration=(SRNOS_SirinLinkRegistration){&s_link,true};
        result = s_config.project_setup(&s_registration, s_config.project_user);
        s_registration.active=false;
        if (result != SRNOS_SIRIN_LINK_OK) {
            return result;
        }
    }
    if(!SRNOS_TelemetryFreeze())return SRNOS_SIRIN_LINK_ERR_PARAM;
    (void)freeze_source_catalog();
    for (size_t i = 0u; i < SRNOS_SIRIN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if (s_control[i].used != 0u) {
            result = SRNOS_SirinLinkCoreControlRegister(&s_link,
                                               s_control[i].channel,
                                               s_control[i].storage,
                                               s_control[i].capacity);
            if (result != SRNOS_SIRIN_LINK_OK) {
                return result;
            }
        }
    }
    s_link.registrations_frozen = 1u;
    s_link_ready = 1u;
    s_started = 1u;
    s_last_snapshot = link_now_ms(NULL);
    s_source_last_poll_ms = s_last_snapshot;
    return SRNOS_SIRIN_LINK_OK;
}

static void publish_snapshot(void)
{
    if (s_config.snapshot == NULL) {
        return;
    }
    SRNOS_SirinLinkSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    SRNOS_SirinLinkCoreGetStats(&s_link, &snapshot.link);
    snapshot.sources = s_source_stats;
    /* Not measured: SystemMonitor intentionally performs no periodic stack
     * scanning. UINT32_MAX cannot be confused with a zero remaining stack. */
    snapshot.comm_stack_high_water_words = UINT32_MAX;
    snapshot.worker_stack_high_water_words = UINT32_MAX;
    snapshot.deferred_depth = (s_work_queue != NULL) ?
        osMessageQueueGetCount(s_work_queue) : 0u;
    snapshot.deferred_high_water = s_deferred_high_water;
    snapshot.started = s_started;
    s_config.snapshot(&snapshot, s_config.project_user);
}

static void poll_once(void)
{
    const bool connected =
        s_config.transport.is_connected(s_config.transport.user);
    uint32_t generation=s_config.transport.session_generation ?
        s_config.transport.session_generation(s_config.transport.user) : s_transport_generation;
    if (((s_transport_connected != 0u) && !connected) || generation!=s_transport_generation) {
        clear_all_telemetry_subscriptions();
        SRNOS_SirinLinkCoreResetSession(&s_link); ++s_session;
        s_completion_pending=false;
    }
    s_transport_generation=generation;
    s_transport_connected = connected ? 1u : 0u;
    SRNOS_SirinLinkCoreSetTransportConnected(&s_link, connected);
    bool tx_success = false;
    while (s_config.transport.tx_take_complete(s_config.transport.user,
                                               &tx_success)) {
        SRNOS_SirinLinkCoreOnTxComplete(&s_link, tx_success);
    }
    SRNOS_SirinLinkCoreDrainDeliveries(&s_link);
    const uint32_t rx_overflow =
        s_config.transport.rx_overflow_bytes(s_config.transport.user);
    SRNOS_SirinLinkCoreRecordRxOverflow(&s_link, rx_overflow - s_last_rx_overflow);
    if(rx_overflow != s_last_rx_overflow) SRNOS_SirinLinkCoreResetRx(&s_link);
    s_last_rx_overflow = rx_overflow;
    size_t rx_budget=SRNOS_SIRIN_LINK_RX_BUDGET_BYTES;
    while(rx_budget) {
        const size_t capacity=rx_budget<s_config.rx_drain_buffer_size ? rx_budget:s_config.rx_drain_buffer_size;
        const size_t count=s_config.transport.rx_read(s_config.transport.user,s_config.rx_drain_buffer,capacity);
        if(!count)break;
        if(count>capacity) { SRNOS_SirinLinkCoreResetRx(&s_link);break; }
        (void)SRNOS_SirinLinkCoreFeedRx(&s_link,s_config.rx_drain_buffer,count);
        rx_budget-=count;
    }
    s_rx_budget_exhausted=rx_budget==0;

    /* The consumer owns this extra slot until TX accepts it. Never put a
     * completion back into a queue that the producer may have refilled. */
    for(unsigned n=0;n<SRNOS_SIRIN_LINK_COMPLETION_DEPTH+1u;n++) {
        if(!s_completion_pending) {
            if(osMessageQueueGet(s_completion_queue,&s_pending_completion,NULL,0u)!=osOK)break;
            s_completion_pending=true;
        }
        if(s_pending_completion.session!=s_session) {s_completion_pending=false;continue;}
        const completion_t *c=&s_pending_completion;
        if(SRNOS_SirinLinkCoreSendResponse(&s_link,c->service,c->message,c->sequence,
            c->status,c->flags,c->body,c->body_len)!=SRNOS_SIRIN_LINK_OK)break;
        s_completion_pending=false;
    }
    SRNOS_SirinLinkCoreDrainDeliveries(&s_link);
    if (connected) {
        (void)SRNOS_SirinLinkCoreServiceTx(&s_link);
        poll_telemetry_sources(link_now_ms(NULL));
        /* At most one bounded pump per registered stream per iteration. */
        for(unsigned n=0;n<SRNOS_SIRIN_LINK_TELEMETRY_COUNT;n++) {
            telemetry_entry_t *e=&s_telemetry[(s_poll_cursor+n)%SRNOS_SIRIN_LINK_TELEMETRY_COUNT];
            const uint32_t now=link_now_ms(NULL);
            if(e->used && e->enabled && e->extended.poll && (int32_t)(now-e->next_poll_ms)>=0) {
                e->extended.poll(e->user,now);
                e->next_poll_ms=now+telemetry_poll_interval(e);
            }
        }
        s_poll_cursor=(s_poll_cursor+1)%SRNOS_SIRIN_LINK_TELEMETRY_COUNT;
        (void)SRNOS_SirinLinkCoreServiceTx(&s_link);
    } else {
        s_source_last_poll_ms = link_now_ms(NULL);
    }
}

static void publish_snapshot_if_due(void)
{
    const uint32_t now = link_now_ms(NULL);
    if ((now - s_last_snapshot) >= s_config.snapshot_period_ms) {
        publish_snapshot();
        s_last_snapshot = now;
    }
}

static bool worker_process(const work_item_t *work)
{
    if(work->session!=s_session) return true;
    completion_t completion;
    memset(&completion, 0, sizeof(completion));
    completion.session=work->session;
    completion.service = work->service;
    completion.message = work->message;
    completion.sequence = work->sequence;
    size_t body_len = 0u;
    if (work->kind == SRNOS_SIRIN_LINK_WORK_EXEC) {
        exec_entry_t *entry = find_exec(work->command_id);
        if ((entry == NULL) || (entry->handler == NULL)) {
            completion.status = SRNOS_SIRIN_LINK_STATUS_NOT_FOUND;
        } else {
            completion.status = entry->handler(
                entry->user, work->payload, work->payload_len,
                completion.body, sizeof(completion.body), &body_len);
        }
    } else if (work->deferred != NULL) {
        completion.status = work->deferred(
            work->user, work->service, work->message,
            work->payload, work->payload_len,
            completion.body, sizeof(completion.body),
            &body_len, &completion.flags);
    } else {
        completion.status = SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
    }
    if ((body_len > sizeof(completion.body)) ||
        ((completion.flags & (uint8_t)~SRNOS_PROTOCOL_VALID_FLAGS) != 0u)) {
        completion.status = SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
        body_len = 0u;
        completion.flags = 0u;
    }
    completion.body_len = (uint16_t)body_len;
    (void)osMessageQueuePut(s_completion_queue,
                            &completion, 0u, osWaitForever);
    if (s_comm_task != NULL) {
        xTaskNotifyGive(s_comm_task);
    }
    return true;

}

#ifdef SRNOS_SIRIN_LINK_TESTING
static bool worker_once(uint32_t timeout)
{
 work_item_t work;if(osMessageQueueGet(s_work_queue,&work,NULL,timeout)!=osOK)return false;
 return worker_process(&work);
}
#endif


SRNOS_SirinLinkResult SRNOS_SirinLinkConfigure(
    const SRNOS_SirinLinkConfig *config)
{
    if ((config == NULL) || (s_configured != 0u) ||
        (config->now_ms == NULL) ||
        (config->tx_storage == NULL) ||
        (((uintptr_t)config->tx_storage & 31u) != 0u) ||
        (config->tx_storage_size <
         (SRNOS_SIRIN_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE)) ||
        (config->rx_drain_buffer == NULL) ||
        (((uintptr_t)config->rx_drain_buffer & 31u) != 0u) ||
        (config->rx_drain_buffer_size == 0u) ||
        (config->transport.init == NULL) ||
        (config->transport.start_rx == NULL) ||
        (config->transport.rx_read == NULL) ||
        (config->transport.rx_overflow_bytes == NULL) ||
        (config->transport.tx_submit == NULL) ||
        (config->transport.tx_take_complete == NULL) ||
        (config->transport.is_connected == NULL)) {
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    }
    s_config = *config;
    if (s_config.task_wait_ms == 0u) {
        s_config.task_wait_ms = SRNOS_SIRIN_LINK_DEFAULT_WAIT_MS;
    }
    if (s_config.snapshot_period_ms == 0u) {
        s_config.snapshot_period_ms = SRNOS_SIRIN_LINK_DEFAULT_SNAPSHOT_MS;
    }
    s_configured = 1u;
    return SRNOS_SIRIN_LINK_OK;
}

bool SRNOS_SirinLinkInitResources(void)
{
    if (s_configured == 0u) {
        return false;
    }
    if (s_work_queue == NULL) {
        s_work_queue = osMessageQueueNew(SRNOS_SIRIN_LINK_WORK_DEPTH,
                                         sizeof(work_item_t), NULL);
    }
    if (s_completion_queue == NULL) {
        s_completion_queue = osMessageQueueNew(SRNOS_SIRIN_LINK_COMPLETION_DEPTH,
                                               sizeof(completion_t), NULL);
    }
    return (s_work_queue != NULL) && (s_completion_queue != NULL);
}

SRNOS_SirinLinkResult SRNOS_SirinLinkPrepareLink(void)
{
    if (!s_configured || s_started) return SRNOS_SIRIN_LINK_ERR_BUSY;
    return start_link();
}

static uint32_t wait_to_deadline(uint32_t now,uint32_t deadline)
{ return (int32_t)(deadline-now)>0 ? deadline-now : 0u; }
static uint32_t next_wait_ms(void)
{
    const uint32_t now=link_now_ms(NULL);
    uint32_t wait=wait_to_deadline(now,s_last_snapshot+s_config.snapshot_period_ms);
    if(s_transport_connected) {
        for(unsigned i=0;i<SRNOS_SIRIN_LINK_TELEMETRY_COUNT;i++) {
            const telemetry_entry_t *entry=&s_telemetry[i];
            if(!entry->used || !entry->enabled || !entry->extended.poll)continue;
            uint32_t due=wait_to_deadline(now,entry->next_poll_ms);if(due<wait)wait=due;
        }
        for(unsigned i=0;i<SRNOS_SIRIN_LINK_SOURCE_RATE_GROUPS;i++) {
            const telemetry_rate_group_t *group=&s_source_groups[i];
            if(!group->active_mask)continue;
            uint32_t rate=group->rate_hz;
            uint32_t interval=(1000u-group->phase_milli+rate-1u)/rate;
            uint32_t due=wait_to_deadline(now,s_source_last_poll_ms+interval);if(due<wait)wait=due;
        }
        if(s_source_immediate_mask)wait=0;
        if(s_link.in_flight<0 && (s_completion_pending || SRNOS_SirinLinkCoreHasPendingTx(&s_link)) &&
            s_config.task_wait_ms<wait)wait=s_config.task_wait_ms;
    }
    return wait;
}
static bool comm_wait(void *arg)
{
    (void)arg;
    if(s_rx_budget_exhausted) { (void)osDelay(1u);return true; }
    uint32_t notifications=0;
    (void)xTaskNotifyWait(0u,UINT32_MAX,&notifications,pdMS_TO_TICKS(next_wait_ms()));
    return true;
}
static void comm_step(void *arg){(void)arg;poll_once();publish_snapshot_if_due();}
static bool command_wait(void *arg){return osMessageQueueGet(s_work_queue,arg,NULL,osWaitForever)==osOK;}
static void command_step(void *arg){(void)worker_process((const work_item_t *)arg);}
void SRNOS_SirinLinkTask(void *argument)
{
    (void)argument;
    SRNOS_ASSERT(SRNOS_SirinLinkInitResources());
    s_comm_task = xTaskGetCurrentTaskHandle();
    if (!s_started) SRNOS_ASSERT(start_link() == SRNOS_SIRIN_LINK_OK);
    SRNOS_ASSERT(s_config.transport.init(s_config.transport.user));
    SRNOS_ASSERT(s_config.transport.start_rx(s_config.transport.user, s_comm_task, SRNOS_SIRIN_LINK_NOTIFY_RX));

    const SRNOS_InternalLoop loop={comm_step,comm_wait,NULL,0,true};
    SRNOS_KernelRunLoop(&loop);
}

void SRNOS_SirinLinkWorkerTask(void *argument)
{
    (void)argument;
    SRNOS_ASSERT(SRNOS_SirinLinkInitResources());
    s_worker_task = xTaskGetCurrentTaskHandle();
    work_item_t work;
    const SRNOS_InternalLoop loop={command_step,command_wait,&work,0,true};
    SRNOS_KernelRunLoop(&loop);
}

SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterHandler(SRNOS_SirinLinkRegistration *registration,
    uint8_t service, uint8_t message, uint8_t kind_mask, SRNOS_SirinLinkQos qos,
    SRNOS_SirinLinkHandlerFn handler, void *user)
{
    if (registration != &s_registration || !s_registration.active || s_started)
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    if (service == SRNOS_SIRIN_LINK_SERVICE_SYSTEM ||
        (service == SRNOS_SIRIN_LINK_SERVICE_CONTROL && message == SRNOS_SIRIN_LINK_CONTROL_STATE))
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    return SRNOS_SirinLinkCoreRegisterHandler(registration->context,service,message,kind_mask,qos,handler,user);
}
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTxTopic(SRNOS_SirinLinkRegistration *registration,
    uint8_t service, uint8_t message, SRNOS_SirinLinkQos qos)
{
    if (registration != &s_registration || !s_registration.active || s_started)
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    return SRNOS_SirinLinkCoreRegisterTxTopic(registration->context,service,message,qos);
}

SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterExec(
    uint16_t command_id,
    SRNOS_SirinLinkExecFn handler,
    void *user)
{
    if (s_started) return SRNOS_SIRIN_LINK_ERR_BUSY;
    if ((s_configured == 0u) || (handler == NULL)) {
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    }
    SRNOS_SIRIN_LINK_ENTER_CRITICAL();
    for (size_t i = 0u; i < SRNOS_SIRIN_LINK_EXEC_COUNT; ++i) {
        if ((s_exec[i].used != 0u) && (s_exec[i].command_id == command_id)) {
            SRNOS_SIRIN_LINK_EXIT_CRITICAL();
            return SRNOS_SIRIN_LINK_ERR_PARAM;
        }
        if (s_exec[i].used == 0u) {
            s_exec[i].used = 1u;
            s_exec[i].command_id = command_id;
            s_exec[i].handler = handler;
            s_exec[i].user = user;
            SRNOS_SIRIN_LINK_EXIT_CRITICAL();
            return SRNOS_SIRIN_LINK_OK;
        }
    }
    SRNOS_SIRIN_LINK_EXIT_CRITICAL();
    return SRNOS_SIRIN_LINK_ERR_FULL;
}

SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTelemetryEx(const SRNOS_SirinLinkTelemetryConfig *c)
{
    if(s_started) return SRNOS_SIRIN_LINK_ERR_BUSY;
    if(!s_configured || !c || !c->max_sample_size || c->max_sample_size>SRNOS_PROTOCOL_MAX_PAYLOAD-14u ||
       (c->policy!=SRNOS_SIRIN_LINK_LATEST && c->policy!=SRNOS_SIRIN_LINK_FIFO) ||
       (c->on_delivery && c->policy!=SRNOS_SIRIN_LINK_FIFO) || c->poll_interval_ms>INT32_MAX) return SRNOS_SIRIN_LINK_ERR_PARAM;
    if(find_telemetry(c->stream_id)) return SRNOS_SIRIN_LINK_ERR_PARAM;
    for(unsigned i=0;i<SRNOS_SIRIN_LINK_TELEMETRY_COUNT;i++) if(!s_telemetry[i].used) {
        s_telemetry[i]=(telemetry_entry_t){.used=1,.stream_id=c->stream_id,
            .max_sample_size=c->max_sample_size,.extended=*c,.on_config=c->on_config,.user=c->user};
        return SRNOS_SIRIN_LINK_OK;
    }
    return SRNOS_SIRIN_LINK_ERR_FULL;
}
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTelemetry(uint16_t id,uint16_t size,
    SRNOS_SirinLinkTelemetryConfigFn callback,void *user)
{
    const SRNOS_SirinLinkTelemetryConfig c={.stream_id=id,.max_sample_size=size,
        .on_config=callback,.user=user,.policy=SRNOS_SIRIN_LINK_LATEST};
    return SRNOS_SirinLinkRegisterTelemetryEx(&c);
}

SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterControl(uint16_t channel,
                                                  uint8_t *storage,
                                                  size_t capacity)
{
    if (s_started) return SRNOS_SIRIN_LINK_ERR_BUSY;
    if ((s_configured == 0u) || (storage == NULL) || (capacity == 0u) ||
        (capacity > (SRNOS_PROTOCOL_MAX_PAYLOAD - 10u))) {
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    }
    SRNOS_SIRIN_LINK_ENTER_CRITICAL();
    for (size_t i = 0u; i < SRNOS_SIRIN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if ((s_control[i].used != 0u) && (s_control[i].channel == channel)) {
            SRNOS_SIRIN_LINK_EXIT_CRITICAL();
            return SRNOS_SIRIN_LINK_ERR_PARAM;
        }
        if (s_control[i].used == 0u) {
            s_control[i].used = 1u;
            s_control[i].channel = channel;
            s_control[i].storage = storage;
            s_control[i].capacity = capacity;
            SRNOS_SIRIN_LINK_EXIT_CRITICAL();
            if (s_link_ready != 0u) {
                const SRNOS_SirinLinkResult result =
                    SRNOS_SirinLinkCoreControlRegister(&s_link,
                                              channel, storage, capacity);
                if (result != SRNOS_SIRIN_LINK_OK) {
                    SRNOS_SIRIN_LINK_ENTER_CRITICAL();
                    memset(&s_control[i], 0, sizeof(s_control[i]));
                    SRNOS_SIRIN_LINK_EXIT_CRITICAL();
                }
                return result;
            }
            return SRNOS_SIRIN_LINK_OK;
        }
    }
    SRNOS_SIRIN_LINK_EXIT_CRITICAL();
    return SRNOS_SIRIN_LINK_ERR_FULL;
}

SRNOS_SirinLinkResult SRNOS_SirinLinkPublishTelemetry(
    uint16_t stream_id,
    uint32_t sample_sequence,
    uint32_t device_time_us,
    uint16_t sample_period_us,
    uint8_t sample_count,
    const uint8_t *samples,
    size_t samples_len)
{
    telemetry_entry_t *entry = find_telemetry(stream_id);
    if (s_link_ready == 0u) {
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    }
    if (entry == NULL) {
        return SRNOS_SIRIN_LINK_ERR_NOT_FOUND;
    }
    if ((entry->enabled == 0u) || (sample_count == 0u) || (samples == NULL) ||
        (samples_len > entry->max_sample_size) ||
        ((samples_len + 14u) > SRNOS_PROTOCOL_MAX_PAYLOAD) ||
        ((samples_len % sample_count) != 0u) ||
        ((samples_len / sample_count) > UINT8_MAX)) {
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    }
    uint8_t payload[SRNOS_PROTOCOL_MAX_PAYLOAD];
    SRNOS_Memory_MutableByteSpan payload_span =
        SRNOS_Memory_MutableBytes(payload, sizeof(payload));
    (void)SRNOS_Endian_StoreLE16(payload_span, 0u, stream_id);
    (void)SRNOS_Endian_StoreLE32(payload_span, 2u, sample_sequence);
    (void)SRNOS_Endian_StoreLE32(payload_span, 6u, device_time_us);
    (void)SRNOS_Endian_StoreLE16(payload_span, 10u, sample_period_us);
    payload[12] = sample_count;
    payload[13] = (uint8_t)(samples_len / sample_count);
    (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(&payload[14],
                                                 sizeof(payload) - 14u),
                         SRNOS_Memory_Bytes(samples, samples_len));
    const SRNOS_SirinLinkResult result=SRNOS_SirinLinkCorePublishEx(&s_link, SRNOS_SIRIN_LINK_SERVICE_TELEMETRY,
                            SRNOS_SIRIN_LINK_TELEMETRY_MESSAGE_SAMPLE, stream_id,
                            sample_sequence, payload, samples_len + 14u,
                            entry->extended.policy,entry->extended.on_delivery,entry->user,sample_count);
    if(result==SRNOS_SIRIN_LINK_OK)wake_comm();
    return result;
}

SRNOS_SirinLinkResult SRNOS_SirinLinkTakeControl(uint16_t channel,
                                              uint8_t *out,
                                              size_t out_cap,
                                              size_t *out_len,
                                              uint32_t *generation,
                                              uint32_t *deadline_us,
                                              uint32_t *received_ms)
{
    if (s_link_ready == 0u) {
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    }
    return SRNOS_SirinLinkCoreControlTake(&s_link, channel, out, out_cap, out_len,
                                 generation, deadline_us, received_ms);
}

SRNOS_SirinLinkResult SRNOS_SirinLinkSubmitLog(const uint8_t *line,
                                            size_t length)
{
    if (s_link_ready == 0u) {
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    }
    const SRNOS_SirinLinkResult result=SRNOS_SirinLinkCoreSubmitLog(&s_link,line,length);
    if(result==SRNOS_SIRIN_LINK_OK)wake_comm();return result;
}

SRNOS_SirinLinkResult SRNOS_SirinLinkPublishEvent(
    uint8_t service,
    uint8_t message,
    uint16_t topic,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len)
{
    if (s_link_ready == 0u) {
        return SRNOS_SIRIN_LINK_ERR_BUSY;
    }
    const SRNOS_SirinLinkResult result=SRNOS_SirinLinkCorePublish(&s_link,service,message,topic,sequence,payload,payload_len);
    if(result==SRNOS_SIRIN_LINK_OK)wake_comm();return result;
}

SRNOS_SirinLinkResult SRNOS_SirinLinkSubmitDeferred(
    uint8_t service,
    uint8_t message,
    uint32_t sequence,
    const uint8_t *request,
    size_t request_len,
    SRNOS_SirinLinkDeferredFn handler,
    void *user)
{
    if ((s_configured == 0u) || (handler == NULL) ||
        ((request == NULL) && (request_len != 0u)) ||
        (request_len > SRNOS_SIRIN_LINK_WORK_PAYLOAD_MAX)) {
        return SRNOS_SIRIN_LINK_ERR_PARAM;
    }
    if(xTaskGetCurrentTaskHandle()!=s_comm_task ||
        !SRNOS_SirinLinkCoreCanDefer(&s_link,service,message,sequence))return SRNOS_SIRIN_LINK_ERR_BUSY;
    work_item_t work;
    memset(&work, 0, sizeof(work));
    work.kind = SRNOS_SIRIN_LINK_WORK_DEFERRED;
    work.service = service;
    work.message = message;
    work.sequence = sequence;
    work.payload_len = (uint16_t)request_len;
    work.deferred = handler;
    work.user = user;
    if (request_len != 0u) {
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(work.payload,
                                                     sizeof(work.payload)),
                             SRNOS_Memory_Bytes(request, request_len));
    }
    const SRNOS_SirinLinkResult result=enqueue_work(&work);
    if(result==SRNOS_SIRIN_LINK_OK)SRNOS_SirinLinkCoreMarkDeferred(&s_link);
    return result;
}

#if defined(SRNOS_SIRIN_LINK_TESTING)
SRNOS_SirinLinkResult SRNOS_SirinLinkTestStart(void)
{
    if (!SRNOS_SirinLinkInitResources()) {
        return SRNOS_SIRIN_LINK_ERR_FULL;
    }
    s_comm_task = xTaskGetCurrentTaskHandle();
    SRNOS_SirinLinkResult result = start_link();
    if (result == SRNOS_SIRIN_LINK_OK && !s_config.transport.init(s_config.transport.user)) return SRNOS_SIRIN_LINK_ERR_TRANSPORT;
    if (result == SRNOS_SIRIN_LINK_OK && !s_config.transport.start_rx(s_config.transport.user, s_comm_task, SRNOS_SIRIN_LINK_NOTIFY_RX))
        return SRNOS_SIRIN_LINK_ERR_TRANSPORT;
    return result;
}

uint32_t SRNOS_SirinLinkTestNextWaitMs(void) { return next_wait_ms(); }
bool SRNOS_SirinLinkTestPendingCompletion(void) { return s_completion_pending; }
bool SRNOS_SirinLinkTestRxBudgetExhausted(void) { return s_rx_budget_exhausted; }
void SRNOS_SirinLinkTestPollOnce(void)
{
    poll_once();
    publish_snapshot_if_due();
}

bool SRNOS_SirinLinkTestWorkerOnce(void)
{
    s_worker_task = xTaskGetCurrentTaskHandle();
    return worker_once(0u);
}
#endif
