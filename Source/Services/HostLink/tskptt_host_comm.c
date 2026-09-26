#include "SirinEmbedOS/Services/HostLink/tskptt_host_comm.h"
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
#define HOST_ENTER_CRITICAL() host_enter_critical()
#define HOST_EXIT_CRITICAL() host_exit_critical()
#else
#define HOST_ENTER_CRITICAL() taskENTER_CRITICAL()
#define HOST_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif

#define HOST_COMM_NOTIFY_RX          (1u << 0)
#define HOST_COMM_WORK_DEPTH         4u
#define HOST_COMM_COMPLETION_DEPTH   4u
#define HOST_COMM_EXEC_COUNT         8u
#define HOST_COMM_TELEMETRY_COUNT    8u
#define HOST_COMM_SOURCE_RATE_GROUPS \
    (TSKPTT_HOST_COMM_SOURCE_RATE_MAX_HZ / \
     TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ)
#define HOST_COMM_SOURCE_DATA_HEADER_SIZE 12u
#define HOST_COMM_SOURCE_RECORD_HEADER_SIZE 4u
#define HOST_COMM_FNV_OFFSET         2166136261u
#define HOST_COMM_FNV_PRIME          16777619u

enum {
    EXEC_MESSAGE_CALL = 0x01u,
    TELEMETRY_MESSAGE_SUBSCRIBE = 0x01u,
    TELEMETRY_MESSAGE_SAMPLE = 0x02u,
    TELEMETRY_MESSAGE_SOURCE_LIST = 0x03u,
    TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE = 0x04u,
    TELEMETRY_MESSAGE_SOURCE_CANCEL = 0x05u,
    TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL = 0x06u,
    TELEMETRY_MESSAGE_SOURCE_DATA = 0x07u,
};

typedef enum {
    WORK_EXEC = 0u,
    WORK_DEFERRED = 1u,
} work_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t service;
    uint8_t message;
    uint8_t reserved;
    uint16_t command_id;
    uint16_t payload_len;
    uint32_t sequence;
    tskptt_host_comm_deferred_fn deferred;
    void *user;
    uint8_t payload[TSKPTT_HOST_COMM_WORK_PAYLOAD_MAX];
} work_item_t;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t flags;
    uint8_t reserved;
    uint16_t status;
    uint16_t body_len;
    uint32_t sequence;
    uint8_t body[TSKPTT_HOST_COMM_RESPONSE_BODY_MAX];
} completion_t;

typedef struct {
    uint8_t used;
    uint16_t command_id;
    tskptt_host_comm_exec_fn handler;
    void *user;
} exec_entry_t;

typedef struct {
    uint8_t used;
    uint8_t enabled;
    uint16_t stream_id;
    uint16_t max_sample_size;
    uint16_t reserved;
    uint32_t period_us;
    tskptt_host_comm_telemetry_config_fn on_config;
    void *user;
} telemetry_entry_t;

typedef struct {
    uint8_t used;
    uint8_t type;
    uint8_t value_size;
    uint8_t rate_hz;
    uint8_t name_len;
    uint8_t unit_len;
    uint16_t reserved;
    const char *name;
    const char *unit;
    const volatile void *value_ptr;
    tskptt_host_comm_source_read_fn read;
    void *user;
} telemetry_source_entry_t;

typedef struct {
    uint32_t active_mask;
    uint32_t phase_milli;
} telemetry_rate_group_t;

typedef struct {
    uint8_t used;
    uint16_t channel;
    uint8_t *storage;
    size_t capacity;
} control_registration_t;

static SRN_LINK_ALIGN32 srn_link_t s_link;
static tskptt_host_comm_config_t s_config;
static osMessageQueueId_t s_work_queue;
static osMessageQueueId_t s_completion_queue;
static TaskHandle_t s_comm_task;
static TaskHandle_t s_worker_task;
static exec_entry_t s_exec[HOST_COMM_EXEC_COUNT];
static telemetry_entry_t s_telemetry[HOST_COMM_TELEMETRY_COUNT];
static telemetry_source_entry_t
    s_sources[TSKPTT_HOST_COMM_SOURCE_COUNT_MAX];
static telemetry_rate_group_t s_source_groups[HOST_COMM_SOURCE_RATE_GROUPS];
static control_registration_t s_control[SRN_LINK_CONTROL_CHANNEL_COUNT];
static uint8_t s_source_payload[SRN_COBS_PROT_MAX_PAYLOAD];
static tskptt_host_comm_source_snapshot_t s_source_stats;
static uint32_t s_deferred_high_water;
static uint32_t s_last_rx_overflow;
static uint32_t s_last_snapshot;
static uint32_t s_source_immediate_mask;
static uint32_t s_source_last_poll_ms;
static uint32_t s_source_sequence;
static uint32_t s_source_catalog_hash;
static uint8_t s_source_count;
static uint8_t s_source_catalog_frozen;
static uint8_t s_transport_connected;
static uint8_t s_configured;
static uint8_t s_link_ready;
static uint8_t s_started;

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
    HOST_ENTER_CRITICAL();
}

static void link_unlock(void *user)
{
    (void)user;
    HOST_EXIT_CRITICAL();
}

static srn_link_tx_result_t transport_submit(void *user,
                                             const uint8_t *data,
                                             size_t length)
{
    (void)user;
    return s_config.transport.tx_submit(s_config.transport.user, data, length);
}

static exec_entry_t *find_exec(uint16_t command_id)
{
    for (size_t i = 0u; i < HOST_COMM_EXEC_COUNT; ++i) {
        if ((s_exec[i].used != 0u) && (s_exec[i].command_id == command_id)) {
            return &s_exec[i];
        }
    }
    return NULL;
}

static telemetry_entry_t *find_telemetry(uint16_t stream_id)
{
    for (size_t i = 0u; i < HOST_COMM_TELEMETRY_COUNT; ++i) {
        if ((s_telemetry[i].used != 0u) &&
            (s_telemetry[i].stream_id == stream_id)) {
            return &s_telemetry[i];
        }
    }
    return NULL;
}

_Static_assert(TSKPTT_HOST_COMM_SOURCE_COUNT_MAX == 32u,
               "telemetry source masks require exactly 32 slots");
_Static_assert(sizeof(float) == 4u, "telemetry float32 requires 32-bit float");
_Static_assert(sizeof(int32_t) == 4u,
               "telemetry int32 requires 32-bit int32_t");
_Static_assert(sizeof(uint32_t) == 4u,
               "telemetry uint32 requires 32-bit uint32_t");
_Static_assert(sizeof(bool) == 1u, "telemetry bool requires 1-byte bool");
_Static_assert(HOST_COMM_SOURCE_DATA_HEADER_SIZE +
               TSKPTT_HOST_COMM_SOURCE_COUNT_MAX *
               (HOST_COMM_SOURCE_RECORD_HEADER_SIZE + sizeof(uint32_t)) <=
               SRN_COBS_PROT_MAX_PAYLOAD,
               "all telemetry sources must fit in one payload");

static bool bounded_string_length(const char *text,
                                  size_t maximum,
                                  uint8_t *length_out)
{
    if ((text == NULL) || (length_out == NULL)) {
        return false;
    }
    size_t length = 0u;
    while ((length <= maximum) && (text[length] != '\0')) {
        length++;
    }
    if (length > maximum) {
        return false;
    }
    *length_out = (uint8_t)length;
    return true;
}

static bool bytes_equal(const char *a, const char *b, size_t length)
{
    for (size_t i = 0u; i < length; ++i) {
        if ((uint8_t)a[i] != (uint8_t)b[i]) {
            return false;
        }
    }
    return true;
}

static uint8_t source_value_size(tskptt_host_comm_source_type_t type)
{
    switch (type) {
    case TSKPTT_HOST_COMM_SOURCE_FLOAT32:
    case TSKPTT_HOST_COMM_SOURCE_INT32:
    case TSKPTT_HOST_COMM_SOURCE_UINT32:
        return 4u;
    case TSKPTT_HOST_COMM_SOURCE_BOOL:
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t fnv_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * HOST_COMM_FNV_PRIME;
}

static uint32_t fnv_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash = fnv_byte(hash, (uint8_t)(value >> shift));
    }
    return hash;
}

static uint32_t freeze_source_catalog(void)
{
    uint32_t hash = fnv_u32(HOST_COMM_FNV_OFFSET, s_source_count);
    for (size_t i = 0u; i < s_source_count; ++i) {
        const telemetry_source_entry_t *entry = &s_sources[i];
        hash = fnv_byte(hash, entry->type);
        hash = fnv_byte(hash, entry->value_size);
        hash = fnv_byte(hash, entry->name_len);
        for (size_t n = 0u; n < entry->name_len; ++n) {
            hash = fnv_byte(hash, (uint8_t)entry->name[n]);
        }
        hash = fnv_byte(hash, entry->unit_len);
        for (size_t n = 0u; n < entry->unit_len; ++n) {
            hash = fnv_byte(hash, (uint8_t)entry->unit[n]);
        }
    }
    s_source_catalog_hash = hash;
    s_source_catalog_frozen = 1u;
    s_source_stats.registered_count = s_source_count;
    return hash;
}

static uint32_t mask_population(uint32_t mask)
{
    uint32_t count = 0u;
    while (mask != 0u) {
        mask &= mask - 1u;
        count++;
    }
    return count;
}

static void rebuild_source_groups(void)
{
    uint32_t new_masks[HOST_COMM_SOURCE_RATE_GROUPS] = {0u};
    uint32_t active_count = 0u;
    uint32_t total_rate_hz = 0u;
    for (size_t i = 0u; i < s_source_count; ++i) {
        const uint8_t rate_hz = s_sources[i].rate_hz;
        if (rate_hz == 0u) {
            continue;
        }
        const size_t group =
            (size_t)(rate_hz / TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ) - 1u;
        new_masks[group] |= UINT32_C(1) << i;
        active_count++;
        total_rate_hz += rate_hz;
    }
    for (size_t group = 0u; group < HOST_COMM_SOURCE_RATE_GROUPS; ++group) {
        if ((new_masks[group] == 0u) ||
            (s_source_groups[group].active_mask == 0u)) {
            s_source_groups[group].phase_milli = 0u;
        }
        s_source_groups[group].active_mask = new_masks[group];
    }
    s_source_stats.active_count = active_count;
    s_source_stats.total_rate_hz = total_rate_hz;
}

static void clear_source_subscriptions(void)
{
    for (size_t i = 0u; i < s_source_count; ++i) {
        s_sources[i].rate_hz = 0u;
    }
    s_source_immediate_mask = 0u;
    rebuild_source_groups();
}

static void clear_all_telemetry_subscriptions(void)
{
    clear_source_subscriptions();
    for (size_t i = 0u; i < HOST_COMM_TELEMETRY_COUNT; ++i) {
        telemetry_entry_t *entry = &s_telemetry[i];
        if ((entry->used == 0u) || (entry->enabled == 0u)) {
            continue;
        }
        entry->enabled = 0u;
        if (entry->on_config != NULL) {
            entry->on_config(entry->user, false, entry->period_us);
        }
    }
    (void)srn_link_discard_topic(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                                 TELEMETRY_MESSAGE_SAMPLE);
    (void)srn_link_discard_topic(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                                 TELEMETRY_MESSAGE_SOURCE_DATA);
}

static uint16_t build_source_summary(uint8_t *response,
                                     size_t response_cap,
                                     size_t *response_len)
{
    if (response_cap < 7u) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    SRNOS_Memory_MutableByteSpan output =
        SRNOS_Memory_MutableBytes(response, response_cap);
    (void)SRNOS_Endian_StoreLE32(output, 0u, s_source_catalog_hash);
    response[4] = (uint8_t)s_source_stats.active_count;
    (void)SRNOS_Endian_StoreLE16(
        output, 5u, (uint16_t)s_source_stats.total_rate_hz);
    *response_len = 7u;
    return SRN_LINK_STATUS_OK;
}

static uint16_t handle_source_list(void *user,
                                   const srn_cobs_prot_view_t *request,
                                   uint8_t *response,
                                   size_t response_cap,
                                   size_t *response_len,
                                   uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if ((request->payload_len != 3u) || (request->payload[2] == 0u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    uint16_t cursor = 0u;
    if (!SRNOS_Endian_LoadLE16(
            SRNOS_Memory_Bytes(request->payload, request->payload_len),
            0u, &cursor)) return SRN_LINK_STATUS_BAD_LENGTH;
    if (cursor > s_source_count) {
        return SRN_LINK_STATUS_INVALID_ARGUMENT;
    }
    if (response_cap < 9u) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    SRNOS_Memory_MutableByteSpan output =
        SRNOS_Memory_MutableBytes(response, response_cap);
    (void)SRNOS_Endian_StoreLE32(output, 0u, s_source_catalog_hash);
    (void)SRNOS_Endian_StoreLE16(output, 4u, s_source_count);
    (void)SRNOS_Endian_StoreLE16(output, 6u, 0xFFFFu);
    response[8] = 0u;
    size_t pos = 9u;
    size_t index = cursor;
    uint8_t count = 0u;
    while ((index < s_source_count) && (count < request->payload[2])) {
        const telemetry_source_entry_t *entry = &s_sources[index];
        const size_t entry_len = 7u + entry->name_len + entry->unit_len;
        if ((pos + entry_len) > response_cap) {
            break;
        }
        (void)SRNOS_Endian_StoreLE16(output, pos, (uint16_t)index);
        response[pos + 2u] = entry->type;
        response[pos + 3u] = entry->value_size;
        response[pos + 4u] = entry->rate_hz;
        response[pos + 5u] = entry->name_len;
        response[pos + 6u] = entry->unit_len;
        pos += 7u;
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(&response[pos],
                                                     response_cap - pos),
                             SRNOS_Memory_Bytes(entry->name, entry->name_len));
        pos += entry->name_len;
        if (entry->unit_len != 0u) {
            (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(&response[pos],
                                                         response_cap - pos),
                                 SRNOS_Memory_Bytes(entry->unit,
                                                 entry->unit_len));
            pos += entry->unit_len;
        }
        index++;
        count++;
    }
    if ((count == 0u) && (index < s_source_count)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    const uint16_t next =
        (index < s_source_count) ? (uint16_t)index : 0xFFFFu;
    (void)SRNOS_Endian_StoreLE16(output, 6u, next);
    response[8] = count;
    *response_flags =
        (next != 0xFFFFu) ? SRN_COBS_PROT_FLAG_MORE : 0u;
    *response_len = pos;
    return SRN_LINK_STATUS_OK;
}

static bool source_hash_matches(const srn_cobs_prot_view_t *request)
{
    uint32_t hash = 0u;
    return SRNOS_Endian_LoadLE32(
               SRNOS_Memory_Bytes(request->payload, request->payload_len),
               0u, &hash) && (hash == s_source_catalog_hash);
}

static uint16_t handle_source_subscribe(void *user,
                                        const srn_cobs_prot_view_t *request,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len,
                                        uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if (request->payload_len < 5u) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const uint8_t count = request->payload[4];
    if ((count == 0u) ||
        (count > TSKPTT_HOST_COMM_SOURCE_COUNT_MAX) ||
        (request->payload_len != (5u + (size_t)count * 3u))) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    if (!source_hash_matches(request)) {
        return SRN_LINK_STATUS_CONFLICT;
    }
    uint8_t new_rates[TSKPTT_HOST_COMM_SOURCE_COUNT_MAX];
    uint32_t seen = 0u;
    for (size_t i = 0u; i < s_source_count; ++i) {
        new_rates[i] = s_sources[i].rate_hz;
    }
    for (size_t item = 0u; item < count; ++item) {
        const size_t offset = 5u + item * 3u;
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(
                SRNOS_Memory_Bytes(request->payload, request->payload_len),
                offset, &index)) return SRN_LINK_STATUS_BAD_LENGTH;
        const uint8_t rate_hz = request->payload[offset + 2u];
        if (index >= s_source_count) {
            return SRN_LINK_STATUS_NOT_FOUND;
        }
        const uint32_t bit = UINT32_C(1) << index;
        if ((seen & bit) != 0u) {
            return SRN_LINK_STATUS_INVALID_ARGUMENT;
        }
        seen |= bit;
        if ((rate_hz < TSKPTT_HOST_COMM_SOURCE_RATE_MIN_HZ) ||
            (rate_hz > TSKPTT_HOST_COMM_SOURCE_RATE_MAX_HZ) ||
            ((rate_hz % TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ) != 0u)) {
            return SRN_LINK_STATUS_OUT_OF_RANGE;
        }
        new_rates[index] = rate_hz;
    }
    uint32_t total_rate_hz = 0u;
    for (size_t i = 0u; i < s_source_count; ++i) {
        total_rate_hz += new_rates[i];
    }
    if (total_rate_hz > TSKPTT_HOST_COMM_SOURCE_RATE_TOTAL_HZ) {
        return SRN_LINK_STATUS_OUT_OF_RANGE;
    }
    bool changed = false;
    for (size_t i = 0u; i < s_source_count; ++i) {
        if (s_sources[i].rate_hz != new_rates[i]) {
            changed = true;
            s_source_immediate_mask |= UINT32_C(1) << i;
            s_sources[i].rate_hz = new_rates[i];
        }
    }
    rebuild_source_groups();
    if (changed) {
        (void)srn_link_discard_topic(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                                     TELEMETRY_MESSAGE_SOURCE_DATA);
    }
    return build_source_summary(response, response_cap, response_len);
}

static uint16_t handle_source_cancel(void *user,
                                     const srn_cobs_prot_view_t *request,
                                     uint8_t *response,
                                     size_t response_cap,
                                     size_t *response_len,
                                     uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if (request->payload_len < 5u) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const uint8_t count = request->payload[4];
    if ((count == 0u) ||
        (count > TSKPTT_HOST_COMM_SOURCE_COUNT_MAX) ||
        (request->payload_len != (5u + (size_t)count * 2u))) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    if (!source_hash_matches(request)) {
        return SRN_LINK_STATUS_CONFLICT;
    }
    uint32_t remove_mask = 0u;
    for (size_t item = 0u; item < count; ++item) {
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(
                SRNOS_Memory_Bytes(request->payload, request->payload_len),
                5u + item * 2u, &index)) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        if ((index >= s_source_count) || (s_sources[index].rate_hz == 0u)) {
            return SRN_LINK_STATUS_NOT_FOUND;
        }
        const uint32_t bit = UINT32_C(1) << index;
        if ((remove_mask & bit) != 0u) {
            return SRN_LINK_STATUS_INVALID_ARGUMENT;
        }
        remove_mask |= bit;
    }
    for (size_t i = 0u; i < s_source_count; ++i) {
        if ((remove_mask & (UINT32_C(1) << i)) != 0u) {
            s_sources[i].rate_hz = 0u;
        }
    }
    s_source_immediate_mask &= ~remove_mask;
    rebuild_source_groups();
    (void)srn_link_discard_topic(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                                 TELEMETRY_MESSAGE_SOURCE_DATA);
    return build_source_summary(response, response_cap, response_len);
}

static uint16_t handle_source_cancel_all(void *user,
                                         const srn_cobs_prot_view_t *request,
                                         uint8_t *response,
                                         size_t response_cap,
                                         size_t *response_len,
                                         uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if (request->payload_len != 4u) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    if (!source_hash_matches(request)) {
        return SRN_LINK_STATUS_CONFLICT;
    }
    clear_source_subscriptions();
    (void)srn_link_discard_topic(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                                 TELEMETRY_MESSAGE_SOURCE_DATA);
    return build_source_summary(response, response_cap, response_len);
}

static bool read_source_value(const telemetry_source_entry_t *entry,
                              tskptt_host_comm_source_value_t *value)
{
    if (entry->read != NULL) {
        return entry->read(entry->user, value);
    }
    switch ((tskptt_host_comm_source_type_t)entry->type) {
    case TSKPTT_HOST_COMM_SOURCE_FLOAT32:
        value->f32 = *(const volatile float *)entry->value_ptr;
        return true;
    case TSKPTT_HOST_COMM_SOURCE_INT32:
        value->i32 = *(const volatile int32_t *)entry->value_ptr;
        return true;
    case TSKPTT_HOST_COMM_SOURCE_UINT32:
        value->u32 = *(const volatile uint32_t *)entry->value_ptr;
        return true;
    case TSKPTT_HOST_COMM_SOURCE_BOOL:
        value->boolean = *(const volatile bool *)entry->value_ptr;
        return true;
    default:
        return false;
    }
}

static void record_source_cycles(uint32_t start)
{
    if (s_config.cycles == NULL) {
        return;
    }
    const uint32_t elapsed = link_cycles(NULL) - start;
    s_source_stats.cycles_total += elapsed;
    if (elapsed > s_source_stats.cycles_max) {
        s_source_stats.cycles_max = elapsed;
    }
    srn_link_record_processing_cycles(&s_link, elapsed);
}

static void poll_telemetry_sources(uint32_t now_ms)
{
    const uint32_t cycle_start =
        (s_config.cycles != NULL) ? link_cycles(NULL) : 0u;
    const uint32_t elapsed_ms = now_ms - s_source_last_poll_ms;
    s_source_last_poll_ms = now_ms;
    if (s_source_stats.active_count == 0u) {
        return;
    }

    uint32_t due_mask = s_source_immediate_mask;
    s_source_immediate_mask = 0u;
    uint16_t coalesce_key = UINT16_MAX;
    for (size_t group = 0u; group < HOST_COMM_SOURCE_RATE_GROUPS; ++group) {
        telemetry_rate_group_t *rate_group = &s_source_groups[group];
        if (rate_group->active_mask == 0u) {
            continue;
        }
        const uint32_t rate_hz =
            (uint32_t)(group + 1u) * TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ;
        const uint32_t whole_seconds = elapsed_ms / 1000u;
        const uint32_t remainder_ms = elapsed_ms % 1000u;
        const uint32_t fractional = rate_group->phase_milli +
            remainder_ms * rate_hz;
        const uint32_t periods = whole_seconds * rate_hz +
            fractional / 1000u;
        rate_group->phase_milli = fractional % 1000u;
        if (periods != 0u) {
            due_mask |= rate_group->active_mask;
            if ((group + 1u) < coalesce_key) {
                coalesce_key = (uint16_t)(group + 1u);
            }
            if (periods > 1u) {
                s_source_stats.missed_periods +=
                    (periods - 1u) * mask_population(rate_group->active_mask);
            }
        }
    }
    if (due_mask == 0u) {
        record_source_cycles(cycle_start);
        return;
    }

    size_t pos = HOST_COMM_SOURCE_DATA_HEADER_SIZE;
    uint8_t record_count = 0u;
    for (size_t i = 0u; i < s_source_count; ++i) {
        const uint32_t bit = UINT32_C(1) << i;
        if ((due_mask & bit) == 0u) {
            continue;
        }
        const telemetry_source_entry_t *entry = &s_sources[i];
        const uint16_t rate_key =
            (uint16_t)(entry->rate_hz /
                       TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ);
        if (rate_key < coalesce_key) {
            coalesce_key = rate_key;
        }
        (void)SRNOS_Endian_StoreLE16(
            SRNOS_Memory_MutableBytes(s_source_payload,
                                   sizeof(s_source_payload)),
            pos, (uint16_t)i);
        s_source_payload[pos + 2u] = entry->type;
        s_source_payload[pos + 3u] = 0u;
        pos += HOST_COMM_SOURCE_RECORD_HEADER_SIZE;

        tskptt_host_comm_source_value_t value;
        memset(&value, 0, sizeof(value));
        const bool available = read_source_value(entry, &value);
        if (!available) {
            s_source_stats.read_failures++;
        } else if (entry->type == TSKPTT_HOST_COMM_SOURCE_BOOL) {
            s_source_payload[pos] = value.boolean ? 1u : 0u;
            s_source_payload[pos - 1u] = 1u;
            pos++;
        } else {
            uint32_t raw_value = 0u;
            if (entry->type == TSKPTT_HOST_COMM_SOURCE_FLOAT32) {
                (void)SRNOS_Memory_Copy(
                    SRNOS_Memory_MutableBytes(&raw_value, sizeof(raw_value)),
                    SRNOS_Memory_Bytes(&value.f32, sizeof(value.f32)));
            } else if (entry->type == TSKPTT_HOST_COMM_SOURCE_INT32) {
                raw_value = (uint32_t)value.i32;
            } else {
                raw_value = value.u32;
            }
            (void)SRNOS_Endian_StoreLE32(
                SRNOS_Memory_MutableBytes(s_source_payload,
                                       sizeof(s_source_payload)),
                pos, raw_value);
            s_source_payload[pos - 1u] = 4u;
            pos += 4u;
        }
        record_count++;
    }

    SRNOS_Memory_MutableByteSpan source_payload =
        SRNOS_Memory_MutableBytes(s_source_payload, sizeof(s_source_payload));
    (void)SRNOS_Endian_StoreLE32(source_payload, 0u, s_source_catalog_hash);
    (void)SRNOS_Endian_StoreLE32(source_payload, 4u, now_ms);
    s_source_payload[8] = record_count;
    s_source_payload[9] = 0u;
    s_source_payload[10] = 0u;
    s_source_payload[11] = 0u;
    s_source_stats.batches++;
    s_source_stats.records += record_count;
    s_source_sequence++;
    record_source_cycles(cycle_start);
    if (srn_link_publish(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                         TELEMETRY_MESSAGE_SOURCE_DATA, coalesce_key,
                         s_source_sequence, s_source_payload, pos) !=
        SRN_LINK_OK) {
        s_source_stats.publish_failures++;
    }
}

static srn_link_result_t enqueue_work(const work_item_t *work)
{
    if ((s_work_queue == NULL) ||
        (osMessageQueuePut(s_work_queue, work, 0u, 0u) != osOK)) {
        return SRN_LINK_ERR_FULL;
    }
    const uint32_t depth = osMessageQueueGetCount(s_work_queue);
    if (depth > s_deferred_high_water) {
        s_deferred_high_water = depth;
    }
    return SRN_LINK_OK;
}

static uint16_t handle_exec(void *user,
                            const srn_cobs_prot_view_t *request,
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
        ((request->payload_len - 2u) > TSKPTT_HOST_COMM_WORK_PAYLOAD_MAX)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    uint16_t command_id = 0u;
    if (!SRNOS_Endian_LoadLE16(
            SRNOS_Memory_Bytes(request->payload, request->payload_len),
            0u, &command_id)) return SRN_LINK_STATUS_BAD_LENGTH;
    if (find_exec(command_id) == NULL) {
        return SRN_LINK_STATUS_NOT_FOUND;
    }
    work_item_t work;
    memset(&work, 0, sizeof(work));
    work.kind = WORK_EXEC;
    work.service = SRN_LINK_SERVICE_EXEC;
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
    return (enqueue_work(&work) == SRN_LINK_OK) ?
        SRN_LINK_STATUS_DEFERRED_INTERNAL : SRN_LINK_STATUS_BUSY;
}

static uint16_t handle_subscribe(void *user,
                                 const srn_cobs_prot_view_t *request,
                                 uint8_t *response,
                                 size_t response_cap,
                                 size_t *response_len,
                                 uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if ((request->payload_len != 7u) || (response_cap < 7u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint16_t stream_id = 0u;
    uint32_t period_us = 0u;
    if (!SRNOS_Endian_LoadLE16(payload, 0u, &stream_id) ||
        !SRNOS_Endian_LoadLE32(payload, 2u, &period_us)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const bool enabled = request->payload[6] != 0u;
    telemetry_entry_t *entry = find_telemetry(stream_id);
    if (entry == NULL) {
        return SRN_LINK_STATUS_NOT_FOUND;
    }
    entry->enabled = enabled ? 1u : 0u;
    entry->period_us = period_us;
    if (entry->on_config != NULL) {
        entry->on_config(entry->user, enabled, period_us);
    }
    (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(response, response_cap),
                         SRNOS_Memory_Bytes(request->payload,
                                         request->payload_len));
    *response_len = request->payload_len;
    return SRN_LINK_STATUS_OK;
}

static srn_link_result_t start_link(void)
{
    const srn_link_config_t link_config = {
        .now_ms = link_now_ms,
        .cycles = (s_config.cycles != NULL) ? link_cycles : NULL,
        .tx_submit = transport_submit,
        .lock = link_lock,
        .unlock = link_unlock,
        .tx_storage = s_config.tx_storage,
        .tx_storage_size = s_config.tx_storage_size,
        .user = NULL,
    };
    srn_link_result_t result = srn_link_init(&s_link, &link_config);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_EXEC, EXEC_MESSAGE_CALL,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_exec, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_TELEMETRY, TELEMETRY_MESSAGE_SUBSCRIBE,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_subscribe, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_tx_topic(
        &s_link, SRN_LINK_SERVICE_TELEMETRY, TELEMETRY_MESSAGE_SAMPLE,
        SRN_LINK_QOS_TELEMETRY);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_TELEMETRY,
        TELEMETRY_MESSAGE_SOURCE_LIST,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_source_list, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_TELEMETRY,
        TELEMETRY_MESSAGE_SOURCE_SUBSCRIBE,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_source_subscribe, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_TELEMETRY,
        TELEMETRY_MESSAGE_SOURCE_CANCEL,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_source_cancel, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_handler(
        &s_link, SRN_LINK_SERVICE_TELEMETRY,
        TELEMETRY_MESSAGE_SOURCE_CANCEL_ALL,
        SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE, handle_source_cancel_all, NULL);
    if (result != SRN_LINK_OK) {
        return result;
    }
    result = srn_link_register_tx_topic(
        &s_link, SRN_LINK_SERVICE_TELEMETRY,
        TELEMETRY_MESSAGE_SOURCE_DATA, SRN_LINK_QOS_TELEMETRY);
    if (result != SRN_LINK_OK) {
        return result;
    }
    if (s_config.project_setup != NULL) {
        result = s_config.project_setup(&s_link, s_config.project_user);
        if (result != SRN_LINK_OK) {
            return result;
        }
    }
    (void)freeze_source_catalog();
    for (size_t i = 0u; i < SRN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if (s_control[i].used != 0u) {
            result = srn_link_control_register(&s_link,
                                               s_control[i].channel,
                                               s_control[i].storage,
                                               s_control[i].capacity);
            if (result != SRN_LINK_OK) {
                return result;
            }
        }
    }
    s_link_ready = 1u;
    s_started = 1u;
    s_last_snapshot = link_now_ms(NULL);
    s_source_last_poll_ms = s_last_snapshot;
    return SRN_LINK_OK;
}

static void publish_snapshot(void)
{
    if (s_config.snapshot == NULL) {
        return;
    }
    tskptt_host_comm_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    srn_link_get_stats(&s_link, &snapshot.link);
    snapshot.sources = s_source_stats;
    snapshot.comm_stack_high_water_words =
        (s_comm_task != NULL) ? uxTaskGetStackHighWaterMark(s_comm_task) : 0u;
    snapshot.worker_stack_high_water_words =
        (s_worker_task != NULL) ? uxTaskGetStackHighWaterMark(s_worker_task) : 0u;
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
    if ((s_transport_connected != 0u) && !connected) {
        clear_all_telemetry_subscriptions();
    }
    s_transport_connected = connected ? 1u : 0u;
    srn_link_set_transport_connected(&s_link, connected);
    bool tx_success = false;
    while (s_config.transport.tx_take_complete(s_config.transport.user,
                                               &tx_success)) {
        srn_link_on_tx_complete(&s_link, tx_success);
    }
    for (;;) {
        const size_t count = s_config.transport.rx_read(
            s_config.transport.user,
            s_config.rx_drain_buffer,
            s_config.rx_drain_buffer_size);
        if (count == 0u) {
            break;
        }
        (void)srn_link_feed_rx(&s_link, s_config.rx_drain_buffer, count);
    }
    const uint32_t rx_overflow =
        s_config.transport.rx_overflow_bytes(s_config.transport.user);
    srn_link_record_rx_overflow(&s_link, rx_overflow - s_last_rx_overflow);
    s_last_rx_overflow = rx_overflow;

    completion_t completion;
    while (osMessageQueueGet(s_completion_queue,
                             &completion, NULL, 0u) == osOK) {
        if (srn_link_send_response(&s_link,
                                   completion.service,
                                   completion.message,
                                   completion.sequence,
                                   completion.status,
                                   completion.flags,
                                   completion.body,
                                   completion.body_len) != SRN_LINK_OK) {
            (void)osMessageQueuePut(s_completion_queue,
                                    &completion, 0u, 0u);
            break;
        }
    }
    if (connected) {
        (void)srn_link_service_tx(&s_link);
        poll_telemetry_sources(link_now_ms(NULL));
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

static bool worker_once(uint32_t timeout)
{
    work_item_t work;
    if (osMessageQueueGet(s_work_queue, &work, NULL, timeout) != osOK) {
        return false;
    }
    completion_t completion;
    memset(&completion, 0, sizeof(completion));
    completion.service = work.service;
    completion.message = work.message;
    completion.sequence = work.sequence;
    size_t body_len = 0u;
    if (work.kind == WORK_EXEC) {
        exec_entry_t *entry = find_exec(work.command_id);
        if ((entry == NULL) || (entry->handler == NULL)) {
            completion.status = SRN_LINK_STATUS_NOT_FOUND;
        } else {
            completion.status = entry->handler(
                entry->user, work.payload, work.payload_len,
                completion.body, sizeof(completion.body), &body_len);
        }
    } else if (work.deferred != NULL) {
        completion.status = work.deferred(
            work.user, work.service, work.message,
            work.payload, work.payload_len,
            completion.body, sizeof(completion.body),
            &body_len, &completion.flags);
    } else {
        completion.status = SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    if ((body_len > sizeof(completion.body)) ||
        ((completion.flags & (uint8_t)~SRN_COBS_PROT_VALID_FLAGS) != 0u)) {
        completion.status = SRN_LINK_STATUS_INTERNAL_ERROR;
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

srn_link_result_t TSKPTT_HostComm_Configure(
    const tskptt_host_comm_config_t *config)
{
    if ((config == NULL) || (s_configured != 0u) ||
        (config->now_ms == NULL) ||
        (config->tx_storage == NULL) ||
        (((uintptr_t)config->tx_storage & 31u) != 0u) ||
        (config->tx_storage_size <
         (SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE)) ||
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
        return SRN_LINK_ERR_PARAM;
    }
    s_config = *config;
    if (s_config.task_wait_ms == 0u) {
        s_config.task_wait_ms = TSKPTT_HOST_COMM_DEFAULT_WAIT_MS;
    }
    if (s_config.snapshot_period_ms == 0u) {
        s_config.snapshot_period_ms = TSKPTT_HOST_COMM_DEFAULT_SNAPSHOT_MS;
    }
    s_configured = 1u;
    return SRN_LINK_OK;
}

bool TSKPTT_HostComm_InitResources(void)
{
    if (s_configured == 0u) {
        return false;
    }
    if (s_work_queue == NULL) {
        s_work_queue = osMessageQueueNew(HOST_COMM_WORK_DEPTH,
                                         sizeof(work_item_t), NULL);
    }
    if (s_completion_queue == NULL) {
        s_completion_queue = osMessageQueueNew(HOST_COMM_COMPLETION_DEPTH,
                                               sizeof(completion_t), NULL);
    }
    return (s_work_queue != NULL) && (s_completion_queue != NULL);
}

srn_link_result_t TSKPTT_HostComm_PrepareLink(void)
{
    if (!s_configured || s_started) return SRN_LINK_ERR_BUSY;
    return start_link();
}

void TSKPTT_HostComm_Task(void *argument)
{
    (void)argument;
    SRNOS_ASSERT(TSKPTT_HostComm_InitResources());
    s_comm_task = xTaskGetCurrentTaskHandle();
    if (!s_started) SRNOS_ASSERT(start_link() == SRN_LINK_OK);
    SRNOS_ASSERT(s_config.transport.init(s_config.transport.user));
    SRNOS_ASSERT(s_config.transport.start_rx(s_config.transport.user, s_comm_task, HOST_COMM_NOTIFY_RX));

    for (;;) {
        uint32_t notifications = 0u;
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notifications,
                              pdMS_TO_TICKS(s_config.task_wait_ms));
        (void)notifications;

        poll_once();
        publish_snapshot_if_due();
    }
}

void TSKPTT_HostComm_WorkerTask(void *argument)
{
    (void)argument;
    SRNOS_ASSERT(TSKPTT_HostComm_InitResources());
    s_worker_task = xTaskGetCurrentTaskHandle();
    for (;;) {
        (void)worker_once(osWaitForever);
    }
}

srn_link_result_t TSKPTT_HostComm_RegisterExec(
    uint16_t command_id,
    tskptt_host_comm_exec_fn handler,
    void *user)
{
    if (s_started) return SRN_LINK_ERR_BUSY;
    if ((s_configured == 0u) || (handler == NULL)) {
        return SRN_LINK_ERR_PARAM;
    }
    HOST_ENTER_CRITICAL();
    for (size_t i = 0u; i < HOST_COMM_EXEC_COUNT; ++i) {
        if ((s_exec[i].used != 0u) && (s_exec[i].command_id == command_id)) {
            HOST_EXIT_CRITICAL();
            return SRN_LINK_ERR_PARAM;
        }
        if (s_exec[i].used == 0u) {
            s_exec[i].used = 1u;
            s_exec[i].command_id = command_id;
            s_exec[i].handler = handler;
            s_exec[i].user = user;
            HOST_EXIT_CRITICAL();
            return SRN_LINK_OK;
        }
    }
    HOST_EXIT_CRITICAL();
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t TSKPTT_HostComm_RegisterTelemetry(
    uint16_t stream_id,
    uint16_t max_sample_size,
    tskptt_host_comm_telemetry_config_fn on_config,
    void *user)
{
    if (s_started) return SRN_LINK_ERR_BUSY;
    if ((s_configured == 0u) || (max_sample_size == 0u) ||
        (max_sample_size > (SRN_COBS_PROT_MAX_PAYLOAD - 14u))) {
        return SRN_LINK_ERR_PARAM;
    }
    HOST_ENTER_CRITICAL();
    for (size_t i = 0u; i < HOST_COMM_TELEMETRY_COUNT; ++i) {
        if ((s_telemetry[i].used != 0u) &&
            (s_telemetry[i].stream_id == stream_id)) {
            HOST_EXIT_CRITICAL();
            return SRN_LINK_ERR_PARAM;
        }
        if (s_telemetry[i].used == 0u) {
            s_telemetry[i].used = 1u;
            s_telemetry[i].stream_id = stream_id;
            s_telemetry[i].max_sample_size = max_sample_size;
            s_telemetry[i].on_config = on_config;
            s_telemetry[i].user = user;
            HOST_EXIT_CRITICAL();
            return SRN_LINK_OK;
        }
    }
    HOST_EXIT_CRITICAL();
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t TSKPTT_HostComm_RegisterTelemetrySource(
    const tskptt_host_comm_source_config_t *config,
    uint16_t *index_out)
{
    if ((config == NULL) || (s_configured == 0u) ||
        (config->name == NULL) ||
        ((config->value_ptr == NULL) == (config->read == NULL))) {
        return SRN_LINK_ERR_PARAM;
    }
    if (s_source_catalog_frozen != 0u) {
        return SRN_LINK_ERR_BUSY;
    }
    uint8_t name_len = 0u;
    uint8_t unit_len = 0u;
    const char *unit = (config->unit != NULL) ? config->unit : "";
    const uint8_t value_size = source_value_size(config->type);
    if ((value_size == 0u) ||
        !bounded_string_length(config->name,
                               TSKPTT_HOST_COMM_SOURCE_NAME_MAX,
                               &name_len) ||
        (name_len == 0u) ||
        !bounded_string_length(unit,
                               TSKPTT_HOST_COMM_SOURCE_UNIT_MAX,
                               &unit_len) ||
        ((config->value_ptr != NULL) && (value_size == 4u) &&
         (((uintptr_t)config->value_ptr & 3u) != 0u))) {
        return SRN_LINK_ERR_PARAM;
    }

    HOST_ENTER_CRITICAL();
    if (s_source_catalog_frozen != 0u) {
        HOST_EXIT_CRITICAL();
        return SRN_LINK_ERR_BUSY;
    }
    for (size_t i = 0u; i < s_source_count; ++i) {
        const telemetry_source_entry_t *entry = &s_sources[i];
        if ((entry->name_len == name_len) &&
            bytes_equal(entry->name, config->name, name_len)) {
            HOST_EXIT_CRITICAL();
            return SRN_LINK_ERR_PARAM;
        }
    }
    if (s_source_count >= TSKPTT_HOST_COMM_SOURCE_COUNT_MAX) {
        HOST_EXIT_CRITICAL();
        return SRN_LINK_ERR_FULL;
    }
    const uint16_t index = s_source_count;
    telemetry_source_entry_t *entry = &s_sources[s_source_count++];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1u;
    entry->type = (uint8_t)config->type;
    entry->value_size = value_size;
    entry->name_len = name_len;
    entry->unit_len = unit_len;
    entry->name = config->name;
    entry->unit = unit;
    entry->value_ptr = config->value_ptr;
    entry->read = config->read;
    entry->user = config->user;
    HOST_EXIT_CRITICAL();
    if (index_out != NULL) {
        *index_out = index;
    }
    return SRN_LINK_OK;
}

srn_link_result_t TSKPTT_HostComm_RegisterControl(uint16_t channel,
                                                  uint8_t *storage,
                                                  size_t capacity)
{
    if (s_started) return SRN_LINK_ERR_BUSY;
    if ((s_configured == 0u) || (storage == NULL) || (capacity == 0u) ||
        (capacity > (SRN_COBS_PROT_MAX_PAYLOAD - 10u))) {
        return SRN_LINK_ERR_PARAM;
    }
    HOST_ENTER_CRITICAL();
    for (size_t i = 0u; i < SRN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if ((s_control[i].used != 0u) && (s_control[i].channel == channel)) {
            HOST_EXIT_CRITICAL();
            return SRN_LINK_ERR_PARAM;
        }
        if (s_control[i].used == 0u) {
            s_control[i].used = 1u;
            s_control[i].channel = channel;
            s_control[i].storage = storage;
            s_control[i].capacity = capacity;
            HOST_EXIT_CRITICAL();
            if (s_link_ready != 0u) {
                const srn_link_result_t result =
                    srn_link_control_register(&s_link,
                                              channel, storage, capacity);
                if (result != SRN_LINK_OK) {
                    HOST_ENTER_CRITICAL();
                    memset(&s_control[i], 0, sizeof(s_control[i]));
                    HOST_EXIT_CRITICAL();
                }
                return result;
            }
            return SRN_LINK_OK;
        }
    }
    HOST_EXIT_CRITICAL();
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t TSKPTT_HostComm_PublishTelemetry(
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
        return SRN_LINK_ERR_BUSY;
    }
    if (entry == NULL) {
        return SRN_LINK_ERR_NOT_FOUND;
    }
    if ((entry->enabled == 0u) || (sample_count == 0u) || (samples == NULL) ||
        (samples_len > entry->max_sample_size) ||
        ((samples_len + 14u) > SRN_COBS_PROT_MAX_PAYLOAD) ||
        ((samples_len % sample_count) != 0u) ||
        ((samples_len / sample_count) > UINT8_MAX)) {
        return SRN_LINK_ERR_PARAM;
    }
    uint8_t payload[SRN_COBS_PROT_MAX_PAYLOAD];
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
    return srn_link_publish(&s_link, SRN_LINK_SERVICE_TELEMETRY,
                            TELEMETRY_MESSAGE_SAMPLE, stream_id,
                            sample_sequence, payload, samples_len + 14u);
}

srn_link_result_t TSKPTT_HostComm_TakeControl(uint16_t channel,
                                              uint8_t *out,
                                              size_t out_cap,
                                              size_t *out_len,
                                              uint32_t *generation,
                                              uint32_t *deadline_us,
                                              uint32_t *received_ms)
{
    if (s_link_ready == 0u) {
        return SRN_LINK_ERR_BUSY;
    }
    return srn_link_control_take(&s_link, channel, out, out_cap, out_len,
                                 generation, deadline_us, received_ms);
}

srn_link_result_t TSKPTT_HostComm_SubmitLog(const uint8_t *line,
                                            size_t length)
{
    if (s_link_ready == 0u) {
        return SRN_LINK_ERR_BUSY;
    }
    return srn_link_submit_log(&s_link, line, length);
}

srn_link_result_t TSKPTT_HostComm_PublishEvent(
    uint8_t service,
    uint8_t message,
    uint16_t topic,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len)
{
    if (s_link_ready == 0u) {
        return SRN_LINK_ERR_BUSY;
    }
    return srn_link_publish(&s_link, service, message, topic, sequence,
                            payload, payload_len);
}

srn_link_result_t TSKPTT_HostComm_SubmitDeferred(
    uint8_t service,
    uint8_t message,
    uint32_t sequence,
    const uint8_t *request,
    size_t request_len,
    tskptt_host_comm_deferred_fn handler,
    void *user)
{
    if ((s_configured == 0u) || (handler == NULL) ||
        ((request == NULL) && (request_len != 0u)) ||
        (request_len > TSKPTT_HOST_COMM_WORK_PAYLOAD_MAX)) {
        return SRN_LINK_ERR_PARAM;
    }
    work_item_t work;
    memset(&work, 0, sizeof(work));
    work.kind = WORK_DEFERRED;
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
    return enqueue_work(&work);
}

#if defined(TSKPTT_HOST_COMM_TESTING)
srn_link_result_t TSKPTT_HostComm_TestStart(void)
{
    if (!TSKPTT_HostComm_InitResources()) {
        return SRN_LINK_ERR_FULL;
    }
    s_comm_task = xTaskGetCurrentTaskHandle();
    srn_link_result_t result = start_link();
    if (result == SRN_LINK_OK && !s_config.transport.init(s_config.transport.user)) return SRN_LINK_ERR_TRANSPORT;
    if (result == SRN_LINK_OK && !s_config.transport.start_rx(s_config.transport.user, s_comm_task, HOST_COMM_NOTIFY_RX))
        return SRN_LINK_ERR_TRANSPORT;
    return result;
}

void TSKPTT_HostComm_TestPollOnce(void)
{
    poll_once();
    publish_snapshot_if_due();
}

bool TSKPTT_HostComm_TestWorkerOnce(void)
{
    s_worker_task = xTaskGetCurrentTaskHandle();
    return worker_once(0u);
}
#endif
