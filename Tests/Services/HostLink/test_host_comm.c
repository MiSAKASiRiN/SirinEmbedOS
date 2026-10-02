#include "SirinEmbedOS/Services/Telemetry.h"
#include "host_comm_internal.h"
#include "SirinEmbedOS/Services/HostLink/HostLink.h"

#include "cmsis_os2.h"
#include "SirinEmbedOS/Services/HostLink/Codec/COBS.h"
#include "SirinEmbedOS/Services/HostLink/Codec/Protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t test_load_le16(const uint8_t *data)
{
    uint16_t value = 0u;
    assert(SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(data, 2u), 0u, &value));
    return value;
}

static uint32_t test_load_le32(const uint8_t *data)
{
    uint32_t value = 0u;
    assert(SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(data, 4u), 0u, &value));
    return value;
}

static void test_store_le16(uint8_t *data, uint16_t value)
{
    assert(SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(data, 2u), 0u, value));
}

static void test_store_le32(uint8_t *data, uint32_t value)
{
    assert(SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(data, 4u), 0u, value));
}


#define SRNOS_TEST_HOST_LINK_QUEUE_COUNT       2u
#define SRNOS_TEST_HOST_LINK_QUEUE_DEPTH       4u
#define SRNOS_TEST_HOST_LINK_QUEUE_ITEM_MAX    512u
#define SRNOS_TEST_HOST_LINK_RX_CAPACITY       4096u

struct test_message_queue {
    uint32_t capacity;
    uint32_t item_size;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    uint8_t data[SRNOS_TEST_HOST_LINK_QUEUE_DEPTH][SRNOS_TEST_HOST_LINK_QUEUE_ITEM_MAX];
};

typedef struct {
    uint32_t now_ms;
    uint32_t cycles;
    uint32_t overflow;
    uint32_t rx_read_calls;
    uint32_t setup_calls;
    uint32_t snapshot_calls;
    uint32_t exec_calls;
    uint32_t notify_calls;
    bool initialized;
    bool rx_started;
    bool connected;
    bool tx_in_use;
    bool completion_pending;
    bool completion_success;
    bool telemetry_enabled;
    bool source_getter_available;
    uint32_t telemetry_period_us;
    float source_float;
    int32_t source_i32;
    bool source_bool;
    uint32_t source_values[SRNOS_HOST_LINK_SOURCE_COUNT_MAX];
    char source_names[SRNOS_HOST_LINK_SOURCE_COUNT_MAX][16];
    SRNOS_HostLinkCoreTxResult tx_result;
    const uint8_t *tx_pointer;
    size_t tx_length;
    uint8_t rx[SRNOS_TEST_HOST_LINK_RX_CAPACITY];
    size_t rx_length;
    size_t rx_offset;
    size_t rx_chunk;
    SRNOS_HostLinkSnapshot snapshot;
    SRNOS_HOST_LINK_ALIGN32 uint8_t tx_storage[
        SRNOS_HOST_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    SRNOS_HOST_LINK_ALIGN32 uint8_t rx_drain[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
} fixture_t;

static struct test_message_queue s_queues[SRNOS_TEST_HOST_LINK_QUEUE_COUNT];
static uint32_t s_queue_count;
static TaskHandle_t s_current_task;
static uint32_t s_critical_depth;

void test_task_enter_critical(void)
{
    s_critical_depth++;
}

void test_task_exit_critical(void)
{
    assert(s_critical_depth != 0u);
    s_critical_depth--;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return s_current_task;
}

uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    return (task == (TaskHandle_t)(uintptr_t)1u) ? 321u : 123u;
}

BaseType_t xTaskNotifyWait(uint32_t clear_on_entry,
                           uint32_t clear_on_exit,
                           uint32_t *value,
                           TickType_t timeout)
{
    (void)clear_on_entry;
    (void)clear_on_exit;
    (void)timeout;
    if (value != NULL) {
        *value = 0u;
    }
    return pdFALSE;
}

void xTaskNotifyGive(TaskHandle_t task)
{
    fixture_t *fixture = (fixture_t *)task;
    (void)fixture;
}

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count,
                                     uint32_t msg_size,
                                     const void *attr)
{
    (void)attr;
    assert(s_queue_count < SRNOS_TEST_HOST_LINK_QUEUE_COUNT);
    assert(msg_count <= SRNOS_TEST_HOST_LINK_QUEUE_DEPTH);
    assert(msg_size <= SRNOS_TEST_HOST_LINK_QUEUE_ITEM_MAX);
    struct test_message_queue *queue = &s_queues[s_queue_count++];
    memset(queue, 0, sizeof(*queue));
    queue->capacity = msg_count;
    queue->item_size = msg_size;
    return queue;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t queue,
                             const void *msg,
                             uint8_t priority,
                             uint32_t timeout)
{
    (void)priority;
    (void)timeout;
    if ((queue == NULL) || (queue->count == queue->capacity)) {
        return osErrorResource;
    }
    memcpy(queue->data[queue->tail], msg, queue->item_size);
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->count++;
    return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t queue,
                             void *msg,
                             uint8_t *priority,
                             uint32_t timeout)
{
    (void)priority;
    (void)timeout;
    if ((queue == NULL) || (queue->count == 0u)) {
        return osErrorResource;
    }
    memcpy(msg, queue->data[queue->head], queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    return osOK;
}

osStatus_t osDelay(uint32_t ticks) { (void)ticks;return osOK; }
uint32_t osMessageQueueGetCount(osMessageQueueId_t queue)
{
    return (queue != NULL) ? queue->count : 0u;
}

static uint32_t test_now(void *user)
{
    return ((fixture_t *)user)->now_ms;
}

static uint32_t test_cycles(void *user)
{
    fixture_t *fixture = user;
    fixture->cycles += 13u;
    return fixture->cycles;
}

static bool transport_init(void *user)
{
    fixture_t *fixture = user;
    fixture->initialized = true;
    return true;
}

static bool transport_start_rx(void *user,
                               TaskHandle_t notify_task,
                               uint32_t notify_bits)
{
    fixture_t *fixture = user;
    assert(fixture->initialized);
    assert(fixture->setup_calls == 1u);
    assert(notify_task == (TaskHandle_t)(uintptr_t)1u);
    assert(notify_bits != 0u);
    fixture->rx_started = true;
    return true;
}

static size_t transport_read(void *user, uint8_t *out, size_t capacity)
{
    fixture_t *fixture = user;
    fixture->rx_read_calls++;
    if (fixture->rx_offset == fixture->rx_length) {
        return 0u;
    }
    size_t count = fixture->rx_length - fixture->rx_offset;
    if ((fixture->rx_chunk != 0u) && (count > fixture->rx_chunk)) {
        count = fixture->rx_chunk;
    }
    if (count > capacity) {
        count = capacity;
    }
    memcpy(out, &fixture->rx[fixture->rx_offset], count);
    fixture->rx_offset += count;
    return count;
}

static uint32_t transport_overflow(void *user)
{
    return ((fixture_t *)user)->overflow;
}

static SRNOS_HostLinkCoreTxResult transport_submit(void *user,
                                              const uint8_t *data,
                                              size_t length)
{
    fixture_t *fixture = user;
    if (fixture->tx_result == SRNOS_HOST_LINK_TX_ACCEPTED) {
        assert(!fixture->tx_in_use);
        fixture->tx_pointer = data;
        fixture->tx_length = length;
        fixture->tx_in_use = true;
    }
    return fixture->tx_result;
}

static bool transport_take_completion(void *user, bool *success)
{
    fixture_t *fixture = user;
    if (!fixture->completion_pending) {
        return false;
    }
    *success = fixture->completion_success;
    fixture->completion_pending = false;
    fixture->tx_in_use = false;
    return true;
}

static bool transport_connected(void *user)
{
    return ((fixture_t *)user)->connected;
}

static uint16_t defer_request(void *user,
                              const SRNOS_ProtocolView *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags);

static fixture_t *source_fixture;
static BaseType_t source_scheduler_state=taskSCHEDULER_NOT_STARTED;
BaseType_t xPortIsInsideInterrupt(void) { return pdFALSE; }
BaseType_t xTaskGetSchedulerState(void) { return source_scheduler_state; }
uint64_t SRNOS_TimeUs(void) { return source_fixture ? (uint64_t)source_fixture->now_ms*1000u : 0; }
static void test_poll_once(void)
{
    fixture_t *f=source_fixture;
    for(uint16_t i=0;i<SRNOS_HOST_LINK_SOURCE_COUNT_MAX;++i) {
        SRNOS_TelemetryValue value={0};bool valid=true;
        if(i==0){value.u32=f->now_ms;valid=f->source_getter_available;}
        else if(i==1)value.f32=f->source_float;
        else if(i==2)value.i32=f->source_i32;
        else if(i==3)value.boolean=f->source_bool;
        else value.u32=f->source_values[i];
        assert(SRNOS_TelemetryPublish(i,value,SRNOS_TimeUs()+i,valid)==SRNOS_TELEMETRY_OK);
    }
    SRNOS_HostLinkTestPollOnce();
}

static unsigned ex_polls, ex_configs;
static SRNOS_HostLinkRegistration *saved_registration;
static void ex_config(void *user,bool enabled,uint32_t period)
{ (void)user; (void)enabled; (void)period; ++ex_configs; }
static void ex_poll(void *user,uint32_t now) { (void)user; (void)now; ++ex_polls; }

static SRNOS_HostLinkResult project_setup(SRNOS_HostLinkRegistration *link, void *user)
{
    fixture_t *fixture = user;
    saved_registration=link;source_fixture=fixture;
    const SRNOS_HostLinkTelemetryConfig ex={.stream_id=0x100,.max_sample_size=432,
        .required_period_us=1000,.policy=SRNOS_HOST_LINK_FIFO,.on_config=ex_config,.poll=ex_poll};
    assert(SRNOS_HostLinkRegisterTelemetryEx(&ex)==SRNOS_HOST_LINK_OK);
    assert(!fixture->initialized); /* declarations precede transport start */
    assert(!fixture->rx_started);
    fixture->setup_calls++;
    SRNOS_TelemetryDescriptor source={.name="invalid",.type=(SRNOS_TelemetryType)99,.max_hz=1000};
    SRNOS_TelemetryHandle handle;
    assert(SRNOS_TelemetryRegister(&source,&handle)==SRNOS_TELEMETRY_INVALID);
    for(uint16_t i=0;i<SRNOS_HOST_LINK_SOURCE_COUNT_MAX;++i) {
        assert(snprintf(fixture->source_names[i],sizeof(fixture->source_names[i]),"source.%02u",(unsigned)i)>0);
        source=(SRNOS_TelemetryDescriptor){fixture->source_names[i],NULL,SRNOS_TELEMETRY_UINT32,1000};
        if(i==0)source.unit="ms";
        else if(i==1){source.unit="rad";source.type=SRNOS_TELEMETRY_FLOAT32;}
        else if(i==2)source.type=SRNOS_TELEMETRY_INT32;
        else if(i==3)source.type=SRNOS_TELEMETRY_BOOL;
        if(i==31)source.max_hz=200;
        assert(SRNOS_TelemetryRegister(&source,&handle)==SRNOS_TELEMETRY_OK && handle==i);
        if(i==0)assert(SRNOS_TelemetryRegister(&source,&handle)==SRNOS_TELEMETRY_INVALID);
    }
    source.name="overflow";
    assert(SRNOS_TelemetryRegister(&source,&handle)==SRNOS_TELEMETRY_FULL);

    SRNOS_HostLinkResult result = SRNOS_HostLinkRegisterTxTopic(
        link, SRNOS_HOST_LINK_SERVICE_TEST, 0x40u, SRNOS_HOST_LINK_QOS_TELEMETRY);
    if (result != SRNOS_HOST_LINK_OK) {
        return result;
    }
    assert(SRNOS_HostLinkRegisterTxTopic(link,SRNOS_HOST_LINK_SERVICE_TEST,0x40u,
        SRNOS_HOST_LINK_QOS_TELEMETRY)==SRNOS_HOST_LINK_ERR_PARAM);
    assert(SRNOS_HostLinkRegisterHandler(link,SRNOS_HOST_LINK_SERVICE_SYSTEM,1,1,
        SRNOS_HOST_LINK_QOS_RELIABLE,defer_request,user)==SRNOS_HOST_LINK_ERR_PARAM);
    return SRNOS_HostLinkRegisterHandler(
        link,
        SRNOS_HOST_LINK_SERVICE_TEST,
        0x41u,
        SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),
        SRNOS_HOST_LINK_QOS_RELIABLE,
        defer_request,
        user);
}

static void take_snapshot(const SRNOS_HostLinkSnapshot *snapshot,
                          void *user)
{
    fixture_t *fixture = user;
    fixture->snapshot_calls++;
    fixture->snapshot = *snapshot;
}

static uint16_t exec_handler(void *user,
                             const uint8_t *args,
                             size_t args_len,
                             uint8_t *response,
                             size_t response_cap,
                             size_t *response_len)
{
    fixture_t *fixture = user;
    fixture->exec_calls++;
    assert(args_len == 1u);
    assert(args[0] == 0x5Au);
    assert(response_cap >= 2u);
    response[0] = 0x4Fu;
    response[1] = 0x4Bu;
    *response_len = 2u;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static void telemetry_config(void *user, bool enabled, uint32_t period_us)
{
    fixture_t *fixture = user;
    fixture->telemetry_enabled = enabled;
    fixture->telemetry_period_us = period_us;
}

static uint16_t deferred_work(void *user,
                              uint8_t service,
                              uint8_t message,
                              const uint8_t *request,
                              size_t request_len,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags)
{
    (void)user;
    assert(service == SRNOS_HOST_LINK_SERVICE_TEST);
    assert(message == 0x41u);
    assert(request_len == 3u);
    assert(request[0] == 1u && request[1] == 2u && request[2] == 3u);
    assert(response_cap >= 1u);
    response[0] = 6u;
    *response_len = 1u;
    *response_flags = SRNOS_PROTOCOL_FLAG_MORE;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static uint16_t defer_request(void *user,
                              const SRNOS_ProtocolView *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags)
{
    (void)response;
    (void)response_cap;
    *response_len = 0u;
    *response_flags = 0u;
    assert(SRNOS_HostLinkSubmitDeferred(request->header.service, request->header.message,
        request->header.sequence + 1u, request->payload, request->payload_len,
        deferred_work, user) == SRNOS_HOST_LINK_ERR_BUSY);
    TaskHandle_t original_task=s_current_task;
    s_current_task=(TaskHandle_t)(uintptr_t)99u;
    assert(SRNOS_HostLinkSubmitDeferred(request->header.service, request->header.message,
        request->header.sequence, request->payload, request->payload_len,
        deferred_work, user) == SRNOS_HOST_LINK_ERR_BUSY);
    s_current_task=original_task;
    SRNOS_HostLinkResult result=SRNOS_HostLinkSubmitDeferred(
        request->header.service, request->header.message, request->header.sequence,
        request->payload, request->payload_len, deferred_work, user);
    if(result==SRNOS_HOST_LINK_OK) {
        assert(SRNOS_HostLinkSubmitDeferred(request->header.service, request->header.message,
            request->header.sequence, request->payload, request->payload_len,
            deferred_work, user) == SRNOS_HOST_LINK_ERR_BUSY);
        return SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL;
    }
    return SRNOS_HOST_LINK_STATUS_BUSY;
}

static void load_rx(fixture_t *fixture,
                    const uint8_t *data,
                    size_t length,
                    size_t chunk)
{
    assert(length <= sizeof(fixture->rx));
    memcpy(fixture->rx, data, length);
    fixture->rx_length = length;
    fixture->rx_offset = 0u;
    fixture->rx_chunk = chunk;
    fixture->rx_read_calls = 0u;
}

static size_t pack(uint8_t kind,
                   uint8_t service,
                   uint8_t message,
                   uint32_t sequence,
                   const uint8_t *payload,
                   size_t payload_len,
                   uint8_t *frame)
{
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(kind, 0u, service, message, sequence,
                              payload, payload_len, frame,
                              SRNOS_PROTOCOL_FRAME_SLOT_SIZE,
                              &frame_len) == SRNOS_PROTOCOL_OK);
    return frame_len;
}

static SRNOS_ProtocolView decode_tx(const fixture_t *fixture,
                                      uint8_t *raw)
{
    assert(fixture->tx_in_use);
    assert(fixture->tx_length >= 3u);
    assert(fixture->tx_pointer[0] == 0u);
    assert(fixture->tx_pointer[fixture->tx_length - 1u] == 0u);
    size_t raw_len = 0u;
    assert(SRNOS_COBSDecode(&fixture->tx_pointer[1],
                           fixture->tx_length - 2u,
                           raw,
                           SRNOS_PROTOCOL_MAX_RAW_LEN,
                           &raw_len) == SRNOS_COBS_OK);
    SRNOS_ProtocolView view;
    assert(SRNOS_ProtocolParse(raw, raw_len, &view) == SRNOS_PROTOCOL_OK);
    return view;
}

static void complete_tx(fixture_t *fixture, bool success)
{
    assert(fixture->tx_in_use);
    fixture->completion_pending = true;
    fixture->completion_success = success;
    test_poll_once();
}

static void count_source_records(const SRNOS_ProtocolView *view,
                                 uint32_t *counts,
                                 size_t count_capacity)
{
    assert(view->header.kind == SRNOS_PROTOCOL_KIND_EVENT);
    assert(view->header.service == SRNOS_HOST_LINK_SERVICE_TELEMETRY);
    assert(view->header.message == 0x07u);
    assert(view->payload_len >= 12u);
    const uint8_t record_count = view->payload[8];
    size_t pos = 12u;
    for (uint8_t record = 0u; record < record_count; ++record) {
        assert((pos + 4u) <= view->payload_len);
        const uint16_t index = test_load_le16(&view->payload[pos]);
        const uint8_t length = view->payload[pos + 3u];
        assert(index < count_capacity);
        assert((length == 0u) || (length == 1u) || (length == 4u));
        assert((pos + 4u + length) <= view->payload_len);
        counts[index]++;
        pos += 4u + length;
    }
    assert(pos == view->payload_len);
}

static SRNOS_ProtocolView source_request(fixture_t *f,uint8_t message,uint32_t sequence,
    const uint8_t *payload,size_t size,uint8_t *raw)
{
    while(f->tx_in_use)complete_tx(f,true);
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t length=pack(SRNOS_PROTOCOL_KIND_REQUEST,SRNOS_HOST_LINK_SERVICE_TELEMETRY,message,sequence,payload,size,frame);
    load_rx(f,frame,length,length);test_poll_once();
    SRNOS_ProtocolView response=decode_tx(f,raw);
    assert(response.header.kind==SRNOS_PROTOCOL_KIND_RESPONSE && response.header.sequence==sequence);
    return response;
}
static uint64_t test_load_le64(const uint8_t *data)
{ return test_load_le32(data)|((uint64_t)test_load_le32(data+4)<<32); }
static void test_source_v2(fixture_t *f,uint32_t hash)
{
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN],list[]={0,0,1};
    f->now_ms=UINT32_MAX-2u;
    SRNOS_ProtocolView view=source_request(f,0x10,7000,list,sizeof(list),raw);
    assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OK);
    assert(test_load_le16(view.payload+15)==1000 && test_load_le16(view.payload+17)==0 && view.payload[19]==0);
    complete_tx(f,true);
    uint8_t subscribe[5+32*4]={0};test_store_le32(subscribe,hash);subscribe[4]=8;
    for(unsigned i=0;i<8;++i){test_store_le16(subscribe+5+i*4,(uint16_t)i);test_store_le16(subscribe+7+i*4,1000);}
    f->source_getter_available=false;
    view=source_request(f,0x11,7001,subscribe,5+8*4,raw);
    assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OK && test_load_le16(view.payload+7)==8000);
    complete_tx(f,true);view=decode_tx(f,raw);
    assert(view.header.message==0x12 && view.payload[12]==8);
    assert(test_load_le64(view.payload+4)==(uint64_t)f->now_ms*1000u);
    size_t pos=16;
    for(unsigned i=0;i<8;++i){
        assert(test_load_le16(view.payload+pos)==i && view.payload[pos+3]==(i==0?0u:1u));
        assert(test_load_le32(view.payload+pos+4)>0);
        assert(test_load_le64(view.payload+pos+8)==(uint64_t)f->now_ms*1000u+i);
        pos+=view.payload[pos+2]==SRNOS_TELEMETRY_BOOL?17u:20u;
    }
    assert(pos==view.payload_len);complete_tx(f,true);f->source_getter_available=true;
    subscribe[4]=1;test_store_le16(subscribe+5,8);test_store_le16(subscribe+7,5);
    view=source_request(f,0x11,7002,subscribe,9,raw);
    assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE);complete_tx(f,true);
    list[0]=8;view=source_request(f,0x10,7003,list,3,raw);
    assert(test_load_le16(view.payload+17)==0 && view.payload[19]==0);complete_tx(f,true);
    uint8_t v1[8];test_store_le32(v1,hash);v1[4]=1;test_store_le16(v1+5,0);v1[7]=100;
    view=source_request(f,4,7004,v1,sizeof(v1),raw);
    assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OK && test_load_le16(view.payload+7)==7100);
    complete_tx(f,true);view=decode_tx(f,raw);assert(view.header.message==7 && view.payload[8]==1);complete_tx(f,true);
    uint8_t cancel[7];test_store_le32(cancel,hash);cancel[4]=1;test_store_le16(cancel+5,0);
    view=source_request(f,0x13,7005,cancel,7,raw);assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_NOT_FOUND);complete_tx(f,true);
    view=source_request(f,6,7006,cancel,4,raw);assert(test_load_le16(view.payload+7)==7000);complete_tx(f,true);
    view=source_request(f,0x14,7007,cancel,4,raw);assert(view.payload[6]==0);complete_tx(f,true);
    subscribe[4]=1;test_store_le16(subscribe+5,31);test_store_le16(subscribe+7,205);
    view=source_request(f,0x11,7010,subscribe,9,raw);
    assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE);complete_tx(f,true);
    /* All 32 sources at one rate use stable index halves, at most 16 records.
     * A second rate at slot31 must not coalesce the first rate's queued data. */
    subscribe[4]=32;
    for(unsigned i=0;i<32;++i){test_store_le16(subscribe+5+i*4,(uint16_t)i);test_store_le16(subscribe+7+i*4,100);}
    test_store_le16(subscribe+7+31*4,200);
    view=source_request(f,0x11,7008,subscribe,sizeof(subscribe),raw);assert(test_load_le16(view.payload)==0);complete_tx(f,true);
    unsigned seen=0,packets=0;
    while(f->tx_in_use) {
        view=decode_tx(f,raw);assert(view.header.message==0x12 && view.payload[12]<=16);++packets;
        pos=16;for(unsigned n=0;n<view.payload[12];++n){unsigned id=test_load_le16(view.payload+pos);assert(!(seen&(UINT32_C(1)<<id)));seen|=UINT32_C(1)<<id;pos+=view.payload[pos+2]==SRNOS_TELEMETRY_BOOL?17u:20u;}
        complete_tx(f,true);
    }
    assert(seen==UINT32_MAX && packets==3);
    f->now_ms=20u;test_poll_once(); /* scheduling clock wraps; emit one latest round */
    packets=0;while(f->tx_in_use){view=decode_tx(f,raw);assert(view.payload[12]<=16);++packets;complete_tx(f,true);}assert(packets==3);
    f->tx_result=SRNOS_HOST_LINK_TX_BUSY;f->now_ms+=30;test_poll_once();
    f->now_ms+=20;test_poll_once();assert(!f->tx_in_use);
    f->tx_result=SRNOS_HOST_LINK_TX_ACCEPTED;test_poll_once();packets=0;seen=0;
    while(f->tx_in_use){
        view=decode_tx(f,raw);assert(view.header.message==0x12 && test_load_le64(view.payload+4)==(uint64_t)f->now_ms*1000u);++packets;
        pos=16;for(unsigned n=0;n<view.payload[12];++n){unsigned id=test_load_le16(view.payload+pos);assert(!(seen&(UINT32_C(1)<<id)));seen|=UINT32_C(1)<<id;pos+=view.payload[pos+2]==SRNOS_TELEMETRY_BOOL?17u:20u;}
        complete_tx(f,true);
    }
    assert(packets==3 && seen==UINT32_MAX);
    f->connected=false;test_poll_once();assert(!f->tx_in_use);
    for(unsigned i=0;i<100;++i){SRNOS_TelemetryValue value={.u32=i};assert(SRNOS_TelemetryPublish(4,value,i,true)==SRNOS_TELEMETRY_OK);}
    f->connected=true;test_poll_once();assert(!f->tx_in_use);
    puts("SOURCE V2 directory/maxHz, 8000Hz atomic cap, V1 replacement, 16-record groups, wrap and disconnect: PASS");
}

static void test_transport_exec_and_services(void)
{
    fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    fixture.connected = true;
    fixture.tx_result = SRNOS_HOST_LINK_TX_ACCEPTED;
    fixture.source_float = 1.5f;
    fixture.source_i32 = -7;
    fixture.source_bool = true;
    fixture.source_values[4] = 0x12345678u;

    const SRNOS_HostLinkConfig config = {
        .transport = {
            .user = &fixture,
            .init = transport_init,
            .start_rx = transport_start_rx,
            .rx_read = transport_read,
            .rx_overflow_bytes = transport_overflow,
            .tx_submit = transport_submit,
            .tx_take_complete = transport_take_completion,
            .is_connected = transport_connected,
        },
        .now_ms = test_now,
        .cycles = test_cycles,
        .clock_user = &fixture,
        .tx_storage = fixture.tx_storage,
        .tx_storage_size = sizeof(fixture.tx_storage),
        .rx_drain_buffer = fixture.rx_drain,
        .rx_drain_buffer_size = sizeof(fixture.rx_drain),
        .project_setup = project_setup,
        .snapshot = take_snapshot,
        .project_user = &fixture,
        .task_wait_ms = 2u,
        .snapshot_period_ms = 10000u,
    };
    SRNOS_HostLinkConfig invalid = config;
    invalid.tx_storage = &fixture.tx_storage[1];
    assert(SRNOS_HostLinkConfigure(&invalid) == SRNOS_HOST_LINK_ERR_PARAM);
    invalid = config;
    invalid.rx_drain_buffer = &fixture.rx_drain[1];
    assert(SRNOS_HostLinkConfigure(&invalid) == SRNOS_HOST_LINK_ERR_PARAM);
    assert(SRNOS_HostLinkConfigure(&config) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkInitResources());
    assert(SRNOS_HostLinkRegisterExec(7u, exec_handler, &fixture) ==
           SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkRegisterTelemetry(9u, 32u, telemetry_config,
                                             &fixture) == SRNOS_HOST_LINK_OK);
    uint8_t control_storage[8];
    assert(SRNOS_HostLinkRegisterControl(3u, control_storage,
                                           sizeof(control_storage)) ==
           SRNOS_HOST_LINK_OK);

    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    assert(SRNOS_HostLinkTestStart() == SRNOS_HOST_LINK_OK);
    source_scheduler_state=taskSCHEDULER_RUNNING;
    assert(SRNOS_HostLinkSubmitDeferred(SRNOS_HOST_LINK_SERVICE_TEST,0x41u,1u,
        NULL,0u,deferred_work,&fixture)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(SRNOS_HostLinkRegisterTxTopic(saved_registration,0x7e,0x42,
        SRNOS_HOST_LINK_QOS_TELEMETRY)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(SRNOS_HostLinkRegisterHandler(saved_registration,0x7e,0x42,1,
        SRNOS_HOST_LINK_QOS_RELIABLE,defer_request,&fixture)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(fixture.initialized && fixture.rx_started);
    assert(fixture.setup_calls == 1u);
    assert(fixture.snapshot_calls == 0u);

    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    SRNOS_TelemetryDescriptor late_source={"late",NULL,SRNOS_TELEMETRY_UINT32,1000};
    SRNOS_TelemetryHandle late_handle;
    assert(SRNOS_TelemetryRegister(&late_source,&late_handle)==SRNOS_TELEMETRY_FROZEN);

    assert(SRNOS_HostLinkRegisterExec(42u,exec_handler,&fixture)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(SRNOS_HostLinkRegisterTelemetry(42u,32u,telemetry_config,&fixture)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(SRNOS_HostLinkRegisterControl(42u,control_storage,sizeof(control_storage))==SRNOS_HOST_LINK_ERR_BUSY);

    const uint8_t list_request[] = {0u, 0u, 2u};
    size_t frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                            SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                            0x03u,
                            9u,
                            list_request,
                            sizeof(list_request),
                            frame);
    load_rx(&fixture, frame, frame_len, 2u);
    test_poll_once();
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    SRNOS_ProtocolView view = decode_tx(&fixture, raw);
    assert(view.header.flags == SRNOS_PROTOCOL_FLAG_MORE);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    const uint32_t source_catalog_hash =
        test_load_le32(&view.payload[2]);
    assert(test_load_le16(&view.payload[6]) ==
           SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
    assert(test_load_le16(&view.payload[8]) == 2u);
    assert(view.payload[10] == 2u);
    assert(test_load_le16(&view.payload[11]) == 0u);
    assert(view.payload[13] == SRNOS_TELEMETRY_UINT32);
    assert(view.payload[14] == 4u);
    assert(view.payload[15] == 0u);
    assert(view.payload[16] == 9u);
    assert(view.payload[17] == 2u);
    assert(memcmp(&view.payload[18], "source.00", 9u) == 0);
    assert(memcmp(&view.payload[27], "ms", 2u) == 0);
    complete_tx(&fixture, true);

    const uint8_t echo_payload[] = {0x11u, 0x22u, 0x33u};
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_SYSTEM,
                     SRNOS_HOST_LINK_SYSTEM_ECHO,
                     11u,
                     echo_payload,
                     sizeof(echo_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, 1u);
    test_poll_once();
    assert(fixture.rx_offset == frame_len);
    assert(fixture.rx_read_calls == (frame_len + 1u));
    const uint8_t *stable_pointer = fixture.tx_pointer;
    uint8_t stable_frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    memcpy(stable_frame, fixture.tx_pointer, fixture.tx_length);
    const size_t stable_length = fixture.tx_length;
    test_poll_once();
    assert(fixture.tx_pointer == stable_pointer);
    assert(fixture.tx_length == stable_length);
    assert(memcmp(fixture.tx_pointer, stable_frame, stable_length) == 0);
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 11u);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload_len == (sizeof(echo_payload) + 2u));
    complete_tx(&fixture, true);
    assert(!fixture.tx_in_use);

    const uint8_t exec_payload[] = {7u, 0u, 0x5Au};
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_EXEC,
                     0x01u,
                     20u,
                     exec_payload,
                     sizeof(exec_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    assert(!fixture.tx_in_use);
    s_current_task = (TaskHandle_t)(uintptr_t)2u;
    assert(SRNOS_HostLinkTestWorkerOnce());
    assert(fixture.exec_calls == 1u);
    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 20u);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload_len == 4u);
    assert(view.payload[2] == 0x4Fu && view.payload[3] == 0x4Bu);
    complete_tx(&fixture, true);

    const uint8_t deferred_payload[] = {1u, 2u, 3u};
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TEST,
                     0x41u,
                     25u,
                     deferred_payload,
                     sizeof(deferred_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, 2u);
    test_poll_once();
    assert(!fixture.tx_in_use);
    s_current_task = (TaskHandle_t)(uintptr_t)2u;
    assert(SRNOS_HostLinkTestWorkerOnce());
    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 25u);
    assert(view.header.flags == SRNOS_PROTOCOL_FLAG_MORE);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload_len == 3u && view.payload[2] == 6u);
    complete_tx(&fixture, true);

    uint8_t burst[5u * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t burst_len = 0u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        burst_len += pack(SRNOS_PROTOCOL_KIND_REQUEST,
                          SRNOS_HOST_LINK_SERVICE_EXEC,
                          0x01u,
                          100u + i,
                          exec_payload,
                          sizeof(exec_payload),
                          &burst[burst_len]);
    }
    load_rx(&fixture, burst, burst_len, burst_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 104u);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_BUSY);
    for (size_t i = 0u; i < 4u; ++i) {
        s_current_task = (TaskHandle_t)(uintptr_t)2u;
        assert(SRNOS_HostLinkTestWorkerOnce());
    }
    assert(!SRNOS_HostLinkTestWorkerOnce());
    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    for (size_t i = 0u; i < 5u; ++i) {
        assert(fixture.tx_in_use);
        complete_tx(&fixture, true);
    }
    assert(!fixture.tx_in_use);

    uint8_t subscribe[7];
    test_store_le16(&subscribe[0], 9u);
    test_store_le32(&subscribe[2], 2500u);
    subscribe[6] = 1u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                     0x01u,
                     30u,
                     subscribe,
                     sizeof(subscribe),
                     frame);
    load_rx(&fixture, frame, frame_len, 3u);
    test_poll_once();
    assert(fixture.telemetry_enabled);
    assert(fixture.telemetry_period_us == 2500u);
    complete_tx(&fixture, true);

    const uint8_t first_sample[] = {1u, 2u};
    const uint8_t second_sample[] = {3u, 4u};
    assert(SRNOS_HostLinkPublishTelemetry(9u, 1u, 10u, 100u, 1u,
                                            first_sample,
                                            sizeof(first_sample)) ==
           SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkPublishTelemetry(9u, 2u, 20u, 100u, 1u,
                                            second_sample,
                                            sizeof(second_sample)) ==
           SRNOS_HOST_LINK_OK);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(view.header.kind == SRNOS_PROTOCOL_KIND_EVENT);
    assert(view.header.sequence == 2u);
    assert(test_load_le16(view.payload) == 9u);
    complete_tx(&fixture, true);

    uint8_t source_subscribe[5u + 5u * 3u];
    test_store_le32(&source_subscribe[0],
                             source_catalog_hash ^ 1u);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 100u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 31u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_CONFLICT);
    complete_tx(&fixture, true);

    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 3u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 311u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE);
    complete_tx(&fixture, true);

    test_store_le16(&source_subscribe[5],
                             SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
    source_subscribe[7] = 5u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 312u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_NOT_FOUND);
    complete_tx(&fixture, true);

    source_subscribe[4] = 5u;
    for (uint16_t i = 0u; i < 5u; ++i) {
        test_store_le16(&source_subscribe[5u + i * 3u], i);
        source_subscribe[7u + i * 3u] = 100u;
    }
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 32u,
                     source_subscribe, sizeof(source_subscribe), frame);
    load_rx(&fixture, frame, frame_len, 1u);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(test_load_le32(&view.payload[2]) == source_catalog_hash);
    assert(view.payload[6] == 5u);
    assert(test_load_le16(&view.payload[7]) == 500u);
    complete_tx(&fixture, true);

    view = decode_tx(&fixture, raw);
    assert(test_load_le32(&view.payload[0]) == source_catalog_hash);
    assert(view.payload[8] == 5u);
    size_t source_pos = 12u;
    assert(test_load_le16(&view.payload[source_pos]) == 0u);
    assert(view.payload[source_pos + 2u] ==
           SRNOS_TELEMETRY_UINT32);
    assert(view.payload[source_pos + 3u] == 0u);
    source_pos += 4u;
    assert(test_load_le16(&view.payload[source_pos]) == 1u);
    assert(view.payload[source_pos + 2u] ==
           SRNOS_TELEMETRY_FLOAT32);
    assert(view.payload[source_pos + 3u] == 4u);
    uint32_t float_bits = 0u;
    memcpy(&float_bits, &fixture.source_float, sizeof(float_bits));
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           float_bits);
    source_pos += 8u;
    assert(test_load_le16(&view.payload[source_pos]) == 2u);
    assert(view.payload[source_pos + 2u] ==
           SRNOS_TELEMETRY_INT32);
    assert(view.payload[source_pos + 3u] == 4u);
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           (uint32_t)fixture.source_i32);
    source_pos += 8u;
    assert(test_load_le16(&view.payload[source_pos]) == 3u);
    assert(view.payload[source_pos + 2u] == SRNOS_TELEMETRY_BOOL);
    assert(view.payload[source_pos + 3u] == 1u);
    assert(view.payload[source_pos + 4u] == 1u);
    source_pos += 5u;
    assert(test_load_le16(&view.payload[source_pos]) == 4u);
    assert(view.payload[source_pos + 2u] ==
           SRNOS_TELEMETRY_UINT32);
    assert(view.payload[source_pos + 3u] == 4u);
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           fixture.source_values[4]);
    uint32_t source_record_counts[SRNOS_HOST_LINK_SOURCE_COUNT_MAX] = {0u};
    count_source_records(&view, source_record_counts,
                         SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
    for (size_t i = 0u; i < 5u; ++i) {
        assert(source_record_counts[i] == 1u);
    }
    complete_tx(&fixture, true);

    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 5u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 33u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE);
    complete_tx(&fixture, true);

    uint8_t source_cancel[9u];
    test_store_le32(&source_cancel[0], source_catalog_hash);
    source_cancel[4] = 1u;
    test_store_le16(&source_cancel[5], 6u);
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x05u, 34u,
                     source_cancel, 7u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_NOT_FOUND);
    complete_tx(&fixture, true);

    source_cancel[4] = 2u;
    test_store_le16(&source_cancel[5], 0u);
    test_store_le16(&source_cancel[7], 1u);
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x05u, 35u,
                     source_cancel, sizeof(source_cancel), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload[6] == 3u);
    assert(test_load_le16(&view.payload[7]) == 300u);
    complete_tx(&fixture, true);

    source_cancel[4] = 2u;
    test_store_le16(&source_cancel[5], 2u);
    test_store_le16(&source_cancel[7], 2u);
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x05u, 36u,
                     source_cancel, sizeof(source_cancel), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);
    complete_tx(&fixture, true);

    uint8_t source_cancel_all[4u];
    test_store_le32(source_cancel_all, source_catalog_hash);
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x06u, 37u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload[6] == 0u);
    complete_tx(&fixture, true);

    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x06u, 370u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload[6] == 0u);
    complete_tx(&fixture, true);

    fixture.source_getter_available = true;
    uint8_t cadence_subscribe[5u + 3u * 3u];
    test_store_le32(&cadence_subscribe[0], source_catalog_hash);
    cadence_subscribe[4] = 3u;
    test_store_le16(&cadence_subscribe[5], 0u);
    cadence_subscribe[7] = 15u;
    test_store_le16(&cadence_subscribe[8], 1u);
    cadence_subscribe[10] = 5u;
    test_store_le16(&cadence_subscribe[11], 4u);
    cadence_subscribe[13] = 100u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 38u,
                     cadence_subscribe, sizeof(cadence_subscribe), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    memset(source_record_counts, 0, sizeof(source_record_counts));
    while(fixture.tx_in_use) {
        view = decode_tx(&fixture, raw);
        count_source_records(&view,source_record_counts,SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
        complete_tx(&fixture,true);
    }

    for (uint32_t now = 2u; now <= 1000u; now += 2u) {
        fixture.now_ms = now;
        test_poll_once();
        while (fixture.tx_in_use) {
            view = decode_tx(&fixture, raw);
            count_source_records(&view, source_record_counts,
                                 SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
            complete_tx(&fixture, true);
        }
    }
    test_poll_once();
    while (fixture.tx_in_use) {
        view = decode_tx(&fixture, raw);
        count_source_records(&view, source_record_counts,
                             SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
        complete_tx(&fixture, true);
    }
    assert(source_record_counts[0] == 16u);
    assert(source_record_counts[1] == 6u);
    assert(source_record_counts[4] == 101u);

    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x06u, 39u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    complete_tx(&fixture, true);

    uint8_t control_payload[11] = {0u};
    uint8_t controls[2u * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t controls_len = 0u;
    test_store_le16(&control_payload[0], 3u);
    test_store_le32(&control_payload[2], 1u);
    control_payload[10] = 0xA1u;
    controls_len += pack(SRNOS_PROTOCOL_KIND_EVENT,
                         SRNOS_HOST_LINK_SERVICE_CONTROL,
                         SRNOS_HOST_LINK_CONTROL_STATE,
                         1u,
                         control_payload,
                         sizeof(control_payload),
                         &controls[controls_len]);
    test_store_le32(&control_payload[2], 2u);
    control_payload[10] = 0xA2u;
    controls_len += pack(SRNOS_PROTOCOL_KIND_EVENT,
                         SRNOS_HOST_LINK_SERVICE_CONTROL,
                         SRNOS_HOST_LINK_CONTROL_STATE,
                         2u,
                         control_payload,
                         sizeof(control_payload),
                         &controls[controls_len]);
    load_rx(&fixture, controls, controls_len, 5u);
    test_poll_once();
    uint8_t control_out[8];
    size_t control_len = 0u;
    uint32_t generation = 0u;
    assert(SRNOS_HostLinkTakeControl(3u,
                                       control_out,
                                       sizeof(control_out),
                                       &control_len,
                                       &generation,
                                       NULL,
                                       NULL) == SRNOS_HOST_LINK_OK);
    assert(control_len == 1u && control_out[0] == 0xA2u);
    assert(generation == 2u);

    const uint8_t event_payload[] = {0xE1u};
    assert(SRNOS_HostLinkPublishEvent(SRNOS_HOST_LINK_SERVICE_TEST,
                                        0x40u,
                                        7u,
                                        55u,
                                        event_payload,
                                        sizeof(event_payload)) ==
           SRNOS_HOST_LINK_OK);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(view.header.service == SRNOS_HOST_LINK_SERVICE_TEST);
    assert(view.header.sequence == 55u);
    complete_tx(&fixture, true);

    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 60u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(view.payload[6] == 1u);
    complete_tx(&fixture, true);
    view = decode_tx(&fixture, raw);
    assert(view.header.message == 0x07u);
    complete_tx(&fixture, true);

    assert(SRNOS_HostLinkSubmitLog((const uint8_t *)"line\n", 5u) ==
           SRNOS_HOST_LINK_OK);
    fixture.tx_result = SRNOS_HOST_LINK_TX_BUSY;
    test_poll_once();
    fixture.connected = false;
    fixture.tx_result = SRNOS_HOST_LINK_TX_DISCONNECTED;
    test_poll_once();
    assert(!fixture.telemetry_enabled);
    fixture.tx_result = SRNOS_HOST_LINK_TX_ERROR;
    test_poll_once();
    fixture.connected = true;
    fixture.tx_result = SRNOS_HOST_LINK_TX_ACCEPTED;
    test_poll_once();
    assert(!fixture.tx_in_use); /* Old queued log was discarded on disconnect. */
    assert(SRNOS_HostLinkSubmitLog((const uint8_t *)"new session",11u)==SRNOS_HOST_LINK_OK);
    test_poll_once();
    complete_tx(&fixture, false);

    fixture.overflow = 17u;
    fixture.now_ms = 9999u;
    test_poll_once();
    assert(fixture.snapshot_calls == 0u);
    fixture.now_ms = 10000u;
    test_poll_once();
    assert(fixture.snapshot_calls == 1u);
    assert(fixture.snapshot.started == 1u);
    assert(fixture.snapshot.link.rx_overflow == 17u);
    assert(fixture.snapshot.link.transport_busy >= 1u);
    assert(fixture.snapshot.link.transport_errors >= 1u);
    assert(fixture.snapshot.link.transport_disconnects >= 1u);
    assert(fixture.snapshot.link.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].coalesced >= 1u);
    assert(fixture.snapshot.link.qos[SRNOS_HOST_LINK_QOS_CONTROL].coalesced >= 1u);
    assert(fixture.snapshot.comm_stack_high_water_words == UINT32_MAX);
    assert(fixture.snapshot.worker_stack_high_water_words == UINT32_MAX);
    assert(fixture.snapshot.deferred_high_water == 4u);
    assert(fixture.snapshot.sources.registered_count ==
           SRNOS_HOST_LINK_SOURCE_COUNT_MAX);
    assert(fixture.snapshot.sources.active_count == 0u);
    assert(fixture.snapshot.sources.total_rate_hz == 0u);
    assert(fixture.snapshot.sources.batches >= 100u);
    assert(fixture.snapshot.sources.records >= 123u);
    assert(fixture.snapshot.sources.read_failures >= 1u);
    assert(fixture.snapshot.sources.cycles_total != 0u);

    fixture.now_ms = UINT32_MAX - 1u;
    test_poll_once();
    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x04u, 70u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    view = decode_tx(&fixture, raw);
    assert(view.header.message == 0x07u);
    complete_tx(&fixture, true);
    fixture.now_ms = 0u;
    test_poll_once();
    assert(!fixture.tx_in_use);
    frame_len = pack(SRNOS_PROTOCOL_KIND_REQUEST,
                     SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x06u, 71u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    test_poll_once();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRNOS_HOST_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    assert(s_critical_depth == 0u);
    uint8_t ex_request[7]={0}; test_store_le16(ex_request,0x100);
    test_store_le32(ex_request+2,500); ex_request[6]=1;
    frame_len=pack(SRNOS_PROTOCOL_KIND_REQUEST,4,1,900,ex_request,7,frame);
    load_rx(&fixture,frame,frame_len,frame_len); test_poll_once();
    view=decode_tx(&fixture,raw); assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);
    assert(ex_configs==0); complete_tx(&fixture,true);
    test_store_le32(ex_request+2,1000);
    frame_len=pack(SRNOS_PROTOCOL_KIND_REQUEST,4,1,901,ex_request,7,frame);
    load_rx(&fixture,frame,frame_len,frame_len); test_poll_once();
    view=decode_tx(&fixture,raw); assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OK);
    assert(ex_configs==1 && ex_polls>0); complete_tx(&fixture,true);
    fixture.connected=false; test_poll_once(); assert(ex_configs==2);
    /* Idle now follows the snapshot deadline, rather than waking every ms. */
    fixture.now_ms=100000;test_poll_once();
    assert(SRNOS_HostLinkTestNextWaitMs()==10000);
    uint8_t padding[2048]={0};load_rx(&fixture,padding,sizeof(padding),512);
    test_poll_once();assert(fixture.rx_offset==1024 && SRNOS_HostLinkTestRxBudgetExhausted());
    test_poll_once();assert(fixture.rx_offset==2048 && SRNOS_HostLinkTestRxBudgetExhausted());
    test_poll_once();assert(!SRNOS_HostLinkTestRxBudgetExhausted());

    /* Fill all reliable TX slots, then fill the producer completion queue
     * while HostComm retains an already-dequeued result in its private slot. */
    fixture.connected=true;fixture.tx_result=SRNOS_HOST_LINK_TX_BUSY;
    uint8_t full_burst[SRNOS_TEST_HOST_LINK_RX_CAPACITY];size_t full_length=0;
    for(unsigned i=0;i<12;i++) full_length+=pack(SRNOS_PROTOCOL_KIND_REQUEST,
        SRNOS_HOST_LINK_SERVICE_SYSTEM,SRNOS_HOST_LINK_SYSTEM_ECHO,4000+i,NULL,0,full_burst+full_length);
    load_rx(&fixture,full_burst,full_length,512);test_poll_once();assert(!fixture.tx_in_use);
    full_length=0;
    for(unsigned i=0;i<4;i++)full_length+=pack(SRNOS_PROTOCOL_KIND_REQUEST,
        SRNOS_HOST_LINK_SERVICE_EXEC,1,5000+i,exec_payload,sizeof(exec_payload),full_burst+full_length);
    load_rx(&fixture,full_burst,full_length,512);test_poll_once();
    unsigned calls_before=fixture.exec_calls;
    assert(SRNOS_HostLinkTestWorkerOnce());test_poll_once();
    assert(SRNOS_HostLinkTestPendingCompletion());
    for(unsigned i=0;i<3;i++)assert(SRNOS_HostLinkTestWorkerOnce());
    frame_len=pack(SRNOS_PROTOCOL_KIND_REQUEST,SRNOS_HOST_LINK_SERVICE_EXEC,1,5004,
        exec_payload,sizeof(exec_payload),frame);
    load_rx(&fixture,frame,frame_len,frame_len);test_poll_once();
    assert(SRNOS_HostLinkTestWorkerOnce());
    assert(fixture.exec_calls==calls_before+5 && s_queues[1].count==4);
    fixture.tx_result=SRNOS_HOST_LINK_TX_ACCEPTED;test_poll_once();
    unsigned replies=0,seen=0;
    while(fixture.tx_in_use) {
        view=decode_tx(&fixture,raw);++replies;
        if(view.header.sequence>=5000 && view.header.sequence<5005) {
            unsigned bit=1u<<(view.header.sequence-5000);assert(!(seen&bit));seen|=bit;
            assert(test_load_le16(view.payload)==SRNOS_HOST_LINK_STATUS_OK);
        }
        assert(replies<=17);complete_tx(&fixture,true);
    }
    assert(replies==17 && seen==31 && !SRNOS_HostLinkTestPendingCompletion() && s_queues[1].count==0);
    test_source_v2(&fixture,source_catalog_hash);
    assert(s_critical_depth==0);
    puts("opaque registry freeze, idle deadlines, RX budget and no completion requeue loss: PASS");


}

int main(void)
{
    puts("test_transport_exec_and_services");
    test_transport_exec_and_services();
    puts("SRNOS_HostComm host tests: PASS");
    return 0;
}
