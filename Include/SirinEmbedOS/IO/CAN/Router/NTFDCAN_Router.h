#ifndef H723VG_V2_FREERTOS_NTFDCAN_ROUTER_H
#define H723VG_V2_FREERTOS_NTFDCAN_ROUTER_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CAN_ROUTER_NATIVE_CHANNEL_COUNT
#define CAN_ROUTER_NATIVE_CHANNEL_COUNT 3u
#endif

#ifndef CAN_ROUTER_EXFDCAN_CHANNEL_COUNT
#define CAN_ROUTER_EXFDCAN_CHANNEL_COUNT 1u
#endif

#ifndef CAN_ROUTER_EXFDCAN_CHANNEL_BASE
#define CAN_ROUTER_EXFDCAN_CHANNEL_BASE (CAN_ROUTER_NATIVE_CHANNEL_COUNT + 1u)
#endif

#ifndef CAN_ROUTER_CHANNEL_COUNT
#define CAN_ROUTER_CHANNEL_COUNT (CAN_ROUTER_NATIVE_CHANNEL_COUNT + CAN_ROUTER_EXFDCAN_CHANNEL_COUNT)
#endif

#ifndef CAN_ROUTER_MAX_DATA_BYTES
#define CAN_ROUTER_MAX_DATA_BYTES 8u
#endif

typedef uint8_t can_channel_t;

#define CAN_CH_NTFDCAN1 ((can_channel_t)1u)
#define CAN_CH_NTFDCAN2 ((can_channel_t)2u)
#define CAN_CH_NTFDCAN3 ((can_channel_t)3u)
#define CAN_CH_EXFDCAN0 ((can_channel_t)CAN_ROUTER_EXFDCAN_CHANNEL_BASE)
#define CAN_CH_EXFDCAN(dev) ((can_channel_t)(CAN_ROUTER_EXFDCAN_CHANNEL_BASE + (uint8_t)(dev)))

enum {
    CAN_FRAME_FLAG_EXT = 1u << 0,
    CAN_FRAME_FLAG_RTR = 1u << 1,
};

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t flags;
    uint8_t _pad[2];
    uint8_t data[CAN_ROUTER_MAX_DATA_BYTES];
} can_frame_t;

#if defined(__cplusplus)
static_assert(offsetof(can_frame_t, data) == 8u, "can_frame_t.data must be word-aligned");
static_assert(sizeof(can_frame_t) == 16u, "can_frame_t must remain 16 bytes");
#else
_Static_assert(offsetof(can_frame_t, data) == 8u, "can_frame_t.data must be word-aligned");
_Static_assert(sizeof(can_frame_t) == 16u, "can_frame_t must remain 16 bytes");
#endif

typedef struct {
    uint32_t id;
    uint8_t flags;
} can_rx_filter_t;

typedef enum {
    CAN_TX_KIND_PERIODIC = 0,
    CAN_TX_KIND_URGENT = 1,
} can_tx_kind_t;

typedef enum {
    CAN_TX_FORMAT_RAW_FRAME = 0,
    CAN_TX_FORMAT_GROUP_I16 = 1,
} can_tx_format_t;

typedef enum {
    CAN_TX_ENDIAN_BE = 0,
    CAN_TX_ENDIAN_LE = 1,
} can_tx_endian_t;

typedef enum {
    CAN_TX_RESULT_ACCEPTED = 0,
    CAN_TX_RESULT_DROPPED = 1,
    CAN_TX_RESULT_EXPIRED = 2,
} can_tx_result_t;

typedef struct {
    uint32_t group_id;
    uint8_t flags;
    uint8_t slot;
    uint8_t endian;
    int16_t value;
} can_tx_group_i16_t;

typedef struct {
    can_tx_format_t format;
    uint8_t priority;
    uint32_t ttl_ms;
    uintptr_t token;
    can_frame_t frame;
    can_tx_group_i16_t group_i16;
} can_tx_item_t;

typedef struct {
    size_t (*rx_filter_count)(void *obj);
    bool (*rx_filter_at)(void *obj, size_t index, can_rx_filter_t *out);
    void (*on_rx)(void *obj, const can_frame_t *frame, uint32_t now_ms);
    size_t (*poll_tx)(void *obj, can_tx_kind_t kind, can_tx_item_t *out, size_t max, uint32_t now_ms);
    void (*commit_tx)(void *obj, can_tx_kind_t kind, const can_tx_item_t *item, can_tx_result_t result);
    void (*tick)(void *obj, uint32_t now_ms);
} can_adapter_ops_t;

typedef void (*can_router_rx_handler_t)(can_channel_t ch,
                                        const can_frame_t *frame,
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
} can_router_stats_t;

int can_router_attach_adapter(can_channel_t ch,
                              void *obj,
                              const can_adapter_ops_t *ops,
                              uint32_t period_ms);
int can_router_detach_adapter(can_channel_t ch, void *obj);

int can_router_subscribe_raw(can_channel_t ch,
                             can_rx_filter_t filter,
                             can_router_rx_handler_t cb,
                             void *user);
int can_router_unsubscribe_raw(can_channel_t ch,
                               can_rx_filter_t filter,
                               can_router_rx_handler_t cb,
                               void *user);

int can_router_post_oneshot(can_channel_t ch,
                            const can_frame_t *frame,
                            uint8_t priority,
                            uint32_t ttl_ms);

const volatile can_router_stats_t *can_router_get_stats(can_channel_t ch);

void bsp_fdcan_on_rx_isr(uint8_t bus);
void bsp_fdcan_on_tx_event(uint8_t bus);

/* Registration/removal is allowed only in ApplicationConfigure. Descriptors
 * and adapter objects must remain alive for the entire firmware lifetime. */
#define SRNOS_CAN_FROZEN (-8)
void SRNOS_CAN_FreezeConfiguration(void);
bool SRNOS_CAN_ConfigurationFrozen(void);
void Start_CAN_RxRouter(void *argument);
void Start_CAN_TxRouter(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* H723VG_V2_FREERTOS_NTFDCAN_ROUTER_H */
