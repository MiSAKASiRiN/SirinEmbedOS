#pragma once
/* SDK transport/dispatcher state; never exported to parent projects. */
#include "SirinEmbedOS/Services/SirinLink/Codec/Link.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SRNOS_SIRIN_LINK_TX_ACCEPTED = 0,
    SRNOS_SIRIN_LINK_TX_BUSY = 1,
    SRNOS_SIRIN_LINK_TX_DISCONNECTED = 2,
    SRNOS_SIRIN_LINK_TX_ERROR = 3,
} SRNOS_SirinLinkCoreTxResult;

typedef uint32_t (*SRNOS_SirinLinkCoreNowMsFn)(void *user);
typedef uint32_t (*SRNOS_SirinLinkCoreCyclesFn)(void *user);
typedef SRNOS_SirinLinkCoreTxResult (*SRNOS_SirinLinkCoreTxSubmitFn)(void *user,
                                                      const uint8_t *data,
                                                      size_t len);
typedef void (*SRNOS_SirinLinkCoreLockFn)(void *user);

typedef struct {
    SRNOS_SirinLinkCoreNowMsFn now_ms;
    SRNOS_SirinLinkCoreCyclesFn cycles;
    SRNOS_SirinLinkCoreTxSubmitFn tx_submit;
    SRNOS_SirinLinkCoreLockFn lock;
    SRNOS_SirinLinkCoreLockFn unlock;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    void *user;
} SRNOS_SirinLinkCoreConfig;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t kind_mask;
    uint8_t qos;
    SRNOS_SirinLinkHandlerFn handler;
    void *user;
} SRNOS_SirinLinkCoreHandlerEntry;

typedef struct {
    uint8_t service;
    uint8_t message;
    uint8_t qos;
    uint8_t used;
} SRNOS_SirinLinkCoreTxTopic;

typedef struct {
    uint8_t *data;
    uint16_t len;
    uint16_t key;
    uint32_t enqueued_ms;
    uint32_t order;
    uint32_t session;
    uint32_t tag;
    SRNOS_SirinLinkDeliveryFn on_delivery;
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
} SRNOS_SirinLinkCoreTxSlot;

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
} SRNOS_SirinLinkCoreRequestCache;

#define SRNOS_SIRIN_LINK_ACTIVE_REQUEST_COUNT 10u
/* Distinct from the short completed-response replay cache. No TTL eviction. */
typedef struct {
    uint8_t used, service, message, deferred_submitted;
    uint32_t sequence, fingerprint;
} SRNOS_SirinLinkActiveRequest;

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
} SRNOS_SirinLinkCoreControlMailbox;

typedef struct {
    uint8_t used;
    uint8_t service;
    uint8_t message;
    uint8_t reserved;
    uint32_t last_sequence;
} SRNOS_SirinLinkCoreEventTrack;

typedef struct SRNOS_Link {
    SRNOS_SirinLinkCoreConfig config;
    SRNOS_COBSStream rx_stream;
    SRNOS_SIRIN_LINK_ALIGN32 uint8_t rx_raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    SRNOS_SirinLinkCoreTxSlot slots[SRNOS_SIRIN_LINK_TX_SLOT_COUNT];
    SRNOS_SirinLinkCoreHandlerEntry handlers[SRNOS_SIRIN_LINK_HANDLER_COUNT];
    SRNOS_SirinLinkCoreTxTopic tx_topics[SRNOS_SIRIN_LINK_TX_TOPIC_COUNT];
    SRNOS_SirinLinkCoreRequestCache request_cache[SRNOS_SIRIN_LINK_REQUEST_CACHE_COUNT];
    SRNOS_SirinLinkActiveRequest active_requests[SRNOS_SIRIN_LINK_ACTIVE_REQUEST_COUNT];
    SRNOS_SirinLinkActiveRequest *dispatching_request;
    SRNOS_SirinLinkCoreControlMailbox control[SRNOS_SIRIN_LINK_CONTROL_CHANNEL_COUNT];
    SRNOS_SirinLinkCoreEventTrack event_track[SRNOS_SIRIN_LINK_EVENT_TRACK_COUNT];
    uint8_t response_payload[SRNOS_PROTOCOL_MAX_PAYLOAD];
    SRNOS_SirinLinkStats stats;
    uint32_t enqueue_order;
    uint32_t session;
    int8_t in_flight;
    uint8_t rr_cursor;
    uint8_t q0_burst;
    uint8_t initialized;
    uint8_t registrations_frozen;
} SRNOS_SirinLinkContext;

SRNOS_SirinLinkResult SRNOS_SirinLinkCoreInit(SRNOS_SirinLinkContext *link,
                                const SRNOS_SirinLinkCoreConfig *config);
void SRNOS_SirinLinkCoreResetRx(SRNOS_SirinLinkContext *link);
SRNOS_SirinLinkResult SRNOS_SirinLinkCoreFeedRx(SRNOS_SirinLinkContext *link,
                                   const uint8_t *data,
                                   size_t len);

SRNOS_SirinLinkResult SRNOS_SirinLinkCoreRegisterHandler(SRNOS_SirinLinkContext *link,
                                            uint8_t service,
                                            uint8_t message,
                                            uint8_t kind_mask,
                                            SRNOS_SirinLinkQos qos,
                                            SRNOS_SirinLinkHandlerFn handler,
                                            void *user);
SRNOS_SirinLinkResult SRNOS_SirinLinkCoreRegisterTxTopic(SRNOS_SirinLinkContext *link,
                                             uint8_t service,
                                             uint8_t message,
                                             SRNOS_SirinLinkQos qos);

SRNOS_SirinLinkResult SRNOS_SirinLinkCorePublish(SRNOS_SirinLinkContext *link,
                                   uint8_t service,
                                   uint8_t message,
                                   uint16_t key,
                                   uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_len);
size_t SRNOS_SirinLinkCoreDiscardTopic(SRNOS_SirinLinkContext *link,
                              uint8_t service,
                              uint8_t message);
SRNOS_SirinLinkResult SRNOS_SirinLinkCoreSubmitLog(SRNOS_SirinLinkContext *link,
                                      const uint8_t *line,
                                      size_t len);
SRNOS_SirinLinkResult SRNOS_SirinLinkCoreSendResponse(SRNOS_SirinLinkContext *link,
                                         uint8_t service,
                                         uint8_t message,
                                         uint32_t sequence,
                                         uint16_t status,
                                         uint8_t flags,
                                         const uint8_t *body,
                                         size_t body_len);

/* FIFO packets are copied, never replaced/evicted, and share at most 8 slots. */
SRNOS_SirinLinkResult SRNOS_SirinLinkCorePublishEx(SRNOS_SirinLinkContext *, uint8_t service, uint8_t message,
    uint16_t key, uint32_t sequence, const uint8_t *payload, size_t length,
    SRNOS_SirinLinkPolicy policy, SRNOS_SirinLinkDeliveryFn done, void *user, uint32_t tag);
size_t SRNOS_SirinLinkCoreDiscardStream(SRNOS_SirinLinkContext *, uint8_t service, uint8_t message, uint16_t key);
/* In-flight memory remains owned by transport until on_tx_complete. */
void SRNOS_SirinLinkCoreResetSession(SRNOS_SirinLinkContext *);

SRNOS_SirinLinkResult SRNOS_SirinLinkCoreServiceTx(SRNOS_SirinLinkContext *link);
void SRNOS_SirinLinkCoreOnTxComplete(SRNOS_SirinLinkContext *link, bool success);
/* HostComm only; each callback runs outside the link lock, at most once. */
void SRNOS_SirinLinkCoreDrainDeliveries(SRNOS_SirinLinkContext *link);
bool SRNOS_SirinLinkCoreHasPendingTx(const SRNOS_SirinLinkContext *link);

SRNOS_SirinLinkResult SRNOS_SirinLinkCoreControlRegister(SRNOS_SirinLinkContext *link,
                                            uint16_t channel,
                                            uint8_t *storage,
                                            size_t capacity);
SRNOS_SirinLinkResult SRNOS_SirinLinkCoreControlTake(SRNOS_SirinLinkContext *link,
                                        uint16_t channel,
                                        uint8_t *out,
                                        size_t out_cap,
                                        size_t *out_len,
                                        uint32_t *generation,
                                        uint32_t *deadline_us,
                                        uint32_t *received_ms);

void SRNOS_SirinLinkCoreGetStats(const SRNOS_SirinLinkContext *link, SRNOS_SirinLinkStats *out);
void SRNOS_SirinLinkCoreResetStats(SRNOS_SirinLinkContext *link);
void SRNOS_SirinLinkCoreRecordProcessingCycles(SRNOS_SirinLinkContext *link, uint32_t cycles);
void SRNOS_SirinLinkCoreRecordRxOverflow(SRNOS_SirinLinkContext *link, uint32_t bytes);
void SRNOS_SirinLinkCoreSetTransportConnected(SRNOS_SirinLinkContext *link, bool connected);


/* Only HostComm may submit work for its currently executing request handler. */
bool SRNOS_SirinLinkCoreCanDefer(SRNOS_SirinLinkContext *,uint8_t,uint8_t,uint32_t);
void SRNOS_SirinLinkCoreMarkDeferred(SRNOS_SirinLinkContext *);

#ifdef __cplusplus
}
#endif
