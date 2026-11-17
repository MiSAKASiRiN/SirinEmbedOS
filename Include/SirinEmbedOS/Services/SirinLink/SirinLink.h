#ifndef SRNOS_SIRIN_LINK_H
#define SRNOS_SIRIN_LINK_H

#include "SirinEmbedOS/Services/SirinLink/Codec/Link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_SIRIN_LINK_DEFAULT_WAIT_MS       2u
#define SRNOS_SIRIN_LINK_DEFAULT_SNAPSHOT_MS   100u
#define SRNOS_SIRIN_LINK_WORK_PAYLOAD_MAX      256u
#define SRNOS_SIRIN_LINK_RESPONSE_BODY_MAX     128u
#define SRNOS_SIRIN_LINK_SOURCE_COUNT_MAX       32u
#define SRNOS_SIRIN_LINK_SOURCE_NAME_MAX        63u
#define SRNOS_SIRIN_LINK_SOURCE_UNIT_MAX        15u
#define SRNOS_SIRIN_LINK_SOURCE_RATE_MIN_HZ     5u
#define SRNOS_SIRIN_LINK_SOURCE_RATE_MAX_HZ     100u
#define SRNOS_SIRIN_LINK_SOURCE_RATE_STEP_HZ    5u
#define SRNOS_SIRIN_LINK_SOURCE_RATE_TOTAL_HZ   500u

typedef uint16_t (*SRNOS_SirinLinkExecFn)(void *user,
                                             const uint8_t *args,
                                             size_t args_len,
                                             uint8_t *response,
                                             size_t response_cap,
                                             size_t *response_len);

typedef void (*SRNOS_SirinLinkTelemetryConfigFn)(void *user,
                                                      bool enabled,
                                                      uint32_t period_us);

/* Registration is frozen before workers start. All callbacks run in HostComm
 * task context and must be bounded/nonblocking; never perform peripheral I/O. */
typedef struct {
    uint16_t stream_id, max_sample_size;
    uint32_t required_period_us; /* zero: accept any requested sampling period */
    uint32_t poll_interval_ms; /* 0: derive from subscription period; sampling stays independent */
    SRNOS_SirinLinkPolicy policy;
    uint16_t (*validate)(void *user, bool enabled, uint32_t period_us);
    SRNOS_SirinLinkTelemetryConfigFn on_config;
    void (*poll)(void *user, uint32_t now_ms);
    SRNOS_SirinLinkDeliveryFn on_delivery;
    void *user;
} SRNOS_SirinLinkTelemetryConfig;
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTelemetryEx(const SRNOS_SirinLinkTelemetryConfig *);

typedef uint16_t (*SRNOS_SirinLinkDeferredFn)(
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
} SRNOS_SirinLinkSourceSnapshot;

typedef struct {
    SRNOS_SirinLinkStats link;
    SRNOS_SirinLinkSourceSnapshot sources;
    /* UINT32_MAX means unmeasured; periodic stack scanning is disabled. */
    uint32_t comm_stack_high_water_words;
    uint32_t worker_stack_high_water_words;
    uint32_t deferred_depth;
    uint32_t deferred_high_water;
    uint8_t started;
    uint8_t reserved[3];
} SRNOS_SirinLinkSnapshot;

typedef struct SRNOS_SirinLinkRegistration SRNOS_SirinLinkRegistration;

typedef SRNOS_SirinLinkResult (*SRNOS_SirinLinkProjectSetupFn)(
    SRNOS_SirinLinkRegistration *registration,
    void *user);
typedef void (*SRNOS_SirinLinkSnapshotFn)(
    const SRNOS_SirinLinkSnapshot *snapshot,
    void *user);

/* Borrowed startup context, valid only inside sirin_link_setup. Registered
 * handlers execute in HostComm with no SDK lock held; keep them bounded. */
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterHandler(SRNOS_SirinLinkRegistration *,
    uint8_t service, uint8_t message, uint8_t kind_mask, SRNOS_SirinLinkQos,
    SRNOS_SirinLinkHandlerFn, void *user);
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTxTopic(SRNOS_SirinLinkRegistration *,
    uint8_t service, uint8_t message, SRNOS_SirinLinkQos);

SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterExec(
    uint16_t command_id,
    SRNOS_SirinLinkExecFn handler,
    void *user);
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterTelemetry(
    uint16_t stream_id,
    uint16_t max_sample_size,
    SRNOS_SirinLinkTelemetryConfigFn on_config,
    void *user);
SRNOS_SirinLinkResult SRNOS_SirinLinkRegisterControl(uint16_t channel,
                                                  uint8_t *storage,
                                                  size_t capacity);
/* Task-only copied submission; samples may be reused after return. A concurrent
 * cancel, resubscribe or disconnect rejects an unfinished submission with BUSY.
 * Initially disabled streams return PARAM. Rejected submissions have no delivery
 * callback; an accepted FIFO record may later report CANCELLED. Encoding and
 * application callbacks run outside the SDK critical section. */
SRNOS_SirinLinkResult SRNOS_SirinLinkPublishTelemetry(
    uint16_t stream_id,
    uint32_t sample_sequence,
    uint32_t device_time_us,
    uint16_t sample_period_us,
    uint8_t sample_count,
    const uint8_t *samples,
    size_t samples_len);
SRNOS_SirinLinkResult SRNOS_SirinLinkTakeControl(uint16_t channel,
                                              uint8_t *out,
                                              size_t out_cap,
                                              size_t *out_len,
                                              uint32_t *generation,
                                              uint32_t *deadline_us,
                                              uint32_t *received_ms);
SRNOS_SirinLinkResult SRNOS_SirinLinkSubmitLog(const uint8_t *line,
                                            size_t length);
SRNOS_SirinLinkResult SRNOS_SirinLinkPublishEvent(
    uint8_t service,
    uint8_t message,
    uint16_t topic,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len);
/* Only from the current HostComm request handler, matching its service/message/
 * sequence. At most one accepted deferred operation per request. */
SRNOS_SirinLinkResult SRNOS_SirinLinkSubmitDeferred(
    uint8_t service,
    uint8_t message,
    uint32_t sequence,
    const uint8_t *request,
    size_t request_len,
    SRNOS_SirinLinkDeferredFn handler,
    void *user);



#ifdef __cplusplus
}
#endif

#endif /* SRNOS_SIRIN_LINK_H */
