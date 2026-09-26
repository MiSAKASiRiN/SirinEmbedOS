#include "SirinEmbedOS/Services/HostLink/tskptt_host_comm.h"

#include "cmsis_os2.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs_prot.h"

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


#define TEST_QUEUE_COUNT       2u
#define TEST_QUEUE_DEPTH       4u
#define TEST_QUEUE_ITEM_MAX    512u
#define TEST_RX_CAPACITY       4096u

struct test_message_queue {
    uint32_t capacity;
    uint32_t item_size;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    uint8_t data[TEST_QUEUE_DEPTH][TEST_QUEUE_ITEM_MAX];
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
    uint32_t source_values[TSKPTT_HOST_COMM_SOURCE_COUNT_MAX];
    char source_names[TSKPTT_HOST_COMM_SOURCE_COUNT_MAX][16];
    srn_link_tx_result_t tx_result;
    const uint8_t *tx_pointer;
    size_t tx_length;
    uint8_t rx[TEST_RX_CAPACITY];
    size_t rx_length;
    size_t rx_offset;
    size_t rx_chunk;
    tskptt_host_comm_snapshot_t snapshot;
    SRN_LINK_ALIGN32 uint8_t tx_storage[
        SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE];
    SRN_LINK_ALIGN32 uint8_t rx_drain[SRN_COBS_PROT_FRAME_SLOT_SIZE];
} fixture_t;

static struct test_message_queue s_queues[TEST_QUEUE_COUNT];
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
    assert(s_queue_count < TEST_QUEUE_COUNT);
    assert(msg_count <= TEST_QUEUE_DEPTH);
    assert(msg_size <= TEST_QUEUE_ITEM_MAX);
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

static srn_link_tx_result_t transport_submit(void *user,
                                              const uint8_t *data,
                                              size_t length)
{
    fixture_t *fixture = user;
    if (fixture->tx_result == SRN_LINK_TX_ACCEPTED) {
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
                              const srn_cobs_prot_view_t *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags);

static bool source_uptime(void *user,
                          tskptt_host_comm_source_value_t *value)
{
    fixture_t *fixture = user;
    if (!fixture->source_getter_available) {
        return false;
    }
    value->u32 = fixture->now_ms;
    return true;
}

static srn_link_result_t project_setup(srn_link_t *link, void *user)
{
    fixture_t *fixture = user;
    assert(!fixture->initialized); /* declarations precede transport start */
    assert(!fixture->rx_started);
    fixture->setup_calls++;
    static SRN_LINK_ALIGN32 uint8_t bad_source_storage[8];
    tskptt_host_comm_source_config_t source = {
        .name = "invalid",
        .unit = NULL,
        .type = TSKPTT_HOST_COMM_SOURCE_UINT32,
        .value_ptr = &bad_source_storage[1],
    };
    assert(TSKPTT_HostComm_RegisterTelemetrySource(&source, NULL) ==
           SRN_LINK_ERR_PARAM);
    source.value_ptr = &fixture->source_values[0];
    source.read = source_uptime;
    source.user = fixture;
    assert(TSKPTT_HostComm_RegisterTelemetrySource(&source, NULL) ==
           SRN_LINK_ERR_PARAM);

    for (uint16_t i = 0u; i < TSKPTT_HOST_COMM_SOURCE_COUNT_MAX; ++i) {
        assert(snprintf(fixture->source_names[i],
                        sizeof(fixture->source_names[i]),
                        "source.%02u", (unsigned)i) > 0);
        memset(&source, 0, sizeof(source));
        source.name = fixture->source_names[i];
        source.type = TSKPTT_HOST_COMM_SOURCE_UINT32;
        source.value_ptr = &fixture->source_values[i];
        if (i == 0u) {
            source.unit = "ms";
            source.value_ptr = NULL;
            source.read = source_uptime;
            source.user = fixture;
        } else if (i == 1u) {
            source.unit = "rad";
            source.type = TSKPTT_HOST_COMM_SOURCE_FLOAT32;
            source.value_ptr = &fixture->source_float;
        } else if (i == 2u) {
            source.type = TSKPTT_HOST_COMM_SOURCE_INT32;
            source.value_ptr = &fixture->source_i32;
        } else if (i == 3u) {
            source.type = TSKPTT_HOST_COMM_SOURCE_BOOL;
            source.value_ptr = &fixture->source_bool;
        }
        uint16_t index = UINT16_MAX;
        assert(TSKPTT_HostComm_RegisterTelemetrySource(&source, &index) ==
               SRN_LINK_OK);
        assert(index == i);
        if (i == 0u) {
            assert(TSKPTT_HostComm_RegisterTelemetrySource(&source, NULL) ==
                   SRN_LINK_ERR_PARAM);
        }
    }
    source.name = "overflow";
    source.type = TSKPTT_HOST_COMM_SOURCE_UINT32;
    source.value_ptr = &fixture->source_values[0];
    source.read = NULL;
    assert(TSKPTT_HostComm_RegisterTelemetrySource(&source, NULL) ==
           SRN_LINK_ERR_FULL);

    srn_link_result_t result = srn_link_register_tx_topic(
        link, SRN_LINK_SERVICE_TEST, 0x40u, SRN_LINK_QOS_TELEMETRY);
    if (result != SRN_LINK_OK) {
        return result;
    }
    return srn_link_register_handler(
        link,
        SRN_LINK_SERVICE_TEST,
        0x41u,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE,
        defer_request,
        user);
}

static void take_snapshot(const tskptt_host_comm_snapshot_t *snapshot,
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
    return SRN_LINK_STATUS_OK;
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
    assert(service == SRN_LINK_SERVICE_TEST);
    assert(message == 0x41u);
    assert(request_len == 3u);
    assert(request[0] == 1u && request[1] == 2u && request[2] == 3u);
    assert(response_cap >= 1u);
    response[0] = 6u;
    *response_len = 1u;
    *response_flags = SRN_COBS_PROT_FLAG_MORE;
    return SRN_LINK_STATUS_OK;
}

static uint16_t defer_request(void *user,
                              const srn_cobs_prot_view_t *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags)
{
    (void)response;
    (void)response_cap;
    *response_len = 0u;
    *response_flags = 0u;
    return (TSKPTT_HostComm_SubmitDeferred(
                request->header.service,
                request->header.message,
                request->header.sequence,
                request->payload,
                request->payload_len,
                deferred_work,
                user) == SRN_LINK_OK) ?
        SRN_LINK_STATUS_DEFERRED_INTERNAL : SRN_LINK_STATUS_BUSY;
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
    assert(srn_cobs_prot_pack(kind, 0u, service, message, sequence,
                              payload, payload_len, frame,
                              SRN_COBS_PROT_FRAME_SLOT_SIZE,
                              &frame_len) == SRN_COBS_PROT_OK);
    return frame_len;
}

static srn_cobs_prot_view_t decode_tx(const fixture_t *fixture,
                                      uint8_t *raw)
{
    assert(fixture->tx_in_use);
    assert(fixture->tx_length >= 3u);
    assert(fixture->tx_pointer[0] == 0u);
    assert(fixture->tx_pointer[fixture->tx_length - 1u] == 0u);
    size_t raw_len = 0u;
    assert(srn_cobs_decode(&fixture->tx_pointer[1],
                           fixture->tx_length - 2u,
                           raw,
                           SRN_COBS_PROT_MAX_RAW_LEN,
                           &raw_len) == SRN_COBS_OK);
    srn_cobs_prot_view_t view;
    assert(srn_cobs_prot_parse(raw, raw_len, &view) == SRN_COBS_PROT_OK);
    return view;
}

static void complete_tx(fixture_t *fixture, bool success)
{
    assert(fixture->tx_in_use);
    fixture->completion_pending = true;
    fixture->completion_success = success;
    TSKPTT_HostComm_TestPollOnce();
}

static void count_source_records(const srn_cobs_prot_view_t *view,
                                 uint32_t *counts,
                                 size_t count_capacity)
{
    assert(view->header.kind == SRN_COBS_PROT_KIND_EVENT);
    assert(view->header.service == SRN_LINK_SERVICE_TELEMETRY);
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

static void test_transport_exec_and_services(void)
{
    fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    fixture.connected = true;
    fixture.tx_result = SRN_LINK_TX_ACCEPTED;
    fixture.source_float = 1.5f;
    fixture.source_i32 = -7;
    fixture.source_bool = true;
    fixture.source_values[4] = 0x12345678u;

    const tskptt_host_comm_config_t config = {
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
    tskptt_host_comm_config_t invalid = config;
    invalid.tx_storage = &fixture.tx_storage[1];
    assert(TSKPTT_HostComm_Configure(&invalid) == SRN_LINK_ERR_PARAM);
    invalid = config;
    invalid.rx_drain_buffer = &fixture.rx_drain[1];
    assert(TSKPTT_HostComm_Configure(&invalid) == SRN_LINK_ERR_PARAM);
    assert(TSKPTT_HostComm_Configure(&config) == SRN_LINK_OK);
    assert(TSKPTT_HostComm_InitResources());
    assert(TSKPTT_HostComm_RegisterExec(7u, exec_handler, &fixture) ==
           SRN_LINK_OK);
    assert(TSKPTT_HostComm_RegisterTelemetry(9u, 32u, telemetry_config,
                                             &fixture) == SRN_LINK_OK);
    uint8_t control_storage[8];
    assert(TSKPTT_HostComm_RegisterControl(3u, control_storage,
                                           sizeof(control_storage)) ==
           SRN_LINK_OK);

    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    assert(TSKPTT_HostComm_TestStart() == SRN_LINK_OK);
    assert(fixture.initialized && fixture.rx_started);
    assert(fixture.setup_calls == 1u);
    assert(fixture.snapshot_calls == 0u);

    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    tskptt_host_comm_source_config_t late_source = {
        .name = "late",
        .type = TSKPTT_HOST_COMM_SOURCE_UINT32,
        .value_ptr = &fixture.source_values[0],
    };
    assert(TSKPTT_HostComm_RegisterTelemetrySource(&late_source, NULL) ==
           SRN_LINK_ERR_BUSY);

    assert(TSKPTT_HostComm_RegisterExec(42u,exec_handler,&fixture)==SRN_LINK_ERR_BUSY);
    assert(TSKPTT_HostComm_RegisterTelemetry(42u,32u,telemetry_config,&fixture)==SRN_LINK_ERR_BUSY);
    assert(TSKPTT_HostComm_RegisterControl(42u,control_storage,sizeof(control_storage))==SRN_LINK_ERR_BUSY);

    const uint8_t list_request[] = {0u, 0u, 2u};
    size_t frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                            SRN_LINK_SERVICE_TELEMETRY,
                            0x03u,
                            9u,
                            list_request,
                            sizeof(list_request),
                            frame);
    load_rx(&fixture, frame, frame_len, 2u);
    TSKPTT_HostComm_TestPollOnce();
    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    srn_cobs_prot_view_t view = decode_tx(&fixture, raw);
    assert(view.header.flags == SRN_COBS_PROT_FLAG_MORE);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    const uint32_t source_catalog_hash =
        test_load_le32(&view.payload[2]);
    assert(test_load_le16(&view.payload[6]) ==
           TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
    assert(test_load_le16(&view.payload[8]) == 2u);
    assert(view.payload[10] == 2u);
    assert(test_load_le16(&view.payload[11]) == 0u);
    assert(view.payload[13] == TSKPTT_HOST_COMM_SOURCE_UINT32);
    assert(view.payload[14] == 4u);
    assert(view.payload[15] == 0u);
    assert(view.payload[16] == 9u);
    assert(view.payload[17] == 2u);
    assert(memcmp(&view.payload[18], "source.00", 9u) == 0);
    assert(memcmp(&view.payload[27], "ms", 2u) == 0);
    complete_tx(&fixture, true);

    const uint8_t echo_payload[] = {0x11u, 0x22u, 0x33u};
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_SYSTEM,
                     SRN_LINK_SYSTEM_ECHO,
                     11u,
                     echo_payload,
                     sizeof(echo_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, 1u);
    TSKPTT_HostComm_TestPollOnce();
    assert(fixture.rx_offset == frame_len);
    assert(fixture.rx_read_calls == (frame_len + 1u));
    const uint8_t *stable_pointer = fixture.tx_pointer;
    uint8_t stable_frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    memcpy(stable_frame, fixture.tx_pointer, fixture.tx_length);
    const size_t stable_length = fixture.tx_length;
    TSKPTT_HostComm_TestPollOnce();
    assert(fixture.tx_pointer == stable_pointer);
    assert(fixture.tx_length == stable_length);
    assert(memcmp(fixture.tx_pointer, stable_frame, stable_length) == 0);
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 11u);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload_len == (sizeof(echo_payload) + 2u));
    complete_tx(&fixture, true);
    assert(!fixture.tx_in_use);

    const uint8_t exec_payload[] = {7u, 0u, 0x5Au};
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_EXEC,
                     0x01u,
                     20u,
                     exec_payload,
                     sizeof(exec_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    assert(!fixture.tx_in_use);
    s_current_task = (TaskHandle_t)(uintptr_t)2u;
    assert(TSKPTT_HostComm_TestWorkerOnce());
    assert(fixture.exec_calls == 1u);
    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 20u);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload_len == 4u);
    assert(view.payload[2] == 0x4Fu && view.payload[3] == 0x4Bu);
    complete_tx(&fixture, true);

    const uint8_t deferred_payload[] = {1u, 2u, 3u};
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TEST,
                     0x41u,
                     25u,
                     deferred_payload,
                     sizeof(deferred_payload),
                     frame);
    load_rx(&fixture, frame, frame_len, 2u);
    TSKPTT_HostComm_TestPollOnce();
    assert(!fixture.tx_in_use);
    s_current_task = (TaskHandle_t)(uintptr_t)2u;
    assert(TSKPTT_HostComm_TestWorkerOnce());
    s_current_task = (TaskHandle_t)(uintptr_t)1u;
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 25u);
    assert(view.header.flags == SRN_COBS_PROT_FLAG_MORE);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload_len == 3u && view.payload[2] == 6u);
    complete_tx(&fixture, true);

    uint8_t burst[5u * SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t burst_len = 0u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        burst_len += pack(SRN_COBS_PROT_KIND_REQUEST,
                          SRN_LINK_SERVICE_EXEC,
                          0x01u,
                          100u + i,
                          exec_payload,
                          sizeof(exec_payload),
                          &burst[burst_len]);
    }
    load_rx(&fixture, burst, burst_len, burst_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(view.header.sequence == 104u);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_BUSY);
    for (size_t i = 0u; i < 4u; ++i) {
        s_current_task = (TaskHandle_t)(uintptr_t)2u;
        assert(TSKPTT_HostComm_TestWorkerOnce());
    }
    assert(!TSKPTT_HostComm_TestWorkerOnce());
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
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY,
                     0x01u,
                     30u,
                     subscribe,
                     sizeof(subscribe),
                     frame);
    load_rx(&fixture, frame, frame_len, 3u);
    TSKPTT_HostComm_TestPollOnce();
    assert(fixture.telemetry_enabled);
    assert(fixture.telemetry_period_us == 2500u);
    complete_tx(&fixture, true);

    const uint8_t first_sample[] = {1u, 2u};
    const uint8_t second_sample[] = {3u, 4u};
    assert(TSKPTT_HostComm_PublishTelemetry(9u, 1u, 10u, 100u, 1u,
                                            first_sample,
                                            sizeof(first_sample)) ==
           SRN_LINK_OK);
    assert(TSKPTT_HostComm_PublishTelemetry(9u, 2u, 20u, 100u, 1u,
                                            second_sample,
                                            sizeof(second_sample)) ==
           SRN_LINK_OK);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(view.header.kind == SRN_COBS_PROT_KIND_EVENT);
    assert(view.header.sequence == 2u);
    assert(test_load_le16(view.payload) == 9u);
    complete_tx(&fixture, true);

    uint8_t source_subscribe[5u + 5u * 3u];
    test_store_le32(&source_subscribe[0],
                             source_catalog_hash ^ 1u);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 100u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 31u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_CONFLICT);
    complete_tx(&fixture, true);

    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 3u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 311u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRN_LINK_STATUS_OUT_OF_RANGE);
    complete_tx(&fixture, true);

    test_store_le16(&source_subscribe[5],
                             TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
    source_subscribe[7] = 5u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 312u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_NOT_FOUND);
    complete_tx(&fixture, true);

    source_subscribe[4] = 5u;
    for (uint16_t i = 0u; i < 5u; ++i) {
        test_store_le16(&source_subscribe[5u + i * 3u], i);
        source_subscribe[7u + i * 3u] = 100u;
    }
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 32u,
                     source_subscribe, sizeof(source_subscribe), frame);
    load_rx(&fixture, frame, frame_len, 1u);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
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
           TSKPTT_HOST_COMM_SOURCE_UINT32);
    assert(view.payload[source_pos + 3u] == 0u);
    source_pos += 4u;
    assert(test_load_le16(&view.payload[source_pos]) == 1u);
    assert(view.payload[source_pos + 2u] ==
           TSKPTT_HOST_COMM_SOURCE_FLOAT32);
    assert(view.payload[source_pos + 3u] == 4u);
    uint32_t float_bits = 0u;
    memcpy(&float_bits, &fixture.source_float, sizeof(float_bits));
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           float_bits);
    source_pos += 8u;
    assert(test_load_le16(&view.payload[source_pos]) == 2u);
    assert(view.payload[source_pos + 2u] ==
           TSKPTT_HOST_COMM_SOURCE_INT32);
    assert(view.payload[source_pos + 3u] == 4u);
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           (uint32_t)fixture.source_i32);
    source_pos += 8u;
    assert(test_load_le16(&view.payload[source_pos]) == 3u);
    assert(view.payload[source_pos + 2u] == TSKPTT_HOST_COMM_SOURCE_BOOL);
    assert(view.payload[source_pos + 3u] == 1u);
    assert(view.payload[source_pos + 4u] == 1u);
    source_pos += 5u;
    assert(test_load_le16(&view.payload[source_pos]) == 4u);
    assert(view.payload[source_pos + 2u] ==
           TSKPTT_HOST_COMM_SOURCE_UINT32);
    assert(view.payload[source_pos + 3u] == 4u);
    assert(test_load_le32(&view.payload[source_pos + 4u]) ==
           fixture.source_values[4]);
    uint32_t source_record_counts[TSKPTT_HOST_COMM_SOURCE_COUNT_MAX] = {0u};
    count_source_records(&view, source_record_counts,
                         TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
    for (size_t i = 0u; i < 5u; ++i) {
        assert(source_record_counts[i] == 1u);
    }
    complete_tx(&fixture, true);

    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 5u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 33u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRN_LINK_STATUS_OUT_OF_RANGE);
    complete_tx(&fixture, true);

    uint8_t source_cancel[9u];
    test_store_le32(&source_cancel[0], source_catalog_hash);
    source_cancel[4] = 1u;
    test_store_le16(&source_cancel[5], 6u);
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x05u, 34u,
                     source_cancel, 7u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_NOT_FOUND);
    complete_tx(&fixture, true);

    source_cancel[4] = 2u;
    test_store_le16(&source_cancel[5], 0u);
    test_store_le16(&source_cancel[7], 1u);
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x05u, 35u,
                     source_cancel, sizeof(source_cancel), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload[6] == 3u);
    assert(test_load_le16(&view.payload[7]) == 300u);
    complete_tx(&fixture, true);

    source_cancel[4] = 2u;
    test_store_le16(&source_cancel[5], 2u);
    test_store_le16(&source_cancel[7], 2u);
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x05u, 36u,
                     source_cancel, sizeof(source_cancel), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) ==
           SRN_LINK_STATUS_INVALID_ARGUMENT);
    complete_tx(&fixture, true);

    uint8_t source_cancel_all[4u];
    test_store_le32(source_cancel_all, source_catalog_hash);
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x06u, 37u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload[6] == 0u);
    complete_tx(&fixture, true);

    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x06u, 370u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
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
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 38u,
                     cadence_subscribe, sizeof(cadence_subscribe), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    view = decode_tx(&fixture, raw);
    memset(source_record_counts, 0, sizeof(source_record_counts));
    count_source_records(&view, source_record_counts,
                         TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
    complete_tx(&fixture, true);

    for (uint32_t now = 2u; now <= 1000u; now += 2u) {
        fixture.now_ms = now;
        TSKPTT_HostComm_TestPollOnce();
        if (fixture.tx_in_use) {
            view = decode_tx(&fixture, raw);
            count_source_records(&view, source_record_counts,
                                 TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
            complete_tx(&fixture, true);
        }
    }
    TSKPTT_HostComm_TestPollOnce();
    if (fixture.tx_in_use) {
        view = decode_tx(&fixture, raw);
        count_source_records(&view, source_record_counts,
                             TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
        complete_tx(&fixture, true);
    }
    assert(source_record_counts[0] == 16u);
    assert(source_record_counts[1] == 6u);
    assert(source_record_counts[4] == 101u);

    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x06u, 39u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    complete_tx(&fixture, true);

    uint8_t control_payload[11] = {0u};
    uint8_t controls[2u * SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t controls_len = 0u;
    test_store_le16(&control_payload[0], 3u);
    test_store_le32(&control_payload[2], 1u);
    control_payload[10] = 0xA1u;
    controls_len += pack(SRN_COBS_PROT_KIND_EVENT,
                         SRN_LINK_SERVICE_CONTROL,
                         SRN_LINK_CONTROL_STATE,
                         1u,
                         control_payload,
                         sizeof(control_payload),
                         &controls[controls_len]);
    test_store_le32(&control_payload[2], 2u);
    control_payload[10] = 0xA2u;
    controls_len += pack(SRN_COBS_PROT_KIND_EVENT,
                         SRN_LINK_SERVICE_CONTROL,
                         SRN_LINK_CONTROL_STATE,
                         2u,
                         control_payload,
                         sizeof(control_payload),
                         &controls[controls_len]);
    load_rx(&fixture, controls, controls_len, 5u);
    TSKPTT_HostComm_TestPollOnce();
    uint8_t control_out[8];
    size_t control_len = 0u;
    uint32_t generation = 0u;
    assert(TSKPTT_HostComm_TakeControl(3u,
                                       control_out,
                                       sizeof(control_out),
                                       &control_len,
                                       &generation,
                                       NULL,
                                       NULL) == SRN_LINK_OK);
    assert(control_len == 1u && control_out[0] == 0xA2u);
    assert(generation == 2u);

    const uint8_t event_payload[] = {0xE1u};
    assert(TSKPTT_HostComm_PublishEvent(SRN_LINK_SERVICE_TEST,
                                        0x40u,
                                        7u,
                                        55u,
                                        event_payload,
                                        sizeof(event_payload)) ==
           SRN_LINK_OK);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(view.header.service == SRN_LINK_SERVICE_TEST);
    assert(view.header.sequence == 55u);
    complete_tx(&fixture, true);

    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 60u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    assert(view.payload[6] == 1u);
    complete_tx(&fixture, true);
    view = decode_tx(&fixture, raw);
    assert(view.header.message == 0x07u);
    complete_tx(&fixture, true);

    assert(TSKPTT_HostComm_SubmitLog((const uint8_t *)"line\n", 5u) ==
           SRN_LINK_OK);
    fixture.tx_result = SRN_LINK_TX_BUSY;
    TSKPTT_HostComm_TestPollOnce();
    fixture.connected = false;
    fixture.tx_result = SRN_LINK_TX_DISCONNECTED;
    TSKPTT_HostComm_TestPollOnce();
    assert(!fixture.telemetry_enabled);
    fixture.tx_result = SRN_LINK_TX_ERROR;
    TSKPTT_HostComm_TestPollOnce();
    fixture.connected = true;
    fixture.tx_result = SRN_LINK_TX_ACCEPTED;
    TSKPTT_HostComm_TestPollOnce();
    complete_tx(&fixture, false);

    fixture.overflow = 17u;
    fixture.now_ms = 9999u;
    TSKPTT_HostComm_TestPollOnce();
    assert(fixture.snapshot_calls == 0u);
    fixture.now_ms = 10000u;
    TSKPTT_HostComm_TestPollOnce();
    assert(fixture.snapshot_calls == 1u);
    assert(fixture.snapshot.started == 1u);
    assert(fixture.snapshot.link.rx_overflow == 17u);
    assert(fixture.snapshot.link.transport_busy >= 1u);
    assert(fixture.snapshot.link.transport_errors >= 1u);
    assert(fixture.snapshot.link.transport_disconnects >= 1u);
    assert(fixture.snapshot.link.qos[SRN_LINK_QOS_TELEMETRY].coalesced >= 1u);
    assert(fixture.snapshot.link.qos[SRN_LINK_QOS_CONTROL].coalesced >= 1u);
    assert(fixture.snapshot.comm_stack_high_water_words == 321u);
    assert(fixture.snapshot.worker_stack_high_water_words == 123u);
    assert(fixture.snapshot.deferred_high_water == 4u);
    assert(fixture.snapshot.sources.registered_count ==
           TSKPTT_HOST_COMM_SOURCE_COUNT_MAX);
    assert(fixture.snapshot.sources.active_count == 0u);
    assert(fixture.snapshot.sources.total_rate_hz == 0u);
    assert(fixture.snapshot.sources.batches >= 100u);
    assert(fixture.snapshot.sources.records >= 123u);
    assert(fixture.snapshot.sources.read_failures >= 1u);
    assert(fixture.snapshot.sources.cycles_total != 0u);

    fixture.now_ms = UINT32_MAX - 1u;
    TSKPTT_HostComm_TestPollOnce();
    test_store_le32(&source_subscribe[0], source_catalog_hash);
    source_subscribe[4] = 1u;
    test_store_le16(&source_subscribe[5], 0u);
    source_subscribe[7] = 5u;
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x04u, 70u,
                     source_subscribe, 8u, frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    view = decode_tx(&fixture, raw);
    assert(view.header.message == 0x07u);
    complete_tx(&fixture, true);
    fixture.now_ms = 0u;
    TSKPTT_HostComm_TestPollOnce();
    assert(!fixture.tx_in_use);
    frame_len = pack(SRN_COBS_PROT_KIND_REQUEST,
                     SRN_LINK_SERVICE_TELEMETRY, 0x06u, 71u,
                     source_cancel_all, sizeof(source_cancel_all), frame);
    load_rx(&fixture, frame, frame_len, frame_len);
    TSKPTT_HostComm_TestPollOnce();
    view = decode_tx(&fixture, raw);
    assert(test_load_le16(view.payload) == SRN_LINK_STATUS_OK);
    complete_tx(&fixture, true);
    assert(s_critical_depth == 0u);
}

int main(void)
{
    puts("test_transport_exec_and_services");
    test_transport_exec_and_services();
    puts("tskptt_host_comm host tests: PASS");
    return 0;
}
