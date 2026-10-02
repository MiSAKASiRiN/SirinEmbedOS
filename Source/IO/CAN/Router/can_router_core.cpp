#include "can_router_core_internal.h"
#include "can_router_collections.hpp"
#include "SirinEmbedOS/Core/Memory/MemorySections.h"

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include "task.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef SRNOS_CAN_ROUTER_MAX_ADAPTERS
#define SRNOS_CAN_ROUTER_MAX_ADAPTERS 64u
#endif

#ifndef SRNOS_CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER
#define SRNOS_CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER 16u
#endif

#ifndef SRNOS_CAN_ROUTER_GROUP_SHADOWS
#define SRNOS_CAN_ROUTER_GROUP_SHADOWS 24u
#endif

#ifndef SRNOS_CAN_ROUTER_POLL_TX_MAX
#define SRNOS_CAN_ROUTER_POLL_TX_MAX 4u
#endif

#ifndef SRNOS_CAN_ROUTER_URGENT_ATTEMPT_MAX
#define SRNOS_CAN_ROUTER_URGENT_ATTEMPT_MAX 16u
#endif

#ifndef SRNOS_CAN_ROUTER_PERIODIC_ATTEMPT_MAX
#define SRNOS_CAN_ROUTER_PERIODIC_ATTEMPT_MAX 16u
#endif

#ifndef SRNOS_CAN_ROUTER_PERIODIC_TX_FREE_RESERVE
#define SRNOS_CAN_ROUTER_PERIODIC_TX_FREE_RESERVE 2u
#endif

#ifndef SRNOS_CAN_ROUTER_PERIOD_MS_MIN
#define SRNOS_CAN_ROUTER_PERIOD_MS_MIN 1u
#endif

#ifndef SRNOS_CAN_ROUTER_PERIOD_MS_MAX
#define SRNOS_CAN_ROUTER_PERIOD_MS_MAX 1000u
#endif

#ifndef SRNOS_CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS
#define SRNOS_CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS 20u
#endif

typedef struct {
    void *obj;
    SRNOS_CANAdapterOps ops;
    uint32_t period_ms;
    uint32_t next_due_ms;
} adapter_entry_t;

typedef struct {
    void *obj;
    void (*commit_tx)(void *obj,
                      SRNOS_CANTxKind kind,
                      const SRNOS_CANTxItem *item,
                      SRNOS_CANTxResult result);
} adapter_commit_ref_t;

typedef struct {
    bool valid;
    uint32_t group_id;
    uint8_t flags;
    uint8_t endian;
    bool endian_set;
    bool slot_valid[4];
    int16_t slot_val[4];
} group_shadow_t;

typedef struct {
    bool used;
    adapter_commit_ref_t adapter;
    SRNOS_CANTxItem item;
} group_slot_item_t;

typedef struct {
    bool valid;
    uint32_t group_id;
    uint8_t flags;
    uint8_t endian;
    bool endian_set;
    bool slot_used[4];
    int16_t slot_val[4];
    group_slot_item_t slot_item[4];
} group_acc_t;

typedef struct {
    group_acc_t groups[SRNOS_CAN_ROUTER_GROUP_SHADOWS];
    SRNOS_CANTxItem items[SRNOS_CAN_ROUTER_POLL_TX_MAX];
} tx_scratch_t;

typedef struct {
    adapter_entry_t adapters[SRNOS_CAN_ROUTER_MAX_ADAPTERS];
    uint8_t adapter_count;
    SRNOS_CANRouterDetail::route_table_t routes;
    SRNOS_CANRouterDetail::oneshot_queue_t oneshot;
    group_shadow_t shadows[SRNOS_CAN_ROUTER_GROUP_SHADOWS];
    tx_scratch_t tx_scratch;
    volatile SRNOS_CANRouterStats stats;
    uint32_t oneshot_seq;
} SRNOS_CANRouterCtx;

/* Software queues/tables only; DMA staging remains in its reachable SRAM.
 * The SDK clears this section before ApplicationConfigure and router setup. */
SRNOS_ITCM_BSS static SRNOS_CANRouterCtx s_ctx[SRNOS_CAN_ROUTER_CHANNEL_COUNT + 1u];
static const SRNOS_CANRouterBackendOps *s_backend[SRNOS_CAN_ROUTER_CHANNEL_COUNT + 1u];

static bool s_configuration_frozen;
#if defined(__arm__) || defined(__thumb__)
static uint32_t s_boot_primask;
static unsigned s_boot_nesting;
#endif
static inline void enter_critical(void)
{
#if defined(__arm__) || defined(__thumb__)
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        const uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (s_boot_nesting++ == 0u) s_boot_primask = mask;
        return;
    }
#endif
    taskENTER_CRITICAL();
}

static inline void exit_critical(void)
{
#if defined(__arm__) || defined(__thumb__)
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        if (--s_boot_nesting == 0u) __set_PRIMASK(s_boot_primask);
        return;
    }
#endif
    taskEXIT_CRITICAL();
}
extern "C" void SRNOS_CAN_FreezeConfiguration(void)
{
    enter_critical();
    s_configuration_frozen = true;
    exit_critical();
}
extern "C" bool SRNOS_CAN_ConfigurationFrozen(void)
{
    return s_configuration_frozen;
}

static SRNOS_CANRouterCtx *ctx_of(SRNOS_CANChannel ch)
{
    if ((ch == 0u) || (ch > SRNOS_CAN_ROUTER_CHANNEL_COUNT)) {
        return nullptr;
    }
    return &s_ctx[ch];
}

static const SRNOS_CANRouterBackendOps *backend_of(SRNOS_CANChannel ch)
{
    if ((ch == 0u) || (ch > SRNOS_CAN_ROUTER_CHANNEL_COUNT)) {
        return nullptr;
    }
    return s_backend[ch];
}

static uint32_t router_now_ms(SRNOS_CANChannel ch)
{
    const SRNOS_CANRouterBackendOps *backend = backend_of(ch);
    if (backend && backend->now_ms) {
        return backend->now_ms();
    }
    return HAL_GetTick();
}

static bool time_reached(uint32_t now, uint32_t due)
{
    return ((int32_t)(now - due) >= 0);
}

static uint32_t clamp_period_ms(uint32_t period_ms)
{
    if (period_ms < SRNOS_CAN_ROUTER_PERIOD_MS_MIN) {
        period_ms = SRNOS_CAN_ROUTER_PERIOD_MS_MIN;
    }
    if (period_ms > SRNOS_CAN_ROUTER_PERIOD_MS_MAX) {
        period_ms = SRNOS_CAN_ROUTER_PERIOD_MS_MAX;
    }
    return period_ms;
}

static uint8_t norm_flags(uint8_t flags)
{
    return (uint8_t)(flags & (SRNOS_CAN_FRAME_FLAG_EXT | SRNOS_CAN_FRAME_FLAG_RTR));
}

static uint32_t norm_id(uint32_t id, uint8_t flags)
{
    return (flags & SRNOS_CAN_FRAME_FLAG_EXT) ? (id & 0x1FFFFFFFu) : (id & 0x7FFu);
}

static bool normalize_frame(SRNOS_CANFrame *frame)
{
    if (!frame) {
        return false;
    }
    frame->flags = norm_flags(frame->flags);
    frame->id = norm_id(frame->id, frame->flags);
    if ((frame->flags & SRNOS_CAN_FRAME_FLAG_RTR) || (frame->dlc > SRNOS_CAN_ROUTER_MAX_DATA_BYTES)) {
        return false;
    }
    for (uint8_t i = frame->dlc; i < SRNOS_CAN_ROUTER_MAX_DATA_BYTES; ++i) {
        frame->data[i] = 0u;
    }
    return true;
}

static bool normalize_filter(SRNOS_CANRxFilter *filter)
{
    if (!filter) {
        return false;
    }
    filter->flags = (uint8_t)(filter->flags & SRNOS_CAN_FRAME_FLAG_EXT);
    filter->id = norm_id(filter->id, filter->flags);
    return true;
}

static void commit_item(const adapter_entry_t *adapter,
                        SRNOS_CANTxKind kind,
                        const SRNOS_CANTxItem *item,
                        SRNOS_CANTxResult result)
{
    if (adapter && adapter->obj && adapter->ops.commit_tx) {
        adapter->ops.commit_tx(adapter->obj, kind, item, result);
    }
}

static void commit_ref(const adapter_commit_ref_t *adapter,
                       SRNOS_CANTxKind kind,
                       const SRNOS_CANTxItem *item,
                       SRNOS_CANTxResult result)
{
    if (adapter && adapter->obj && adapter->commit_tx) {
        adapter->commit_tx(adapter->obj, kind, item, result);
    }
}

static void signal_tx(SRNOS_CANChannel ch)
{
    const SRNOS_CANRouterBackendOps *backend = backend_of(ch);
    if (backend && backend->signal_tx) {
        backend->signal_tx(ch);
    }
}

extern "C" void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel ch, const SRNOS_CANRouterBackendOps *ops)
{
    if ((ch == 0u) || (ch > SRNOS_CAN_ROUTER_CHANNEL_COUNT)) {
        return;
    }
    s_backend[ch] = ops;
}

extern "C" bool SRNOS_CANRouterGetStats(SRNOS_CANChannel ch, SRNOS_CANRouterStats *out)
{
    SRNOS_CANRouterCtx *ctx=ctx_of(ch);if(!ctx||!out)return false;
#if defined(__arm__) || defined(__thumb__)
    if(__get_IPSR()!=0 || osKernelGetState()!=osKernelRunning)return false;
#endif
    enter_critical();std::memcpy(out,(const void *)&ctx->stats,sizeof(*out));exit_critical();
    return true;
}

extern "C" int SRNOS_CANRouterSubscribeRaw(SRNOS_CANChannel ch,
                                         SRNOS_CANRxFilter filter,
                                         SRNOS_CANRouterRxHandler cb,
                                         void *user)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !cb || !normalize_filter(&filter)) {
        return -1;
    }

    enter_critical();
    if (SRNOS_CANRouterDetail::route_raw_exists(ctx->routes, filter, cb, user)) {
        exit_critical();
        return 0;
    }
    SRNOS_CANRouterDetail::route_entry_t entry = {};
    entry.filter = filter;
    entry.kind = SRNOS_CANRouterDetail::route_kind_t::raw;
    entry.callback.raw = cb;
    entry.target = user;
    if (!SRNOS_CANRouterDetail::route_insert(ctx->routes, entry)) {
        ctx->stats.rx_route_capacity_rejects++;
        exit_critical();
        return -2;
    }
    ctx->stats.rx_route_count = ctx->routes.count;
    if (ctx->stats.rx_route_high_water < ctx->routes.count) {
        ctx->stats.rx_route_high_water = ctx->routes.count;
    }
    exit_critical();
    return 0;
}

extern "C" int SRNOS_CANRouterUnsubscribeRaw(SRNOS_CANChannel ch,
                                           SRNOS_CANRxFilter filter,
                                           SRNOS_CANRouterRxHandler cb,
                                           void *user)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !cb || !normalize_filter(&filter)) {
        return -1;
    }

    enter_critical();
    if (!SRNOS_CANRouterDetail::route_erase_raw(ctx->routes, filter, cb, user)) {
        exit_critical();
        return -2;
    }
    ctx->stats.rx_route_count = ctx->routes.count;
    exit_critical();
    return 0;
}

extern "C" int SRNOS_CANRouterAttachAdapter(SRNOS_CANChannel ch,
                                          void *obj,
                                          const SRNOS_CANAdapterOps *ops,
                                          uint32_t period_ms)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !obj || !ops || (!ops->on_rx && !ops->poll_tx && !ops->tick)) {
        return -1;
    }

    const size_t filter_count = ops->rx_filter_count ? ops->rx_filter_count(obj) : 0u;
    if ((filter_count > 0u) && (!ops->rx_filter_at || !ops->on_rx)) {
        return -1;
    }
    if (filter_count > SRNOS_CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER) {
        return -3;
    }

    SRNOS_CANRxFilter filters[SRNOS_CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER] = {};
    for (size_t i = 0; i < filter_count; ++i) {
        if (!ops->rx_filter_at(obj, i, &filters[i]) || !normalize_filter(&filters[i])) {
            return -1;
        }
    }

    period_ms = clamp_period_ms(period_ms);
    const uint32_t now_ms = router_now_ms(ch);

    enter_critical();
    for (uint32_t i = 0; i < ctx->adapter_count; ++i) {
        if (ctx->adapters[i].obj == obj) {
            exit_critical();
            return -2;
        }
    }
    if (ctx->adapter_count >= SRNOS_CAN_ROUTER_MAX_ADAPTERS) {
        exit_critical();
        return -3;
    }
    if (filter_count > (SRNOS_CAN_ROUTER_ROUTE_CAPACITY - ctx->routes.count)) {
        ctx->stats.rx_route_capacity_rejects++;
        exit_critical();
        return -4;
    }

    adapter_entry_t *entry = &ctx->adapters[ctx->adapter_count];
    entry->obj = obj;
    entry->ops = *ops;
    entry->period_ms = period_ms;
    entry->next_due_ms = now_ms;
    ctx->adapter_count++;
    for (size_t i = 0u; i < filter_count; ++i) {
        SRNOS_CANRouterDetail::route_entry_t route = {};
        route.filter = filters[i];
        route.kind = SRNOS_CANRouterDetail::route_kind_t::adapter;
        route.target = obj;
        route.callback.adapter = ops->on_rx;
        (void)SRNOS_CANRouterDetail::route_insert(ctx->routes, route);
    }
    ctx->stats.rx_route_count = ctx->routes.count;
    if (ctx->stats.rx_route_high_water < ctx->routes.count) {
        ctx->stats.rx_route_high_water = ctx->routes.count;
    }
    ctx->stats.tx_adapter_count = ctx->adapter_count;
    if (ctx->stats.tx_adapter_high_water < ctx->adapter_count) {
        ctx->stats.tx_adapter_high_water = ctx->adapter_count;
    }
    exit_critical();
    signal_tx(ch);
    return 0;
}

extern "C" int SRNOS_CANRouterDetachAdapter(SRNOS_CANChannel ch, void *obj)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !obj) {
        return -1;
    }

    enter_critical();
    int32_t adapter_index = -1;
    for (uint32_t i = 0; i < ctx->adapter_count; ++i) {
        if (ctx->adapters[i].obj == obj) {
            adapter_index = (int32_t)i;
            break;
        }
    }
    if (adapter_index < 0) {
        exit_critical();
        return -2;
    }
    (void)SRNOS_CANRouterDetail::route_erase_adapter(ctx->routes, obj);
    const uint8_t last = (uint8_t)(ctx->adapter_count - 1u);
    if ((uint8_t)adapter_index != last) {
        ctx->adapters[adapter_index] = ctx->adapters[last];
    }
    memset(&ctx->adapters[last], 0, sizeof(ctx->adapters[last]));
    ctx->adapter_count--;
    ctx->stats.rx_route_count = ctx->routes.count;
    ctx->stats.tx_adapter_count = ctx->adapter_count;
    exit_critical();
    return 0;
}

extern "C" int SRNOS_CANRouterPostOneshot(SRNOS_CANChannel ch,
                                        const SRNOS_CANFrame *frame,
                                        uint8_t priority,
                                        uint32_t ttl_ms)
{
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !frame) {
        return -1;
    }

    SRNOS_CANFrame copy = *frame;
    if (!normalize_frame(&copy)) {
        return -1;
    }
    if (ttl_ms == 0u) {
        ttl_ms = SRNOS_CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS;
    }

    const uint32_t now_ms = router_now_ms(ch);
    enter_critical();
    const uint32_t next_seq = ctx->oneshot_seq + 1u;
    const bool inserted = SRNOS_CANRouterDetail::oneshot_enqueue(ctx->oneshot,
                                                              copy,
                                                              priority,
                                                              now_ms + ttl_ms,
                                                              next_seq);
    if (!inserted) {
        ctx->stats.tx_oneshot_enqueue_full++;
        exit_critical();
        return -2;
    }
    ctx->oneshot_seq = next_seq;
    ctx->stats.tx_oneshot_enqueue_ok++;
    ctx->stats.tx_oneshot_depth = ctx->oneshot.depth;
    if (ctx->stats.tx_oneshot_high_water < ctx->oneshot.depth) {
        ctx->stats.tx_oneshot_high_water = ctx->oneshot.depth;
    }
    exit_critical();
    signal_tx(ch);
    return 0;
}

extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel ch, bool by_isr)
{
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx) {
        return;
    }
    if (by_isr) {
        ctx->stats.rx_wake_by_isr++;
    } else {
        ctx->stats.rx_wake_by_timeout++;
    }
}

extern "C" void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel ch)
{
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (ctx) {
        ctx->stats.tx_complete_events++;
    }
}

extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel ch,
                                             const SRNOS_CANFrame *frame,
                                             uint32_t now_ms)
{
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx || !frame) {
        return;
    }

    SRNOS_CANFrame copy = *frame;
    if (!normalize_frame(&copy)) {
        ctx->stats.rx_dropped++;
        return;
    }

    ctx->stats.rx_pops++;
    SRNOS_CANRxFilter key = {};
    key.id = copy.id;
    key.flags = (uint8_t)(copy.flags & SRNOS_CAN_FRAME_FLAG_EXT);
    uint32_t lookup_steps = 0u;
    uint8_t snapshot_count = 0u;
    SRNOS_CANRouterDetail::route_target_t snapshot[SRNOS_CAN_ROUTER_ROUTE_CAPACITY];

    enter_critical();
    size_t index = SRNOS_CANRouterDetail::route_lower_bound(ctx->routes, key, &lookup_steps);
    while (index < ctx->routes.count &&
           SRNOS_CANRouterDetail::same_key(ctx->routes.entries[index].filter, key)) {
        lookup_steps++;
        const SRNOS_CANRouterDetail::route_entry_t &route = ctx->routes.entries[index++];
        snapshot[snapshot_count].kind = route.kind;
        snapshot[snapshot_count].target = route.target;
        snapshot[snapshot_count].callback = route.callback;
        snapshot_count++;
    }
    ctx->stats.rx_route_lookup_steps += lookup_steps;
    exit_critical();

    for (uint8_t i = 0u; i < snapshot_count; ++i) {
        const SRNOS_CANRouterDetail::route_target_t &route = snapshot[i];
        if (route.kind == SRNOS_CANRouterDetail::route_kind_t::raw && route.callback.raw) {
            route.callback.raw(ch, &copy, route.target);
            ctx->stats.rx_routed_raw++;
        } else if (route.kind == SRNOS_CANRouterDetail::route_kind_t::adapter &&
                   route.target && route.callback.adapter) {
            route.callback.adapter(route.target, &copy, now_ms);
            ctx->stats.rx_routed_adapter++;
        }
    }

    if (snapshot_count == 0u) {
        ctx->stats.rx_no_subscriber++;
    }
}

static bool backend_try_accept(SRNOS_CANRouterCtx *ctx,
                               SRNOS_CANChannel ch,
                               const SRNOS_CANFrame *frame,
                               SRNOS_CANRouterTxClass tx_class)
{
    const SRNOS_CANRouterBackendOps *backend = backend_of(ch);
    if (!backend || !backend->try_push || !frame) {
        return false;
    }

    SRNOS_CANFrame copy = *frame;
    if (!normalize_frame(&copy)) {
        return false;
    }

    uint8_t free_level = 1u;
    if (backend->get_tx_free) {
        free_level = backend->get_tx_free(ch);
    }

    if (free_level == 0u) {
        ctx->stats.tx_backend_busy++;
        return false;
    }
    if ((tx_class == SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC) &&
        (free_level <= SRNOS_CAN_ROUTER_PERIODIC_TX_FREE_RESERVE)) {
        ctx->stats.tx_backend_busy++;
        return false;
    }

    const bool ok = backend->try_push(ch, &copy, tx_class);
    if (!ok) {
        ctx->stats.tx_backend_busy++;
    }
    return ok;
}

static void expire_oneshots_locked(SRNOS_CANRouterCtx *ctx, uint32_t now_ms)
{
    const size_t expired = SRNOS_CANRouterDetail::oneshot_expire(ctx->oneshot, now_ms);
    ctx->stats.tx_oneshot_expired += expired;
    ctx->stats.tx_oneshot_depth = ctx->oneshot.depth;
}

static bool drain_oneshots(SRNOS_CANRouterCtx *ctx, SRNOS_CANChannel ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    while (attempts < SRNOS_CAN_ROUTER_URGENT_ATTEMPT_MAX) {
        SRNOS_CANRouterDetail::oneshot_slot_t item = {};
        uint8_t index = 0u;
        enter_critical();
        expire_oneshots_locked(ctx, now_ms);
        const bool found = SRNOS_CANRouterDetail::oneshot_peek(ctx->oneshot, &index, &item);
        exit_critical();

        if (!found) {
            return true;
        }

        if (!backend_try_accept(ctx, ch, &item.frame, SRNOS_CAN_ROUTER_TX_CLASS_URGENT)) {
            return false;
        }

        enter_critical();
        if (SRNOS_CANRouterDetail::oneshot_remove_if_seq(ctx->oneshot, index, item.seq)) {
            ctx->stats.tx_urgent_accepted++;
            ctx->stats.tx_oneshot_depth = ctx->oneshot.depth;
        }
        exit_critical();
        attempts++;
    }
    return true;
}

static group_shadow_t *find_or_alloc_shadow(SRNOS_CANRouterCtx *ctx,
                                            uint32_t group_id,
                                            uint8_t flags,
                                            uint8_t endian)
{
    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_shadow_t *s = &ctx->shadows[i];
        if (s->valid && s->group_id == group_id && s->flags == flags) {
            if (!s->endian_set) {
                s->endian = endian;
                s->endian_set = true;
            }
            return s;
        }
    }
    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_shadow_t *s = &ctx->shadows[i];
        if (!s->valid) {
            memset(s, 0, sizeof(*s));
            s->valid = true;
            s->group_id = group_id;
            s->flags = flags;
            s->endian = endian;
            s->endian_set = true;
            return s;
        }
    }
    return nullptr;
}

static group_shadow_t *find_shadow(SRNOS_CANRouterCtx *ctx, uint32_t group_id, uint8_t flags)
{
    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_shadow_t *s = &ctx->shadows[i];
        if (s->valid && s->group_id == group_id && s->flags == flags) {
            return s;
        }
    }
    return nullptr;
}

static group_acc_t *find_or_alloc_group(group_acc_t *groups,
                                        uint32_t group_id,
                                        uint8_t flags,
                                        uint8_t endian)
{
    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_acc_t *g = &groups[i];
        if (g->valid && g->group_id == group_id && g->flags == flags) {
            if (!g->endian_set) {
                g->endian = endian;
                g->endian_set = true;
            }
            return g;
        }
    }
    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_acc_t *g = &groups[i];
        if (!g->valid) {
            memset(g, 0, sizeof(*g));
            g->valid = true;
            g->group_id = group_id;
            g->flags = flags;
            g->endian = endian;
            g->endian_set = true;
            return g;
        }
    }
    return nullptr;
}

static bool add_group_item(group_acc_t *groups,
                           const adapter_entry_t *adapter,
                           const SRNOS_CANTxItem *item,
                           SRNOS_CANTxKind kind)
{
    if (!item || item->group_i16.slot > 3u) {
        commit_item(adapter, kind, item, SRNOS_CAN_TX_RESULT_DROPPED);
        return false;
    }

    const uint8_t flags = (uint8_t)(item->group_i16.flags & SRNOS_CAN_FRAME_FLAG_EXT);
    const uint32_t group_id = norm_id(item->group_i16.group_id, flags);
    const uint8_t endian = (item->group_i16.endian == SRNOS_CAN_TX_ENDIAN_LE) ?
                           SRNOS_CAN_TX_ENDIAN_LE : SRNOS_CAN_TX_ENDIAN_BE;
    group_acc_t *g = find_or_alloc_group(groups, group_id, flags, endian);
    if (!g || (g->endian_set && g->endian != endian)) {
        commit_item(adapter, kind, item, SRNOS_CAN_TX_RESULT_DROPPED);
        return false;
    }

    const uint8_t slot = item->group_i16.slot;
    if (g->slot_item[slot].used) {
        commit_ref(&g->slot_item[slot].adapter,
                   kind,
                   &g->slot_item[slot].item,
                   SRNOS_CAN_TX_RESULT_DROPPED);
    }

    g->slot_used[slot] = true;
    g->slot_val[slot] = item->group_i16.value;
    g->slot_item[slot].used = true;
    g->slot_item[slot].adapter.obj = adapter->obj;
    g->slot_item[slot].adapter.commit_tx = adapter->ops.commit_tx;
    g->slot_item[slot].item = *item;
    return true;
}

static void compose_group_frame(SRNOS_CANRouterCtx *ctx,
                                const group_acc_t *g,
                                SRNOS_CANFrame *out)
{
    memset(out, 0, sizeof(*out));
    out->id = g->group_id;
    out->flags = g->flags;
    out->dlc = 8u;

    const group_shadow_t *shadow = find_shadow(ctx, g->group_id, g->flags);
    const bool le = (g->endian == SRNOS_CAN_TX_ENDIAN_LE);
    for (uint8_t slot = 0; slot < 4u; ++slot) {
        int16_t value = 0;
        if (g->slot_used[slot]) {
            value = g->slot_val[slot];
        } else if (shadow && shadow->slot_valid[slot]) {
            value = shadow->slot_val[slot];
        }

        const uint16_t u = (uint16_t)value;
        if (le) {
            out->data[2u * slot + 0u] = (uint8_t)(u & 0xFFu);
            out->data[2u * slot + 1u] = (uint8_t)((u >> 8) & 0xFFu);
        } else {
            out->data[2u * slot + 0u] = (uint8_t)((u >> 8) & 0xFFu);
            out->data[2u * slot + 1u] = (uint8_t)(u & 0xFFu);
        }
    }
}

static void commit_group(SRNOS_CANRouterCtx *ctx,
                         group_acc_t *g,
                         SRNOS_CANTxKind kind,
                         SRNOS_CANTxResult result)
{
    group_shadow_t *shadow = nullptr;
    if (result == SRNOS_CAN_TX_RESULT_ACCEPTED) {
        shadow = find_or_alloc_shadow(ctx, g->group_id, g->flags, g->endian);
    }

    for (uint8_t slot = 0; slot < 4u; ++slot) {
        if (!g->slot_item[slot].used) {
            continue;
        }
        commit_ref(&g->slot_item[slot].adapter, kind, &g->slot_item[slot].item, result);
        if (shadow && result == SRNOS_CAN_TX_RESULT_ACCEPTED) {
            shadow->slot_valid[slot] = true;
            shadow->slot_val[slot] = g->slot_val[slot];
        }
    }
}

static bool transmit_raw_item(SRNOS_CANRouterCtx *ctx,
                              SRNOS_CANChannel ch,
                              const adapter_entry_t *adapter,
                              SRNOS_CANTxKind kind,
                              const SRNOS_CANTxItem *item)
{
    if (!item) {
        return true;
    }

    SRNOS_CANFrame frame = item->frame;
    if (!normalize_frame(&frame)) {
        commit_item(adapter, kind, item, SRNOS_CAN_TX_RESULT_DROPPED);
        if (kind == SRNOS_CAN_TX_KIND_PERIODIC) {
            ctx->stats.tx_periodic_dropped++;
        }
        return true;
    }

    const SRNOS_CANRouterTxClass tx_class = (kind == SRNOS_CAN_TX_KIND_URGENT) ?
                                           SRNOS_CAN_ROUTER_TX_CLASS_URGENT :
                                           SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC;
    if (!backend_try_accept(ctx, ch, &frame, tx_class)) {
        if (kind == SRNOS_CAN_TX_KIND_URGENT) {
            return false;
        }
        commit_item(adapter, kind, item, SRNOS_CAN_TX_RESULT_DROPPED);
        ctx->stats.tx_periodic_dropped++;
        return true;
    }

    commit_item(adapter, kind, item, SRNOS_CAN_TX_RESULT_ACCEPTED);
    if (kind == SRNOS_CAN_TX_KIND_URGENT) {
        ctx->stats.tx_urgent_accepted++;
    } else {
        ctx->stats.tx_periodic_accepted++;
    }
    return true;
}

static bool drain_adapter_urgent(SRNOS_CANRouterCtx *ctx, SRNOS_CANChannel ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    SRNOS_CANTxItem *items = ctx->tx_scratch.items;

    for (uint32_t i = 0;; ++i) {
        adapter_entry_t adapter = {};
        enter_critical();
        if (i < ctx->adapter_count && ctx->adapters[i].ops.poll_tx) {
            adapter = ctx->adapters[i];
        }
        const bool done = i >= ctx->adapter_count;
        exit_critical();
        if (done) {
            break;
        }
        if (!adapter.obj || !adapter.ops.poll_tx) {
            continue;
        }

        memset(items, 0, sizeof(ctx->tx_scratch.items));
        const size_t n = adapter.ops.poll_tx(adapter.obj,
                                             SRNOS_CAN_TX_KIND_URGENT,
                                             items,
                                             SRNOS_CAN_ROUTER_POLL_TX_MAX,
                                             now_ms);
        for (size_t k = 0; k < n && k < SRNOS_CAN_ROUTER_POLL_TX_MAX; ++k) {
            attempts++;
            if (items[k].format == SRNOS_CAN_TX_FORMAT_RAW_FRAME) {
                if (!transmit_raw_item(ctx, ch, &adapter, SRNOS_CAN_TX_KIND_URGENT, &items[k])) {
                    return false;
                }
            } else if (items[k].format == SRNOS_CAN_TX_FORMAT_GROUP_I16) {
                group_acc_t *groups = ctx->tx_scratch.groups;
                memset(groups, 0, sizeof(ctx->tx_scratch.groups));
                if (!add_group_item(groups, &adapter, &items[k], SRNOS_CAN_TX_KIND_URGENT)) {
                    continue;
                }
                SRNOS_CANFrame frame = {};
                compose_group_frame(ctx, &groups[0], &frame);
                if (!backend_try_accept(ctx, ch, &frame, SRNOS_CAN_ROUTER_TX_CLASS_URGENT)) {
                    return false;
                }
                commit_group(ctx, &groups[0], SRNOS_CAN_TX_KIND_URGENT, SRNOS_CAN_TX_RESULT_ACCEPTED);
                ctx->stats.tx_urgent_accepted++;
            } else {
                commit_item(&adapter, SRNOS_CAN_TX_KIND_URGENT, &items[k], SRNOS_CAN_TX_RESULT_DROPPED);
            }

            if (attempts >= SRNOS_CAN_ROUTER_URGENT_ATTEMPT_MAX) {
                return true;
            }
        }
    }
    return true;
}

static void run_adapter_ticks(SRNOS_CANRouterCtx *ctx, uint32_t now_ms)
{
    for (uint32_t i = 0;; ++i) {
        adapter_entry_t adapter = {};
        enter_critical();
        if (i < ctx->adapter_count && ctx->adapters[i].ops.tick) {
            adapter = ctx->adapters[i];
        }
        const bool done = i >= ctx->adapter_count;
        exit_critical();
        if (done) {
            break;
        }
        if (adapter.obj && adapter.ops.tick) {
            adapter.ops.tick(adapter.obj, now_ms);
        }
    }
}

static void drain_periodic(SRNOS_CANRouterCtx *ctx, SRNOS_CANChannel ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    group_acc_t *groups = ctx->tx_scratch.groups;
    SRNOS_CANTxItem *items = ctx->tx_scratch.items;

    memset(groups, 0, sizeof(ctx->tx_scratch.groups));

    for (uint32_t i = 0;; ++i) {
        adapter_entry_t adapter = {};
        enter_critical();
        if (i < ctx->adapter_count && ctx->adapters[i].ops.poll_tx &&
            time_reached(now_ms, ctx->adapters[i].next_due_ms)) {
            ctx->adapters[i].next_due_ms = now_ms + ctx->adapters[i].period_ms;
            adapter = ctx->adapters[i];
        }
        const bool done = i >= ctx->adapter_count;
        exit_critical();
        if (done) {
            break;
        }
        if (!adapter.obj || !adapter.ops.poll_tx) {
            continue;
        }

        memset(items, 0, sizeof(ctx->tx_scratch.items));
        const size_t n = adapter.ops.poll_tx(adapter.obj,
                                             SRNOS_CAN_TX_KIND_PERIODIC,
                                             items,
                                             SRNOS_CAN_ROUTER_POLL_TX_MAX,
                                             now_ms);
        for (size_t k = 0; k < n && k < SRNOS_CAN_ROUTER_POLL_TX_MAX; ++k) {
            if (items[k].format == SRNOS_CAN_TX_FORMAT_RAW_FRAME) {
                (void)transmit_raw_item(ctx, ch, &adapter, SRNOS_CAN_TX_KIND_PERIODIC, &items[k]);
                attempts++;
            } else if (items[k].format == SRNOS_CAN_TX_FORMAT_GROUP_I16) {
                (void)add_group_item(groups, &adapter, &items[k], SRNOS_CAN_TX_KIND_PERIODIC);
            } else {
                commit_item(&adapter, SRNOS_CAN_TX_KIND_PERIODIC, &items[k], SRNOS_CAN_TX_RESULT_DROPPED);
                ctx->stats.tx_periodic_dropped++;
            }
            if (attempts >= SRNOS_CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
                break;
            }
        }
        if (attempts >= SRNOS_CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
            break;
        }
    }

    for (uint32_t i = 0; i < SRNOS_CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_acc_t *g = &groups[i];
        if (!g->valid) {
            continue;
        }
        SRNOS_CANFrame frame = {};
        compose_group_frame(ctx, g, &frame);
        if (backend_try_accept(ctx, ch, &frame, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC)) {
            commit_group(ctx, g, SRNOS_CAN_TX_KIND_PERIODIC, SRNOS_CAN_TX_RESULT_ACCEPTED);
            ctx->stats.tx_periodic_accepted++;
        } else {
            commit_group(ctx, g, SRNOS_CAN_TX_KIND_PERIODIC, SRNOS_CAN_TX_RESULT_DROPPED);
            ctx->stats.tx_periodic_dropped++;
        }
        attempts++;
        if (attempts >= SRNOS_CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
            break;
        }
    }
}

extern "C" void SRNOS_CANRouterCoreDrainTx(SRNOS_CANChannel ch, uint32_t now_ms)
{
    SRNOS_CANRouterCtx *ctx = ctx_of(ch);
    if (!ctx) {
        return;
    }

    run_adapter_ticks(ctx, now_ms);

    if (!drain_oneshots(ctx, ch, now_ms)) {
        return;
    }
    if (!drain_adapter_urgent(ctx, ch, now_ms)) {
        return;
    }
    drain_periodic(ctx, ch, now_ms);
}

extern "C" uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel ch, uint32_t now_ms)
{
    auto *ctx=ctx_of(ch);if(!ctx)return UINT32_MAX;
    uint32_t wait=UINT32_MAX;
    enter_critical();
    /* No public urgent-ready notification exists for adapters, so changing
     * this cadence would silently delay parent application commands. */
    for(unsigned i=0;i<ctx->adapter_count;i++) {
        const auto &a=ctx->adapters[i];
        if(a.ops.tick || a.ops.poll_tx){wait=1;break;}
    }
    if(ctx->oneshot.expiry_size) {
        uint32_t deadline=ctx->oneshot.slots[ctx->oneshot.expiry_heap[0]].deadline_ms;
        int32_t delta=(int32_t)(deadline-now_ms);
        uint32_t expiry=delta<=0?0:(uint32_t)delta;
        if(expiry<wait)wait=expiry;
        const auto *backend=backend_of(ch);
        if(backend && backend->retry_wait_ms) {
            uint32_t retry=backend->retry_wait_ms(ch,now_ms);
            if(retry<wait)wait=retry;
        }
        /* Full hardware FIFO wakes via TX completion; never spin at full. */
    }
    exit_critical();return wait;
}
