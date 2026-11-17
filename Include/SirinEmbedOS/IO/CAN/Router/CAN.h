#ifndef SRNOS_CAN_ROUTER_H
#define SRNOS_CAN_ROUTER_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT
#define SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT 3u
#endif

#ifndef SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_COUNT
#define SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_COUNT 1u
#endif

#ifndef SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE
#define SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE (SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT + 1u)
#endif

#ifndef SRNOS_CAN_ROUTER_CHANNEL_COUNT
#define SRNOS_CAN_ROUTER_CHANNEL_COUNT (SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT + SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_COUNT)
#endif

#ifndef SRNOS_CAN_ROUTER_MAX_DATA_BYTES
#define SRNOS_CAN_ROUTER_MAX_DATA_BYTES 8u
#endif

typedef uint8_t SRNOS_CANChannel;

#define SRNOS_CAN_CH_NTFDCAN1 ((SRNOS_CANChannel)1u)
#define SRNOS_CAN_CH_NTFDCAN2 ((SRNOS_CANChannel)2u)
#define SRNOS_CAN_CH_NTFDCAN3 ((SRNOS_CANChannel)3u)
#define SRNOS_CAN_CH_EXFDCAN0 ((SRNOS_CANChannel)SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE)
#define SRNOS_CAN_CH_EXFDCAN(dev) ((SRNOS_CANChannel)(SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE + (uint8_t)(dev)))

enum {
    SRNOS_CAN_FRAME_FLAG_EXT = 1u << 0,
    SRNOS_CAN_FRAME_FLAG_RTR = 1u << 1,
};

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t flags;
    uint8_t _pad[2];
    uint8_t data[SRNOS_CAN_ROUTER_MAX_DATA_BYTES];
} SRNOS_CANFrame;

#if defined(__cplusplus)
static_assert(offsetof(SRNOS_CANFrame, data) == 8u, "SRNOS_CANFrame.data must be word-aligned");
static_assert(sizeof(SRNOS_CANFrame) == 16u, "SRNOS_CANFrame must remain 16 bytes");
#else
_Static_assert(offsetof(SRNOS_CANFrame, data) == 8u, "SRNOS_CANFrame.data must be word-aligned");
_Static_assert(sizeof(SRNOS_CANFrame) == 16u, "SRNOS_CANFrame must remain 16 bytes");
#endif

typedef struct {
    uint32_t id;
    uint8_t flags;
} SRNOS_CANRxFilter;

typedef enum {
    SRNOS_CAN_TX_KIND_PERIODIC = 0,
    SRNOS_CAN_TX_KIND_URGENT = 1,
} SRNOS_CANTxKind;

typedef enum {
    SRNOS_CAN_TX_FORMAT_RAW_FRAME = 0,
    SRNOS_CAN_TX_FORMAT_GROUP_I16 = 1,
} SRNOS_CANTxFormat;

typedef enum {
    SRNOS_CAN_TX_ENDIAN_BE = 0,
    SRNOS_CAN_TX_ENDIAN_LE = 1,
} SRNOS_CANTxEndian;

typedef enum {
    SRNOS_CAN_TX_RESULT_ACCEPTED = 0,
    SRNOS_CAN_TX_RESULT_DROPPED = 1,
    SRNOS_CAN_TX_RESULT_EXPIRED = 2,
} SRNOS_CANTxResult;

typedef struct {
    uint32_t group_id;
    uint8_t flags;
    uint8_t slot;
    uint8_t endian;
    int16_t value;
} SRNOS_CANTxGroupI16;

typedef struct {
    SRNOS_CANTxFormat format;
    uint8_t priority;
    uint32_t ttl_ms;
    uintptr_t token;
    SRNOS_CANFrame frame;
    SRNOS_CANTxGroupI16 group_i16;
} SRNOS_CANTxItem;

/* All now_ms arguments use SRNOS_TimeMs(): the 32-bit RTOS millisecond
 * clock, zero before scheduler start, wrapping at 2^32 ms. on_rx timestamps
 * software dispatch, not the physical bus arrival. Device last_rx_ms derived
 * here can be compared with SRNOS_TimeMs(); never mix HAL or TimeUs epochs. */
typedef struct {
    size_t (*rx_filter_count)(void *obj);
    bool (*rx_filter_at)(void *obj, size_t index, SRNOS_CANRxFilter *out);
    void (*on_rx)(void *obj, const SRNOS_CANFrame *frame, uint32_t now_ms);
    size_t (*poll_tx)(void *obj, SRNOS_CANTxKind kind, SRNOS_CANTxItem *out, size_t max, uint32_t now_ms);
    void (*commit_tx)(void *obj, SRNOS_CANTxKind kind, const SRNOS_CANTxItem *item, SRNOS_CANTxResult result);
    void (*tick)(void *obj, uint32_t now_ms);
} SRNOS_CANAdapterOps;

/* Bounded, nonblocking callback: P0 RX worker on CAN1/2/3; the unified P1
 * SRNOS_CAN4 service on CAN4. Called without the routing lock. */
typedef void (*SRNOS_CANRouterRxHandler)(SRNOS_CANChannel ch,
                                        const SRNOS_CANFrame *frame,
                                        void *user);

typedef struct {
    uint32_t rx_pops;
    uint32_t rx_routed_raw;
    uint32_t rx_routed_adapter;
    uint32_t rx_no_subscriber;
    uint32_t rx_dropped;
    uint32_t rx_wake_by_isr;
    uint32_t rx_wake_by_timeout;
    uint32_t tx_oneshot_enqueue_ok;
    uint32_t tx_oneshot_enqueue_full;
    uint32_t tx_oneshot_expired;
    uint32_t tx_urgent_accepted;
    uint32_t tx_periodic_accepted;
    uint32_t tx_periodic_dropped;
    uint32_t tx_backend_busy;
    uint32_t tx_complete_events;
    uint32_t rx_route_count;
    uint32_t rx_route_high_water;
    uint32_t rx_route_lookup_steps;
    uint32_t rx_route_capacity_rejects;
    uint32_t tx_adapter_count;
    uint32_t tx_adapter_high_water;
    uint32_t tx_oneshot_depth;
    uint32_t tx_oneshot_high_water;
} SRNOS_CANRouterStats;

int SRNOS_CANRouterAttachAdapter(SRNOS_CANChannel ch,
                              void *obj,
                              const SRNOS_CANAdapterOps *ops,
                              uint32_t period_ms);
int SRNOS_CANRouterDetachAdapter(SRNOS_CANChannel ch, void *obj);

int SRNOS_CANRouterSubscribeRaw(SRNOS_CANChannel ch,
                             SRNOS_CANRxFilter filter,
                             SRNOS_CANRouterRxHandler cb,
                             void *user);
int SRNOS_CANRouterUnsubscribeRaw(SRNOS_CANChannel ch,
                               SRNOS_CANRxFilter filter,
                               SRNOS_CANRouterRxHandler cb,
                               void *user);

/* Copies a valid Classic data frame. OK (0) means software admission only.
 * TTL 0 selects 20 ms; positive TTL must be <= INT32_MAX. TTL bounds software
 * queue waiting, not a hardware submission already in progress. */
int SRNOS_CANRouterPostOneshot(SRNOS_CANChannel ch,
                            const SRNOS_CANFrame *frame,
                            uint8_t priority,
                            uint32_t ttl_ms);

/* Task-context copy owned by the caller; no live internal pointer escapes. */
bool SRNOS_CANRouterGetStats(SRNOS_CANChannel ch, SRNOS_CANRouterStats *out);


/* Registration/removal is allowed only in ApplicationConfigure. Descriptors
 * and adapter objects must remain alive for the entire firmware lifetime. */
#define SRNOS_CAN_FROZEN (-8)
#define SRNOS_CAN_MODE_MISMATCH (-9)
/* All APIs in this header carry Classic CAN frames only. Raw subscriptions
 * may use any supported Classic nominal rate; adapters require Classic 1M.
 * Final startup validation rejects registration/mode conflicts regardless of
 * declaration order. Frames/filters permit only EXT, with standard ID <=0x7ff
 * or extended ID <=0x1fffffff; frames have DLC 0..8 and no RTR. Invalid IDs or
 * flags are rejected (-1), never truncated. Adapter group frames follow the
 * same rules, with slot 0..3 and explicit BE/LE endian; invalid items are
 * committed as DROPPED. FD raw traffic uses IO/CAN/FD/CANFD.h. */

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_CAN_ROUTER_H */
