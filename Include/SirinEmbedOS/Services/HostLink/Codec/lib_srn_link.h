#ifndef OMX_LIB_SRN_LINK_H
#define OMX_LIB_SRN_LINK_H

#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs_prot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRN_LINK_TX_SLOT_COUNT          12u
#define SRN_LINK_FIFO_SLOT_LIMIT        8u
#define SRN_LINK_HANDLER_COUNT          24u
#define SRN_LINK_TX_TOPIC_COUNT         16u
#define SRN_LINK_REQUEST_CACHE_COUNT    4u
#define SRN_LINK_CONTROL_CHANNEL_COUNT  8u
#define SRN_LINK_EVENT_TRACK_COUNT      8u
#define SRN_LINK_REQUEST_CACHE_TTL_MS   2000u
#define SRN_LINK_Q1_FORCE_MS            10u

#if defined(__GNUC__)
#define SRN_LINK_ALIGN32 __attribute__((aligned(32)))
#else
#define SRN_LINK_ALIGN32
#endif

typedef enum {
    SRN_LINK_QOS_CONTROL = 0u,
    SRN_LINK_QOS_RELIABLE = 1u,
    SRN_LINK_QOS_TELEMETRY = 2u,
    SRN_LINK_QOS_LOG = 3u,
    SRN_LINK_QOS_COUNT = 4u,
} srn_link_qos_t;

/* Policy is local scheduling metadata; it does not change the V2 wire format. */
typedef enum { SRN_LINK_LATEST = 0, SRN_LINK_FIFO = 1 } srn_link_policy_t;
typedef enum { SRN_LINK_DELIVERED, SRN_LINK_FAILED, SRN_LINK_CANCELLED } srn_link_delivery_t;
/* Task context, short/nonblocking. Called once for accepted FIFO packets. */
typedef void (*srn_link_delivery_fn)(void *user, uint32_t tag, srn_link_delivery_t result);

typedef enum {
    SRN_LINK_SERVICE_SYSTEM = 0x00u,
    SRN_LINK_SERVICE_EXEC = 0x01u,
    SRN_LINK_SERVICE_CONFIG = 0x02u,
    SRN_LINK_SERVICE_CONTROL = 0x03u,
    SRN_LINK_SERVICE_TELEMETRY = 0x04u,
    SRN_LINK_SERVICE_TEST = 0x7Eu,
} srn_link_service_t;

typedef enum {
    SRN_LINK_SYSTEM_HELLO = 0x01u,
    SRN_LINK_SYSTEM_QOS_GET = 0x02u,
    SRN_LINK_SYSTEM_QOS_RESET = 0x03u,
    SRN_LINK_SYSTEM_ECHO = 0x04u,
} srn_link_system_message_t;

typedef enum {
    SRN_LINK_CONTROL_STATE = 0x01u,
} srn_link_control_message_t;

typedef enum {
    SRN_LINK_STATUS_OK = 0x0000u,
    SRN_LINK_STATUS_ACCEPTED = 0x0001u,
    SRN_LINK_STATUS_BAD_LENGTH = 0x0101u,
    SRN_LINK_STATUS_BAD_KIND = 0x0102u,
    SRN_LINK_STATUS_BAD_FLAGS = 0x0103u,
    SRN_LINK_STATUS_UNSUPPORTED_SERVICE = 0x0104u,
    SRN_LINK_STATUS_UNSUPPORTED_MESSAGE = 0x0105u,
    SRN_LINK_STATUS_BUSY = 0x0106u,
    SRN_LINK_STATUS_CONFLICT = 0x0107u,
    SRN_LINK_STATUS_INVALID_ARGUMENT = 0x0108u,
    SRN_LINK_STATUS_NOT_FOUND = 0x0109u,
    SRN_LINK_STATUS_TYPE_MISMATCH = 0x010Au,
    SRN_LINK_STATUS_OUT_OF_RANGE = 0x010Bu,
    SRN_LINK_STATUS_STORAGE_ERROR = 0x010Cu,
    SRN_LINK_STATUS_INTERNAL_ERROR = 0x01FFu,
    SRN_LINK_STATUS_DEFERRED_INTERNAL = 0xFFFFu,
} srn_link_status_t;

typedef enum {
    SRN_LINK_OK = 0,
    SRN_LINK_ERR_NULL = -1,
    SRN_LINK_ERR_PARAM = -2,
    SRN_LINK_ERR_FULL = -3,
    SRN_LINK_ERR_NOT_FOUND = -4,
    SRN_LINK_ERR_TRANSPORT = -5,
    SRN_LINK_ERR_BUSY = -6,
    SRN_LINK_ERR_BUFFER = -7,
} srn_link_result_t;

typedef enum {
    SRN_LINK_TX_ACCEPTED = 0,
    SRN_LINK_TX_BUSY = 1,
    SRN_LINK_TX_DISCONNECTED = 2,
    SRN_LINK_TX_ERROR = 3,
} srn_link_tx_result_t;

typedef uint32_t (*srn_link_now_ms_fn)(void *user);
typedef uint32_t (*srn_link_cycles_fn)(void *user);
typedef srn_link_tx_result_t (*srn_link_tx_submit_fn)(void *user,
                                                      const uint8_t *data,
                                                      size_t len);
typedef void (*srn_link_lock_fn)(void *user);

typedef struct {
    srn_link_now_ms_fn now_ms;
    srn_link_cycles_fn cycles;
    srn_link_tx_submit_fn tx_submit;
    srn_link_lock_fn lock;
    srn_link_lock_fn unlock;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    void *user;
} srn_link_config_t;

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t submitted;
    uint32_t sent;
    uint32_t rejected;
    uint32_t drop_new;
    uint32_t drop_old;
    uint32_t coalesced;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t last_wait_ms;
    uint32_t max_wait_ms;
} srn_link_qos_stats_t;

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t cobs_errors;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t version_errors;
    uint32_t flags_errors;
    uint32_t kind_errors;
    uint32_t unknown_service;
    uint32_t unknown_message;
    uint32_t rx_overflow;
    uint32_t sequence_gap;
    uint32_t sequence_duplicate;
    uint32_t sequence_out_of_order;
    uint32_t request_replay;
    uint32_t request_conflict;
    uint32_t transport_busy;
    uint32_t transport_errors;
    uint32_t transport_disconnects;
    uint32_t processing_cycles_total;
    uint32_t processing_cycles_max;
    uint32_t transport_connected;
    srn_link_qos_stats_t qos[SRN_LINK_QOS_COUNT];
} srn_link_stats_t;

typedef uint16_t (*srn_link_handler_fn)(void *user,
                                        const srn_cobs_prot_view_t *request,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len,
                                        uint8_t *response_flags);

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t kind_mask;
    uint8_t qos;
    srn_link_handler_fn handler;
    void *user;
} srn_link_handler_entry_t;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t qos;
    uint8_t used;
} srn_link_tx_topic_t;

typedef struct {
    uint8_t *data;
    uint16_t len;
    uint16_t key;
    uint32_t enqueued_ms;
    uint32_t order;
    uint32_t session;
    uint32_t tag;
    srn_link_delivery_fn on_delivery;
    void *delivery_user;
    uint8_t policy;
    uint8_t cancelled;
    uint8_t qos;
    uint8_t service;
    uint8_t message;
    uint8_t kind;
    uint8_t state;
    uint8_t reserved[3];
} srn_link_tx_slot_t;

typedef struct {
    uint8_t used;
    uint8_t pending;
    uint8_t service;
    uint8_t message;
    uint32_t sequence;
    uint32_t fingerprint;
    uint32_t expires_ms;
    uint16_t frame_len;
    uint8_t frame[SRN_COBS_PROT_MAX_FRAME_LEN];
} srn_link_request_cache_t;

typedef struct {
    uint8_t used;
    uint8_t pending;
    uint16_t channel;
    uint32_t generation;
    uint32_t deadline_us;
    uint32_t received_ms;
    uint8_t *storage;
    size_t capacity;
    size_t length;
} srn_link_control_mailbox_t;

typedef struct {
    uint8_t used;
    uint8_t service;
    uint8_t message;
    uint8_t reserved;
    uint32_t last_sequence;
} srn_link_event_track_t;

typedef struct srn_link {
    srn_link_config_t config;
    srn_cobs_stream_t rx_stream;
    SRN_LINK_ALIGN32 uint8_t rx_raw[SRN_COBS_PROT_MAX_RAW_LEN];
    srn_link_tx_slot_t slots[SRN_LINK_TX_SLOT_COUNT];
    srn_link_handler_entry_t handlers[SRN_LINK_HANDLER_COUNT];
    srn_link_tx_topic_t tx_topics[SRN_LINK_TX_TOPIC_COUNT];
    srn_link_request_cache_t request_cache[SRN_LINK_REQUEST_CACHE_COUNT];
    srn_link_control_mailbox_t control[SRN_LINK_CONTROL_CHANNEL_COUNT];
    srn_link_event_track_t event_track[SRN_LINK_EVENT_TRACK_COUNT];
    uint8_t response_payload[SRN_COBS_PROT_MAX_PAYLOAD];
    srn_link_stats_t stats;
    uint32_t enqueue_order;
    uint32_t session;
    int8_t in_flight;
    uint8_t rr_cursor;
    uint8_t q0_burst;
    uint8_t initialized;
} srn_link_t;

srn_link_result_t srn_link_init(srn_link_t *link,
                                const srn_link_config_t *config);
void srn_link_reset_rx(srn_link_t *link);
srn_link_result_t srn_link_feed_rx(srn_link_t *link,
                                   const uint8_t *data,
                                   size_t len);

srn_link_result_t srn_link_register_handler(srn_link_t *link,
                                            uint8_t service,
                                            uint8_t message,
                                            uint8_t kind_mask,
                                            srn_link_qos_t qos,
                                            srn_link_handler_fn handler,
                                            void *user);
srn_link_result_t srn_link_register_tx_topic(srn_link_t *link,
                                             uint8_t service,
                                             uint8_t message,
                                             srn_link_qos_t qos);

srn_link_result_t srn_link_publish(srn_link_t *link,
                                   uint8_t service,
                                   uint8_t message,
                                   uint16_t key,
                                   uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_len);
size_t srn_link_discard_topic(srn_link_t *link,
                              uint8_t service,
                              uint8_t message);
srn_link_result_t srn_link_submit_log(srn_link_t *link,
                                      const uint8_t *line,
                                      size_t len);
srn_link_result_t srn_link_send_response(srn_link_t *link,
                                         uint8_t service,
                                         uint8_t message,
                                         uint32_t sequence,
                                         uint16_t status,
                                         uint8_t flags,
                                         const uint8_t *body,
                                         size_t body_len);

/* FIFO packets are copied, never replaced/evicted, and share at most 8 slots. */
srn_link_result_t srn_link_publish_ex(srn_link_t *, uint8_t service, uint8_t message,
    uint16_t key, uint32_t sequence, const uint8_t *payload, size_t length,
    srn_link_policy_t policy, srn_link_delivery_fn done, void *user, uint32_t tag);
size_t srn_link_discard_stream(srn_link_t *, uint8_t service, uint8_t message, uint16_t key);
/* In-flight memory remains owned by transport until on_tx_complete. */
void srn_link_reset_session(srn_link_t *);

srn_link_result_t srn_link_service_tx(srn_link_t *link);
void srn_link_on_tx_complete(srn_link_t *link, bool success);
bool srn_link_has_pending_tx(const srn_link_t *link);

srn_link_result_t srn_link_control_register(srn_link_t *link,
                                            uint16_t channel,
                                            uint8_t *storage,
                                            size_t capacity);
srn_link_result_t srn_link_control_take(srn_link_t *link,
                                        uint16_t channel,
                                        uint8_t *out,
                                        size_t out_cap,
                                        size_t *out_len,
                                        uint32_t *generation,
                                        uint32_t *deadline_us,
                                        uint32_t *received_ms);

void srn_link_get_stats(const srn_link_t *link, srn_link_stats_t *out);
void srn_link_reset_stats(srn_link_t *link);
void srn_link_record_processing_cycles(srn_link_t *link, uint32_t cycles);
void srn_link_record_rx_overflow(srn_link_t *link, uint32_t bytes);
void srn_link_set_transport_connected(srn_link_t *link, bool connected);

#define SRN_LINK_KIND_MASK(kind) ((uint8_t)(1u << (uint8_t)(kind)))

#ifdef __cplusplus
}
#endif

#endif /* OMX_LIB_SRN_LINK_H */
