#ifndef SRNOS_SIRIN_LINK_TYPES_H
#define SRNOS_SIRIN_LINK_TYPES_H

#include "SirinEmbedOS/Services/SirinLink/Codec/COBS.h"
#include "SirinEmbedOS/Services/SirinLink/Codec/Protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_SIRIN_LINK_TX_SLOT_COUNT          12u
#define SRNOS_SIRIN_LINK_FIFO_SLOT_LIMIT        8u
#define SRNOS_SIRIN_LINK_HANDLER_COUNT          40u
#define SRNOS_SIRIN_LINK_TX_TOPIC_COUNT         16u
#define SRNOS_SIRIN_LINK_REQUEST_CACHE_COUNT    4u
#define SRNOS_SIRIN_LINK_CONTROL_CHANNEL_COUNT  8u
#define SRNOS_SIRIN_LINK_EVENT_TRACK_COUNT      8u
#define SRNOS_SIRIN_LINK_REQUEST_CACHE_TTL_MS   2000u
#define SRNOS_SIRIN_LINK_Q1_FORCE_MS            10u

#if defined(__GNUC__)
#define SRNOS_SIRIN_LINK_ALIGN32 __attribute__((aligned(32)))
#else
#define SRNOS_SIRIN_LINK_ALIGN32
#endif

typedef enum {
    SRNOS_SIRIN_LINK_QOS_CONTROL = 0u,
    SRNOS_SIRIN_LINK_QOS_RELIABLE = 1u,
    SRNOS_SIRIN_LINK_QOS_TELEMETRY = 2u,
    SRNOS_SIRIN_LINK_QOS_LOG = 3u,
    SRNOS_SIRIN_LINK_QOS_COUNT = 4u,
} SRNOS_SirinLinkQos;

/* Policy is local scheduling metadata; it does not change the V2 wire format. */
typedef enum { SRNOS_SIRIN_LINK_LATEST = 0, SRNOS_SIRIN_LINK_FIFO = 1 } SRNOS_SirinLinkPolicy;
typedef enum { SRNOS_SIRIN_LINK_DELIVERED, SRNOS_SIRIN_LINK_FAILED, SRNOS_SIRIN_LINK_CANCELLED } SRNOS_SirinLinkDelivery;
/* Task context, short/nonblocking. Called once for accepted FIFO packets. */
typedef void (*SRNOS_SirinLinkDeliveryFn)(void *user, uint32_t tag, SRNOS_SirinLinkDelivery result);

typedef enum {
    SRNOS_SIRIN_LINK_SERVICE_SYSTEM = 0x00u,
    SRNOS_SIRIN_LINK_SERVICE_EXEC = 0x01u,
    SRNOS_SIRIN_LINK_SERVICE_CONFIG = 0x02u,
    SRNOS_SIRIN_LINK_SERVICE_CONTROL = 0x03u,
    SRNOS_SIRIN_LINK_SERVICE_TELEMETRY = 0x04u,
    SRNOS_SIRIN_LINK_SERVICE_TEST = 0x7Eu,
} SRNOS_SirinLinkService;

typedef enum {
    SRNOS_SIRIN_LINK_SYSTEM_HELLO = 0x01u,
    SRNOS_SIRIN_LINK_SYSTEM_QOS_GET = 0x02u,
    SRNOS_SIRIN_LINK_SYSTEM_QOS_RESET = 0x03u,
    SRNOS_SIRIN_LINK_SYSTEM_ECHO = 0x04u,
} SRNOS_SirinLinkSystemMessage;

typedef enum {
    SRNOS_SIRIN_LINK_CONTROL_STATE = 0x01u,
} SRNOS_SirinLinkControlMessage;

typedef enum {
    SRNOS_SIRIN_LINK_STATUS_OK = 0x0000u,
    SRNOS_SIRIN_LINK_STATUS_ACCEPTED = 0x0001u,
    SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH = 0x0101u,
    SRNOS_SIRIN_LINK_STATUS_BAD_KIND = 0x0102u,
    SRNOS_SIRIN_LINK_STATUS_BAD_FLAGS = 0x0103u,
    SRNOS_SIRIN_LINK_STATUS_UNSUPPORTED_SERVICE = 0x0104u,
    SRNOS_SIRIN_LINK_STATUS_UNSUPPORTED_MESSAGE = 0x0105u,
    SRNOS_SIRIN_LINK_STATUS_BUSY = 0x0106u,
    SRNOS_SIRIN_LINK_STATUS_CONFLICT = 0x0107u,
    SRNOS_SIRIN_LINK_STATUS_INVALID_ARGUMENT = 0x0108u,
    SRNOS_SIRIN_LINK_STATUS_NOT_FOUND = 0x0109u,
    SRNOS_SIRIN_LINK_STATUS_TYPE_MISMATCH = 0x010Au,
    SRNOS_SIRIN_LINK_STATUS_OUT_OF_RANGE = 0x010Bu,
    SRNOS_SIRIN_LINK_STATUS_STORAGE_ERROR = 0x010Cu,
    SRNOS_SIRIN_LINK_STATUS_CALIBRATION_ERROR = 0x010Du,
    SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR = 0x01FFu,
    SRNOS_SIRIN_LINK_STATUS_DEFERRED_INTERNAL = 0xFFFFu,
} SRNOS_SirinLinkStatus;

typedef enum {
    SRNOS_SIRIN_LINK_OK = 0,
    SRNOS_SIRIN_LINK_ERR_NULL = -1,
    SRNOS_SIRIN_LINK_ERR_PARAM = -2,
    SRNOS_SIRIN_LINK_ERR_FULL = -3,
    SRNOS_SIRIN_LINK_ERR_NOT_FOUND = -4,
    SRNOS_SIRIN_LINK_ERR_TRANSPORT = -5,
    SRNOS_SIRIN_LINK_ERR_BUSY = -6,
    SRNOS_SIRIN_LINK_ERR_BUFFER = -7,
} SRNOS_SirinLinkResult;

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
} SRNOS_SirinLinkQosStats;

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
    SRNOS_SirinLinkQosStats qos[SRNOS_SIRIN_LINK_QOS_COUNT];
} SRNOS_SirinLinkStats;

typedef uint16_t (*SRNOS_SirinLinkHandlerFn)(void *user,
                                        const SRNOS_ProtocolView *request,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len,
                                        uint8_t *response_flags);

#define SRNOS_SIRIN_LINK_KIND_MASK(kind) ((uint8_t)(1u << (uint8_t)(kind)))

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_SIRIN_LINK_TYPES_H */
