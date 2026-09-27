#ifndef OMX_TSKPTT_HOST_COMM_H
#define OMX_TSKPTT_HOST_COMM_H

#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSKPTT_HOST_COMM_DEFAULT_WAIT_MS       2u
#define TSKPTT_HOST_COMM_DEFAULT_SNAPSHOT_MS   100u
#define TSKPTT_HOST_COMM_WORK_PAYLOAD_MAX      256u
#define TSKPTT_HOST_COMM_RESPONSE_BODY_MAX     128u
#define TSKPTT_HOST_COMM_SOURCE_COUNT_MAX       32u
#define TSKPTT_HOST_COMM_SOURCE_NAME_MAX        63u
#define TSKPTT_HOST_COMM_SOURCE_UNIT_MAX        15u
#define TSKPTT_HOST_COMM_SOURCE_RATE_MIN_HZ     5u
#define TSKPTT_HOST_COMM_SOURCE_RATE_MAX_HZ     100u
#define TSKPTT_HOST_COMM_SOURCE_RATE_STEP_HZ    5u
#define TSKPTT_HOST_COMM_SOURCE_RATE_TOTAL_HZ   500u

typedef enum {
    TSKPTT_HOST_COMM_SOURCE_FLOAT32 = 1u,
    TSKPTT_HOST_COMM_SOURCE_INT32 = 2u,
    TSKPTT_HOST_COMM_SOURCE_UINT32 = 3u,
    TSKPTT_HOST_COMM_SOURCE_BOOL = 4u,
} tskptt_host_comm_source_type_t;

typedef union {
    float f32;
    int32_t i32;
    uint32_t u32;
    bool boolean;
} tskptt_host_comm_source_value_t;

typedef bool (*tskptt_host_comm_source_read_fn)(
    void *user,
    tskptt_host_comm_source_value_t *value);

typedef struct {
    const char *name;
    const char *unit;
    tskptt_host_comm_source_type_t type;
    const volatile void *value_ptr;
    tskptt_host_comm_source_read_fn read;
    void *user;
} tskptt_host_comm_source_config_t;

typedef uint16_t (*tskptt_host_comm_exec_fn)(void *user,
                                             const uint8_t *args,
                                             size_t args_len,
                                             uint8_t *response,
                                             size_t response_cap,
                                             size_t *response_len);

typedef void (*tskptt_host_comm_telemetry_config_fn)(void *user,
                                                      bool enabled,
                                                      uint32_t period_us);

/* Registration is frozen before workers start. All callbacks run in HostComm
 * task context and must be bounded/nonblocking; never perform peripheral I/O. */
typedef struct {
    uint16_t stream_id, max_sample_size;
    uint32_t required_period_us; /* zero: legacy unrestricted period */
    srn_link_policy_t policy;
    uint16_t (*validate)(void *user, bool enabled, uint32_t period_us);
    tskptt_host_comm_telemetry_config_fn on_config;
    void (*poll)(void *user, uint32_t now_ms);
    srn_link_delivery_fn on_delivery;
    void *user;
} tskptt_host_comm_telemetry_config_t;
srn_link_result_t TSKPTT_HostComm_RegisterTelemetryEx(const tskptt_host_comm_telemetry_config_t *);

typedef uint16_t (*tskptt_host_comm_deferred_fn)(
    void *user,
    uint8_t service,
    uint8_t message,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t response_cap,
    size_t *response_len,
    uint8_t *response_flags);

typedef struct {
    void *user;
    bool (*init)(void *user);
    bool (*start_rx)(void *user, TaskHandle_t notify_task, uint32_t notify_bits);
    size_t (*rx_read)(void *user, uint8_t *out, size_t capacity);
    uint32_t (*rx_overflow_bytes)(void *user);
    srn_link_tx_result_t (*tx_submit)(void *user,
                                      const uint8_t *data,
                                      size_t length);
    bool (*tx_take_complete)(void *user, bool *success);
    bool (*is_connected)(void *user);
    uint32_t (*session_generation)(void *user); /* optional; detects quick DTR reopen */
} tskptt_host_comm_transport_t;

typedef struct {
    uint32_t registered_count;
    uint32_t active_count;
    uint32_t total_rate_hz;
    uint32_t batches;
    uint32_t records;
    uint32_t read_failures;
    uint32_t publish_failures;
    uint32_t missed_periods;
    uint32_t cycles_total;
    uint32_t cycles_max;
} tskptt_host_comm_source_snapshot_t;

typedef struct {
    srn_link_stats_t link;
    tskptt_host_comm_source_snapshot_t sources;
    uint32_t comm_stack_high_water_words;
    uint32_t worker_stack_high_water_words;
    uint32_t deferred_depth;
    uint32_t deferred_high_water;
    uint8_t started;
    uint8_t reserved[3];
} tskptt_host_comm_snapshot_t;

typedef srn_link_result_t (*tskptt_host_comm_project_setup_fn)(
    srn_link_t *link,
    void *user);
typedef void (*tskptt_host_comm_snapshot_fn)(
    const tskptt_host_comm_snapshot_t *snapshot,
    void *user);

typedef struct {
    tskptt_host_comm_transport_t transport;
    srn_link_now_ms_fn now_ms;
    srn_link_cycles_fn cycles;
    void *clock_user;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    uint8_t *rx_drain_buffer;
    size_t rx_drain_buffer_size;
    tskptt_host_comm_project_setup_fn project_setup;
    tskptt_host_comm_snapshot_fn snapshot;
    void *project_user;
    uint32_t task_wait_ms;
    uint32_t snapshot_period_ms;
    bool disable_stack_scan;
} tskptt_host_comm_config_t;

srn_link_result_t TSKPTT_HostComm_Configure(
    const tskptt_host_comm_config_t *config);
bool TSKPTT_HostComm_InitResources(void);
/* Prepare/freeze registrations before any service worker runs. */
srn_link_result_t TSKPTT_HostComm_PrepareLink(void);
void TSKPTT_HostComm_Task(void *argument);
void TSKPTT_HostComm_WorkerTask(void *argument);

srn_link_result_t TSKPTT_HostComm_RegisterExec(
    uint16_t command_id,
    tskptt_host_comm_exec_fn handler,
    void *user);
srn_link_result_t TSKPTT_HostComm_RegisterTelemetry(
    uint16_t stream_id,
    uint16_t max_sample_size,
    tskptt_host_comm_telemetry_config_fn on_config,
    void *user);
srn_link_result_t TSKPTT_HostComm_RegisterTelemetrySource(
    const tskptt_host_comm_source_config_t *config,
    uint16_t *index_out);
srn_link_result_t TSKPTT_HostComm_RegisterControl(uint16_t channel,
                                                  uint8_t *storage,
                                                  size_t capacity);
srn_link_result_t TSKPTT_HostComm_PublishTelemetry(
    uint16_t stream_id,
    uint32_t sample_sequence,
    uint32_t device_time_us,
    uint16_t sample_period_us,
    uint8_t sample_count,
    const uint8_t *samples,
    size_t samples_len);
srn_link_result_t TSKPTT_HostComm_TakeControl(uint16_t channel,
                                              uint8_t *out,
                                              size_t out_cap,
                                              size_t *out_len,
                                              uint32_t *generation,
                                              uint32_t *deadline_us,
                                              uint32_t *received_ms);
srn_link_result_t TSKPTT_HostComm_SubmitLog(const uint8_t *line,
                                            size_t length);
srn_link_result_t TSKPTT_HostComm_PublishEvent(
    uint8_t service,
    uint8_t message,
    uint16_t topic,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len);
srn_link_result_t TSKPTT_HostComm_SubmitDeferred(
    uint8_t service,
    uint8_t message,
    uint32_t sequence,
    const uint8_t *request,
    size_t request_len,
    tskptt_host_comm_deferred_fn handler,
    void *user);

#if defined(TSKPTT_HOST_COMM_TESTING)
srn_link_result_t TSKPTT_HostComm_TestStart(void);
void TSKPTT_HostComm_TestPollOnce(void);
bool TSKPTT_HostComm_TestWorkerOnce(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OMX_TSKPTT_HOST_COMM_H */
