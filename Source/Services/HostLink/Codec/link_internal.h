#pragma once
/* SDK transport/dispatcher state; never exported to parent projects. */
#include "SirinEmbedOS/Services/HostLink/Codec/Link.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SRNOS_HOST_LINK_TX_ACCEPTED = 0,
    SRNOS_HOST_LINK_TX_BUSY = 1,
    SRNOS_HOST_LINK_TX_DISCONNECTED = 2,
    SRNOS_HOST_LINK_TX_ERROR = 3,
} SRNOS_HostLinkCoreTxResult;

typedef uint32_t (*SRNOS_HostLinkCoreNowMsFn)(void *user);
typedef uint32_t (*SRNOS_HostLinkCoreCyclesFn)(void *user);
typedef SRNOS_HostLinkCoreTxResult (*SRNOS_HostLinkCoreTxSubmitFn)(void *user,
                                                      const uint8_t *data,
                                                      size_t len);
typedef void (*SRNOS_HostLinkCoreLockFn)(void *user);

typedef struct {
    SRNOS_HostLinkCoreNowMsFn now_ms;
    SRNOS_HostLinkCoreCyclesFn cycles;
    SRNOS_HostLinkCoreTxSubmitFn tx_submit;
    SRNOS_HostLinkCoreLockFn lock;
    SRNOS_HostLinkCoreLockFn unlock;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    void *user;
} SRNOS_HostLinkCoreConfig;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t kind_mask;
    uint8_t qos;
    SRNOS_HostLinkHandlerFn handler;
    void *user;
} SRNOS_HostLinkCoreHandlerEntry;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t qos;
    uint8_t used;
} SRNOS_HostLinkCoreTxTopic;

typedef struct {
    uint8_t *data;
    uint16_t len;
    uint16_t key;
    uint32_t enqueued_ms;
    uint32_t order;
    uint32_t session;
    uint32_t tag;
    SRNOS_HostLinkDeliveryFn on_delivery;
    void *delivery_user;
    uint8_t policy;
    uint8_t cancelled;
    uint8_t qos;
    uint8_t service;
    uint8_t message;
    uint8_t kind;
    uint8_t state;
    uint8_t delivery_result;
    uint8_t reserved[2];
} SRNOS_HostLinkCoreTxSlot;

typedef struct {
    uint8_t used;
    uint8_t reserved;
    uint8_t service;
    uint8_t message;
    uint32_t sequence;
    uint32_t fingerprint;
    uint32_t expires_ms;
    uint16_t frame_len;
    uint8_t frame[SRNOS_PROTOCOL_MAX_FRAME_LEN];
} SRNOS_HostLinkCoreRequestCache;

#define SRNOS_HOST_LINK_ACTIVE_REQUEST_COUNT 10u
/* Distinct from the short completed-response replay cache. No TTL eviction. */
typedef struct {
    uint8_t used, service, message, deferred_submitted;
    uint32_t sequence, fingerprint;
} SRNOS_HostLinkActiveRequest;

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
} SRNOS_HostLinkCoreControlMailbox;

typedef struct {
    uint8_t used;
    uint8_t service;
    uint8_t message;
    uint8_t reserved;
    uint32_t last_sequence;
} SRNOS_HostLinkCoreEventTrack;

typedef struct SRNOS_Link {
    SRNOS_HostLinkCoreConfig config;
    SRNOS_COBSStream rx_stream;
    SRNOS_HOST_LINK_ALIGN32 uint8_t rx_raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    SRNOS_HostLinkCoreTxSlot slots[SRNOS_HOST_LINK_TX_SLOT_COUNT];
    SRNOS_HostLinkCoreHandlerEntry handlers[SRNOS_HOST_LINK_HANDLER_COUNT];
    SRNOS_HostLinkCoreTxTopic tx_topics[SRNOS_HOST_LINK_TX_TOPIC_COUNT];
    SRNOS_HostLinkCoreRequestCache request_cache[SRNOS_HOST_LINK_REQUEST_CACHE_COUNT];
    SRNOS_HostLinkActiveRequest active_requests[SRNOS_HOST_LINK_ACTIVE_REQUEST_COUNT];
    SRNOS_HostLinkActiveRequest *dispatching_request;
    SRNOS_HostLinkCoreControlMailbox control[SRNOS_HOST_LINK_CONTROL_CHANNEL_COUNT];
    SRNOS_HostLinkCoreEventTrack event_track[SRNOS_HOST_LINK_EVENT_TRACK_COUNT];
    uint8_t response_payload[SRNOS_PROTOCOL_MAX_PAYLOAD];
    SRNOS_HostLinkStats stats;
    uint32_t enqueue_order;
    uint32_t session;
    int8_t in_flight;
    uint8_t rr_cursor;
    uint8_t q0_burst;
    uint8_t initialized;
    uint8_t registrations_frozen;
} SRNOS_HostLinkContext;

SRNOS_HostLinkResult SRNOS_HostLinkCoreInit(SRNOS_HostLinkContext *link,
                                const SRNOS_HostLinkCoreConfig *config);
void SRNOS_HostLinkCoreResetRx(SRNOS_HostLinkContext *link);
SRNOS_HostLinkResult SRNOS_HostLinkCoreFeedRx(SRNOS_HostLinkContext *link,
                                   const uint8_t *data,
                                   size_t len);

SRNOS_HostLinkResult SRNOS_HostLinkCoreRegisterHandler(SRNOS_HostLinkContext *link,
                                            uint8_t service,
                                            uint8_t message,
                                            uint8_t kind_mask,
                                            SRNOS_HostLinkQos qos,
                                            SRNOS_HostLinkHandlerFn handler,
                                            void *user);
SRNOS_HostLinkResult SRNOS_HostLinkCoreRegisterTxTopic(SRNOS_HostLinkContext *link,
                                             uint8_t service,
                                             uint8_t message,
                                             SRNOS_HostLinkQos qos);

SRNOS_HostLinkResult SRNOS_HostLinkCorePublish(SRNOS_HostLinkContext *link,
                                   uint8_t service,
                                   uint8_t message,
                                   uint16_t key,
                                   uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_len);
size_t SRNOS_HostLinkCoreDiscardTopic(SRNOS_HostLinkContext *link,
                              uint8_t service,
                              uint8_t message);
SRNOS_HostLinkResult SRNOS_HostLinkCoreSubmitLog(SRNOS_HostLinkContext *link,
                                      const uint8_t *line,
                                      size_t len);
SRNOS_HostLinkResult SRNOS_HostLinkCoreSendResponse(SRNOS_HostLinkContext *link,
                                         uint8_t service,
                                         uint8_t message,
                                         uint32_t sequence,
                                         uint16_t status,
                                         uint8_t flags,
                                         const uint8_t *body,
                                         size_t body_len);

/* FIFO packets are copied, never replaced/evicted, and share at most 8 slots. */
SRNOS_HostLinkResult SRNOS_HostLinkCorePublishEx(SRNOS_HostLinkContext *, uint8_t service, uint8_t message,
    uint16_t key, uint32_t sequence, const uint8_t *payload, size_t length,
    SRNOS_HostLinkPolicy policy, SRNOS_HostLinkDeliveryFn done, void *user, uint32_t tag);
size_t SRNOS_HostLinkCoreDiscardStream(SRNOS_HostLinkContext *, uint8_t service, uint8_t message, uint16_t key);
/* In-flight memory remains owned by transport until on_tx_complete. */
void SRNOS_HostLinkCoreResetSession(SRNOS_HostLinkContext *);

SRNOS_HostLinkResult SRNOS_HostLinkCoreServiceTx(SRNOS_HostLinkContext *link);
void SRNOS_HostLinkCoreOnTxComplete(SRNOS_HostLinkContext *link, bool success);
/* HostComm only; each callback runs outside the link lock, at most once. */
void SRNOS_HostLinkCoreDrainDeliveries(SRNOS_HostLinkContext *link);
bool SRNOS_HostLinkCoreHasPendingTx(const SRNOS_HostLinkContext *link);

SRNOS_HostLinkResult SRNOS_HostLinkCoreControlRegister(SRNOS_HostLinkContext *link,
                                            uint16_t channel,
                                            uint8_t *storage,
                                            size_t capacity);
SRNOS_HostLinkResult SRNOS_HostLinkCoreControlTake(SRNOS_HostLinkContext *link,
                                        uint16_t channel,
                                        uint8_t *out,
                                        size_t out_cap,
                                        size_t *out_len,
                                        uint32_t *generation,
                                        uint32_t *deadline_us,
                                        uint32_t *received_ms);

void SRNOS_HostLinkCoreGetStats(const SRNOS_HostLinkContext *link, SRNOS_HostLinkStats *out);
void SRNOS_HostLinkCoreResetStats(SRNOS_HostLinkContext *link);
void SRNOS_HostLinkCoreRecordProcessingCycles(SRNOS_HostLinkContext *link, uint32_t cycles);
void SRNOS_HostLinkCoreRecordRxOverflow(SRNOS_HostLinkContext *link, uint32_t bytes);
void SRNOS_HostLinkCoreSetTransportConnected(SRNOS_HostLinkContext *link, bool connected);


/* Only HostComm may submit work for its currently executing request handler. */
bool SRNOS_HostLinkCoreCanDefer(SRNOS_HostLinkContext *,uint8_t,uint8_t,uint32_t);
void SRNOS_HostLinkCoreMarkDeferred(SRNOS_HostLinkContext *);

#ifdef __cplusplus
}
#endif
