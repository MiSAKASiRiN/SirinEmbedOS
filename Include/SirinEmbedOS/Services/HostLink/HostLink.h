#ifndef SRNOS_HOST_LINK_H
#define SRNOS_HOST_LINK_H

#include "SirinEmbedOS/Services/HostLink/Codec/Link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_HOST_LINK_DEFAULT_WAIT_MS       2u
#define SRNOS_HOST_LINK_DEFAULT_SNAPSHOT_MS   100u
#define SRNOS_HOST_LINK_WORK_PAYLOAD_MAX      256u
#define SRNOS_HOST_LINK_RESPONSE_BODY_MAX     128u
#define SRNOS_HOST_LINK_SOURCE_COUNT_MAX       32u
#define SRNOS_HOST_LINK_SOURCE_NAME_MAX        63u
#define SRNOS_HOST_LINK_SOURCE_UNIT_MAX        15u
#define SRNOS_HOST_LINK_SOURCE_RATE_MIN_HZ     5u
#define SRNOS_HOST_LINK_SOURCE_RATE_MAX_HZ     100u
#define SRNOS_HOST_LINK_SOURCE_RATE_STEP_HZ    5u
#define SRNOS_HOST_LINK_SOURCE_RATE_TOTAL_HZ   500u

typedef uint16_t (*SRNOS_HostLinkExecFn)(void *user,
                                             const uint8_t *args,
                                             size_t args_len,
                                             uint8_t *response,
                                             size_t response_cap,
                                             size_t *response_len);

typedef void (*SRNOS_HostLinkTelemetryConfigFn)(void *user,
                                                      bool enabled,
                                                      uint32_t period_us);

/* Registration is frozen before workers start. All callbacks run in HostComm
 * task context and must be bounded/nonblocking; never perform peripheral I/O. */
typedef struct {
    uint16_t stream_id, max_sample_size;
    uint32_t required_period_us; /* zero: accept any requested sampling period */
    uint32_t poll_interval_ms; /* 0: derive from subscription period; sampling stays independent */
    SRNOS_HostLinkPolicy policy;
    uint16_t (*validate)(void *user, bool enabled, uint32_t period_us);
    SRNOS_HostLinkTelemetryConfigFn on_config;
    void (*poll)(void *user, uint32_t now_ms);
    SRNOS_HostLinkDeliveryFn on_delivery;
    void *user;
} SRNOS_HostLinkTelemetryConfig;
SRNOS_HostLinkResult SRNOS_HostLinkRegisterTelemetryEx(const SRNOS_HostLinkTelemetryConfig *);

typedef uint16_t (*SRNOS_HostLinkDeferredFn)(
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
} SRNOS_HostLinkSourceSnapshot;

typedef struct {
    SRNOS_HostLinkStats link;
    SRNOS_HostLinkSourceSnapshot sources;
    /* UINT32_MAX means unmeasured; periodic stack scanning is disabled. */
    uint32_t comm_stack_high_water_words;
    uint32_t worker_stack_high_water_words;
    uint32_t deferred_depth;
    uint32_t deferred_high_water;
    uint8_t started;
    uint8_t reserved[3];
} SRNOS_HostLinkSnapshot;

typedef struct SRNOS_HostLinkRegistration SRNOS_HostLinkRegistration;

typedef SRNOS_HostLinkResult (*SRNOS_HostLinkProjectSetupFn)(
    SRNOS_HostLinkRegistration *registration,
    void *user);
typedef void (*SRNOS_HostLinkSnapshotFn)(
    const SRNOS_HostLinkSnapshot *snapshot,
    void *user);

/* Borrowed startup context, valid only inside host_link_setup. Registered
 * handlers execute in HostComm with no SDK lock held; keep them bounded. */
SRNOS_HostLinkResult SRNOS_HostLinkRegisterHandler(SRNOS_HostLinkRegistration *,
    uint8_t service, uint8_t message, uint8_t kind_mask, SRNOS_HostLinkQos,
    SRNOS_HostLinkHandlerFn, void *user);
SRNOS_HostLinkResult SRNOS_HostLinkRegisterTxTopic(SRNOS_HostLinkRegistration *,
    uint8_t service, uint8_t message, SRNOS_HostLinkQos);

SRNOS_HostLinkResult SRNOS_HostLinkRegisterExec(
    uint16_t command_id,
    SRNOS_HostLinkExecFn handler,
    void *user);
SRNOS_HostLinkResult SRNOS_HostLinkRegisterTelemetry(
    uint16_t stream_id,
    uint16_t max_sample_size,
    SRNOS_HostLinkTelemetryConfigFn on_config,
    void *user);
SRNOS_HostLinkResult SRNOS_HostLinkRegisterControl(uint16_t channel,
                                                  uint8_t *storage,
                                                  size_t capacity);
SRNOS_HostLinkResult SRNOS_HostLinkPublishTelemetry(
    uint16_t stream_id,
    uint32_t sample_sequence,
    uint32_t device_time_us,
    uint16_t sample_period_us,
    uint8_t sample_count,
    const uint8_t *samples,
    size_t samples_len);
SRNOS_HostLinkResult SRNOS_HostLinkTakeControl(uint16_t channel,
                                              uint8_t *out,
                                              size_t out_cap,
                                              size_t *out_len,
                                              uint32_t *generation,
                                              uint32_t *deadline_us,
                                              uint32_t *received_ms);
SRNOS_HostLinkResult SRNOS_HostLinkSubmitLog(const uint8_t *line,
                                            size_t length);
SRNOS_HostLinkResult SRNOS_HostLinkPublishEvent(
    uint8_t service,
    uint8_t message,
    uint16_t topic,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len);
/* Only from the current HostComm request handler, matching its service/message/
 * sequence. At most one accepted deferred operation per request. */
SRNOS_HostLinkResult SRNOS_HostLinkSubmitDeferred(
    uint8_t service,
    uint8_t message,
    uint32_t sequence,
    const uint8_t *request,
    size_t request_len,
    SRNOS_HostLinkDeferredFn handler,
    void *user);



#ifdef __cplusplus
}
#endif

#endif /* SRNOS_HOST_LINK_H */
