#include "can_router_core_internal.h"
#include "can_router_collections.hpp"

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include "task.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CAN_ROUTER_MAX_ADAPTERS
#define CAN_ROUTER_MAX_ADAPTERS 64u
#endif

#ifndef CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER
#define CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER 16u
#endif

#ifndef CAN_ROUTER_GROUP_SHADOWS
#define CAN_ROUTER_GROUP_SHADOWS 24u
#endif

#ifndef CAN_ROUTER_POLL_TX_MAX
#define CAN_ROUTER_POLL_TX_MAX 4u
#endif

#ifndef CAN_ROUTER_URGENT_ATTEMPT_MAX
#define CAN_ROUTER_URGENT_ATTEMPT_MAX 16u
#endif

#ifndef CAN_ROUTER_PERIODIC_ATTEMPT_MAX
#define CAN_ROUTER_PERIODIC_ATTEMPT_MAX 16u
#endif

#ifndef CAN_ROUTER_PERIODIC_TX_FREE_RESERVE
#define CAN_ROUTER_PERIODIC_TX_FREE_RESERVE 2u
#endif

#ifndef CAN_ROUTER_PERIOD_MS_MIN
#define CAN_ROUTER_PERIOD_MS_MIN 1u
#endif

#ifndef CAN_ROUTER_PERIOD_MS_MAX
#define CAN_ROUTER_PERIOD_MS_MAX 1000u
#endif

#ifndef CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS
#define CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS 20u
#endif

typedef struct {
    void *obj;
    can_adapter_ops_t ops;
    uint32_t period_ms;
    uint32_t next_due_ms;
} adapter_entry_t;

typedef struct {
    void *obj;
    void (*commit_tx)(void *obj,
                      can_tx_kind_t kind,
                      const can_tx_item_t *item,
                      can_tx_result_t result);
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
    can_tx_item_t item;
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
    group_acc_t groups[CAN_ROUTER_GROUP_SHADOWS];
    can_tx_item_t items[CAN_ROUTER_POLL_TX_MAX];
} tx_scratch_t;

typedef struct {
    adapter_entry_t adapters[CAN_ROUTER_MAX_ADAPTERS];
    uint8_t adapter_count;
    can_router_detail::route_table_t routes;
    can_router_detail::oneshot_queue_t oneshot;
    group_shadow_t shadows[CAN_ROUTER_GROUP_SHADOWS];
    tx_scratch_t tx_scratch;
    volatile can_router_stats_t stats;
    uint32_t oneshot_seq;
} can_router_ctx_t;

static can_router_ctx_t s_ctx[CAN_ROUTER_CHANNEL_COUNT + 1u];
static const can_router_backend_ops_t *s_backend[CAN_ROUTER_CHANNEL_COUNT + 1u];

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

static can_router_ctx_t *ctx_of(can_channel_t ch)
{
    if ((ch == 0u) || (ch > CAN_ROUTER_CHANNEL_COUNT)) {
        return nullptr;
    }
    return &s_ctx[ch];
}

static const can_router_backend_ops_t *backend_of(can_channel_t ch)
{
    if ((ch == 0u) || (ch > CAN_ROUTER_CHANNEL_COUNT)) {
        return nullptr;
    }
    return s_backend[ch];
}

static uint32_t router_now_ms(can_channel_t ch)
{
    const can_router_backend_ops_t *backend = backend_of(ch);
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
    if (period_ms < CAN_ROUTER_PERIOD_MS_MIN) {
        period_ms = CAN_ROUTER_PERIOD_MS_MIN;
    }
    if (period_ms > CAN_ROUTER_PERIOD_MS_MAX) {
        period_ms = CAN_ROUTER_PERIOD_MS_MAX;
    }
    return period_ms;
}

static uint8_t norm_flags(uint8_t flags)
{
    return (uint8_t)(flags & (CAN_FRAME_FLAG_EXT | CAN_FRAME_FLAG_RTR));
}

static uint32_t norm_id(uint32_t id, uint8_t flags)
{
    return (flags & CAN_FRAME_FLAG_EXT) ? (id & 0x1FFFFFFFu) : (id & 0x7FFu);
}

static bool normalize_frame(can_frame_t *frame)
{
    if (!frame) {
        return false;
    }
    frame->flags = norm_flags(frame->flags);
    frame->id = norm_id(frame->id, frame->flags);
    if ((frame->flags & CAN_FRAME_FLAG_RTR) || (frame->dlc > CAN_ROUTER_MAX_DATA_BYTES)) {
        return false;
    }
    for (uint8_t i = frame->dlc; i < CAN_ROUTER_MAX_DATA_BYTES; ++i) {
        frame->data[i] = 0u;
    }
    return true;
}

static bool normalize_filter(can_rx_filter_t *filter)
{
    if (!filter) {
        return false;
    }
    filter->flags = (uint8_t)(filter->flags & CAN_FRAME_FLAG_EXT);
    filter->id = norm_id(filter->id, filter->flags);
    return true;
}

static void commit_item(const adapter_entry_t *adapter,
                        can_tx_kind_t kind,
                        const can_tx_item_t *item,
                        can_tx_result_t result)
{
    if (adapter && adapter->obj && adapter->ops.commit_tx) {
        adapter->ops.commit_tx(adapter->obj, kind, item, result);
    }
}

static void commit_ref(const adapter_commit_ref_t *adapter,
                       can_tx_kind_t kind,
                       const can_tx_item_t *item,
                       can_tx_result_t result)
{
    if (adapter && adapter->obj && adapter->commit_tx) {
        adapter->commit_tx(adapter->obj, kind, item, result);
    }
}

static void signal_tx(can_channel_t ch)
{
    const can_router_backend_ops_t *backend = backend_of(ch);
    if (backend && backend->signal_tx) {
        backend->signal_tx(ch);
    }
}

extern "C" void can_router_core_set_backend(can_channel_t ch, const can_router_backend_ops_t *ops)
{
    if ((ch == 0u) || (ch > CAN_ROUTER_CHANNEL_COUNT)) {
        return;
    }
    s_backend[ch] = ops;
}

extern "C" const volatile can_router_stats_t *can_router_get_stats(can_channel_t ch)
{
    can_router_ctx_t *ctx = ctx_of(ch);
    return ctx ? &ctx->stats : nullptr;
}

extern "C" int can_router_subscribe_raw(can_channel_t ch,
                                         can_rx_filter_t filter,
                                         can_router_rx_handler_t cb,
                                         void *user)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx || !cb || !normalize_filter(&filter)) {
        return -1;
    }

    enter_critical();
    if (can_router_detail::route_raw_exists(ctx->routes, filter, cb, user)) {
        exit_critical();
        return 0;
    }
    can_router_detail::route_entry_t entry = {};
    entry.filter = filter;
    entry.kind = can_router_detail::route_kind_t::raw;
    entry.callback.raw = cb;
    entry.target = user;
    if (!can_router_detail::route_insert(ctx->routes, entry)) {
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

extern "C" int can_router_unsubscribe_raw(can_channel_t ch,
                                           can_rx_filter_t filter,
                                           can_router_rx_handler_t cb,
                                           void *user)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx || !cb || !normalize_filter(&filter)) {
        return -1;
    }

    enter_critical();
    if (!can_router_detail::route_erase_raw(ctx->routes, filter, cb, user)) {
        exit_critical();
        return -2;
    }
    ctx->stats.rx_route_count = ctx->routes.count;
    exit_critical();
    return 0;
}

extern "C" int can_router_attach_adapter(can_channel_t ch,
                                          void *obj,
                                          const can_adapter_ops_t *ops,
                                          uint32_t period_ms)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx || !obj || !ops || (!ops->on_rx && !ops->poll_tx && !ops->tick)) {
        return -1;
    }

    const size_t filter_count = ops->rx_filter_count ? ops->rx_filter_count(obj) : 0u;
    if ((filter_count > 0u) && (!ops->rx_filter_at || !ops->on_rx)) {
        return -1;
    }
    if (filter_count > CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER) {
        return -3;
    }

    can_rx_filter_t filters[CAN_ROUTER_MAX_RX_FILTERS_PER_ADAPTER] = {};
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
    if (ctx->adapter_count >= CAN_ROUTER_MAX_ADAPTERS) {
        exit_critical();
        return -3;
    }
    if (filter_count > (CAN_ROUTER_ROUTE_CAPACITY - ctx->routes.count)) {
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
        can_router_detail::route_entry_t route = {};
        route.filter = filters[i];
        route.kind = can_router_detail::route_kind_t::adapter;
        route.target = obj;
        route.callback.adapter = ops->on_rx;
        (void)can_router_detail::route_insert(ctx->routes, route);
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

extern "C" int can_router_detach_adapter(can_channel_t ch, void *obj)
{
    if (s_configuration_frozen) return SRNOS_CAN_FROZEN;
    can_router_ctx_t *ctx = ctx_of(ch);
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
    (void)can_router_detail::route_erase_adapter(ctx->routes, obj);
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

extern "C" int can_router_post_oneshot(can_channel_t ch,
                                        const can_frame_t *frame,
                                        uint8_t priority,
                                        uint32_t ttl_ms)
{
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx || !frame) {
        return -1;
    }

    can_frame_t copy = *frame;
    if (!normalize_frame(&copy)) {
        return -1;
    }
    if (ttl_ms == 0u) {
        ttl_ms = CAN_ROUTER_ONESHOT_DEFAULT_TTL_MS;
    }

    const uint32_t now_ms = router_now_ms(ch);
    enter_critical();
    const uint32_t next_seq = ctx->oneshot_seq + 1u;
    const bool inserted = can_router_detail::oneshot_enqueue(ctx->oneshot,
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

extern "C" void can_router_core_note_rx_wake(can_channel_t ch, bool by_isr)
{
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx) {
        return;
    }
    if (by_isr) {
        ctx->stats.rx_wake_by_isr++;
    } else {
        ctx->stats.rx_wake_by_timeout++;
    }
}

extern "C" void can_router_core_note_tx_complete(can_channel_t ch)
{
    can_router_ctx_t *ctx = ctx_of(ch);
    if (ctx) {
        ctx->stats.tx_complete_events++;
    }
}

extern "C" void can_router_core_on_rx_frame(can_channel_t ch,
                                             const can_frame_t *frame,
                                             uint32_t now_ms)
{
    can_router_ctx_t *ctx = ctx_of(ch);
    if (!ctx || !frame) {
        return;
    }

    can_frame_t copy = *frame;
    if (!normalize_frame(&copy)) {
        ctx->stats.rx_dropped++;
        return;
    }

    ctx->stats.rx_pops++;
    can_rx_filter_t key = {};
    key.id = copy.id;
    key.flags = (uint8_t)(copy.flags & CAN_FRAME_FLAG_EXT);
    uint32_t lookup_steps = 0u;
    uint8_t snapshot_count = 0u;
    can_router_detail::route_target_t snapshot[CAN_ROUTER_ROUTE_CAPACITY];

    enter_critical();
    size_t index = can_router_detail::route_lower_bound(ctx->routes, key, &lookup_steps);
    while (index < ctx->routes.count &&
           can_router_detail::same_key(ctx->routes.entries[index].filter, key)) {
        lookup_steps++;
        const can_router_detail::route_entry_t &route = ctx->routes.entries[index++];
        snapshot[snapshot_count].kind = route.kind;
        snapshot[snapshot_count].target = route.target;
        snapshot[snapshot_count].callback = route.callback;
        snapshot_count++;
    }
    ctx->stats.rx_route_lookup_steps += lookup_steps;
    exit_critical();

    for (uint8_t i = 0u; i < snapshot_count; ++i) {
        const can_router_detail::route_target_t &route = snapshot[i];
        if (route.kind == can_router_detail::route_kind_t::raw && route.callback.raw) {
            route.callback.raw(ch, &copy, route.target);
            ctx->stats.rx_routed_raw++;
        } else if (route.kind == can_router_detail::route_kind_t::adapter &&
                   route.target && route.callback.adapter) {
            route.callback.adapter(route.target, &copy, now_ms);
            ctx->stats.rx_routed_adapter++;
        }
    }

    if (snapshot_count == 0u) {
        ctx->stats.rx_no_subscriber++;
    }
}

static bool backend_try_accept(can_router_ctx_t *ctx,
                               can_channel_t ch,
                               const can_frame_t *frame,
                               can_router_tx_class_t tx_class)
{
    const can_router_backend_ops_t *backend = backend_of(ch);
    if (!backend || !backend->try_push || !frame) {
        return false;
    }

    can_frame_t copy = *frame;
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
    if ((tx_class == CAN_ROUTER_TX_CLASS_PERIODIC) &&
        (free_level <= CAN_ROUTER_PERIODIC_TX_FREE_RESERVE)) {
        ctx->stats.tx_backend_busy++;
        return false;
    }

    const bool ok = backend->try_push(ch, &copy, tx_class);
    if (!ok) {
        ctx->stats.tx_backend_busy++;
    }
    return ok;
}

static void expire_oneshots_locked(can_router_ctx_t *ctx, uint32_t now_ms)
{
    const size_t expired = can_router_detail::oneshot_expire(ctx->oneshot, now_ms);
    ctx->stats.tx_oneshot_expired += expired;
    ctx->stats.tx_oneshot_depth = ctx->oneshot.depth;
}

static bool drain_oneshots(can_router_ctx_t *ctx, can_channel_t ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    while (attempts < CAN_ROUTER_URGENT_ATTEMPT_MAX) {
        can_router_detail::oneshot_slot_t item = {};
        uint8_t index = 0u;
        enter_critical();
        expire_oneshots_locked(ctx, now_ms);
        const bool found = can_router_detail::oneshot_peek(ctx->oneshot, &index, &item);
        exit_critical();

        if (!found) {
            return true;
        }

        if (!backend_try_accept(ctx, ch, &item.frame, CAN_ROUTER_TX_CLASS_URGENT)) {
            return false;
        }

        enter_critical();
        if (can_router_detail::oneshot_remove_if_seq(ctx->oneshot, index, item.seq)) {
            ctx->stats.tx_urgent_accepted++;
            ctx->stats.tx_oneshot_depth = ctx->oneshot.depth;
        }
        exit_critical();
        attempts++;
    }
    return true;
}

static group_shadow_t *find_or_alloc_shadow(can_router_ctx_t *ctx,
                                            uint32_t group_id,
                                            uint8_t flags,
                                            uint8_t endian)
{
    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_shadow_t *s = &ctx->shadows[i];
        if (s->valid && s->group_id == group_id && s->flags == flags) {
            if (!s->endian_set) {
                s->endian = endian;
                s->endian_set = true;
            }
            return s;
        }
    }
    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
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

static group_shadow_t *find_shadow(can_router_ctx_t *ctx, uint32_t group_id, uint8_t flags)
{
    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
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
    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_acc_t *g = &groups[i];
        if (g->valid && g->group_id == group_id && g->flags == flags) {
            if (!g->endian_set) {
                g->endian = endian;
                g->endian_set = true;
            }
            return g;
        }
    }
    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
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
                           const can_tx_item_t *item,
                           can_tx_kind_t kind)
{
    if (!item || item->group_i16.slot > 3u) {
        commit_item(adapter, kind, item, CAN_TX_RESULT_DROPPED);
        return false;
    }

    const uint8_t flags = (uint8_t)(item->group_i16.flags & CAN_FRAME_FLAG_EXT);
    const uint32_t group_id = norm_id(item->group_i16.group_id, flags);
    const uint8_t endian = (item->group_i16.endian == CAN_TX_ENDIAN_LE) ?
                           CAN_TX_ENDIAN_LE : CAN_TX_ENDIAN_BE;
    group_acc_t *g = find_or_alloc_group(groups, group_id, flags, endian);
    if (!g || (g->endian_set && g->endian != endian)) {
        commit_item(adapter, kind, item, CAN_TX_RESULT_DROPPED);
        return false;
    }

    const uint8_t slot = item->group_i16.slot;
    if (g->slot_item[slot].used) {
        commit_ref(&g->slot_item[slot].adapter,
                   kind,
                   &g->slot_item[slot].item,
                   CAN_TX_RESULT_DROPPED);
    }

    g->slot_used[slot] = true;
    g->slot_val[slot] = item->group_i16.value;
    g->slot_item[slot].used = true;
    g->slot_item[slot].adapter.obj = adapter->obj;
    g->slot_item[slot].adapter.commit_tx = adapter->ops.commit_tx;
    g->slot_item[slot].item = *item;
    return true;
}

static void compose_group_frame(can_router_ctx_t *ctx,
                                const group_acc_t *g,
                                can_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->id = g->group_id;
    out->flags = g->flags;
    out->dlc = 8u;

    const group_shadow_t *shadow = find_shadow(ctx, g->group_id, g->flags);
    const bool le = (g->endian == CAN_TX_ENDIAN_LE);
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

static void commit_group(can_router_ctx_t *ctx,
                         group_acc_t *g,
                         can_tx_kind_t kind,
                         can_tx_result_t result)
{
    group_shadow_t *shadow = nullptr;
    if (result == CAN_TX_RESULT_ACCEPTED) {
        shadow = find_or_alloc_shadow(ctx, g->group_id, g->flags, g->endian);
    }

    for (uint8_t slot = 0; slot < 4u; ++slot) {
        if (!g->slot_item[slot].used) {
            continue;
        }
        commit_ref(&g->slot_item[slot].adapter, kind, &g->slot_item[slot].item, result);
        if (shadow && result == CAN_TX_RESULT_ACCEPTED) {
            shadow->slot_valid[slot] = true;
            shadow->slot_val[slot] = g->slot_val[slot];
        }
    }
}

static bool transmit_raw_item(can_router_ctx_t *ctx,
                              can_channel_t ch,
                              const adapter_entry_t *adapter,
                              can_tx_kind_t kind,
                              const can_tx_item_t *item)
{
    if (!item) {
        return true;
    }

    can_frame_t frame = item->frame;
    if (!normalize_frame(&frame)) {
        commit_item(adapter, kind, item, CAN_TX_RESULT_DROPPED);
        if (kind == CAN_TX_KIND_PERIODIC) {
            ctx->stats.tx_periodic_dropped++;
        }
        return true;
    }

    const can_router_tx_class_t tx_class = (kind == CAN_TX_KIND_URGENT) ?
                                           CAN_ROUTER_TX_CLASS_URGENT :
                                           CAN_ROUTER_TX_CLASS_PERIODIC;
    if (!backend_try_accept(ctx, ch, &frame, tx_class)) {
        if (kind == CAN_TX_KIND_URGENT) {
            return false;
        }
        commit_item(adapter, kind, item, CAN_TX_RESULT_DROPPED);
        ctx->stats.tx_periodic_dropped++;
        return true;
    }

    commit_item(adapter, kind, item, CAN_TX_RESULT_ACCEPTED);
    if (kind == CAN_TX_KIND_URGENT) {
        ctx->stats.tx_urgent_accepted++;
    } else {
        ctx->stats.tx_periodic_accepted++;
    }
    return true;
}

static bool drain_adapter_urgent(can_router_ctx_t *ctx, can_channel_t ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    can_tx_item_t *items = ctx->tx_scratch.items;

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
                                             CAN_TX_KIND_URGENT,
                                             items,
                                             CAN_ROUTER_POLL_TX_MAX,
                                             now_ms);
        for (size_t k = 0; k < n && k < CAN_ROUTER_POLL_TX_MAX; ++k) {
            attempts++;
            if (items[k].format == CAN_TX_FORMAT_RAW_FRAME) {
                if (!transmit_raw_item(ctx, ch, &adapter, CAN_TX_KIND_URGENT, &items[k])) {
                    return false;
                }
            } else if (items[k].format == CAN_TX_FORMAT_GROUP_I16) {
                group_acc_t *groups = ctx->tx_scratch.groups;
                memset(groups, 0, sizeof(ctx->tx_scratch.groups));
                if (!add_group_item(groups, &adapter, &items[k], CAN_TX_KIND_URGENT)) {
                    continue;
                }
                can_frame_t frame = {};
                compose_group_frame(ctx, &groups[0], &frame);
                if (!backend_try_accept(ctx, ch, &frame, CAN_ROUTER_TX_CLASS_URGENT)) {
                    return false;
                }
                commit_group(ctx, &groups[0], CAN_TX_KIND_URGENT, CAN_TX_RESULT_ACCEPTED);
                ctx->stats.tx_urgent_accepted++;
            } else {
                commit_item(&adapter, CAN_TX_KIND_URGENT, &items[k], CAN_TX_RESULT_DROPPED);
            }

            if (attempts >= CAN_ROUTER_URGENT_ATTEMPT_MAX) {
                return true;
            }
        }
    }
    return true;
}

static void run_adapter_ticks(can_router_ctx_t *ctx, uint32_t now_ms)
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

static void drain_periodic(can_router_ctx_t *ctx, can_channel_t ch, uint32_t now_ms)
{
    uint32_t attempts = 0u;
    group_acc_t *groups = ctx->tx_scratch.groups;
    can_tx_item_t *items = ctx->tx_scratch.items;

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
                                             CAN_TX_KIND_PERIODIC,
                                             items,
                                             CAN_ROUTER_POLL_TX_MAX,
                                             now_ms);
        for (size_t k = 0; k < n && k < CAN_ROUTER_POLL_TX_MAX; ++k) {
            if (items[k].format == CAN_TX_FORMAT_RAW_FRAME) {
                (void)transmit_raw_item(ctx, ch, &adapter, CAN_TX_KIND_PERIODIC, &items[k]);
                attempts++;
            } else if (items[k].format == CAN_TX_FORMAT_GROUP_I16) {
                (void)add_group_item(groups, &adapter, &items[k], CAN_TX_KIND_PERIODIC);
            } else {
                commit_item(&adapter, CAN_TX_KIND_PERIODIC, &items[k], CAN_TX_RESULT_DROPPED);
                ctx->stats.tx_periodic_dropped++;
            }
            if (attempts >= CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
                break;
            }
        }
        if (attempts >= CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
            break;
        }
    }

    for (uint32_t i = 0; i < CAN_ROUTER_GROUP_SHADOWS; ++i) {
        group_acc_t *g = &groups[i];
        if (!g->valid) {
            continue;
        }
        can_frame_t frame = {};
        compose_group_frame(ctx, g, &frame);
        if (backend_try_accept(ctx, ch, &frame, CAN_ROUTER_TX_CLASS_PERIODIC)) {
            commit_group(ctx, g, CAN_TX_KIND_PERIODIC, CAN_TX_RESULT_ACCEPTED);
            ctx->stats.tx_periodic_accepted++;
        } else {
            commit_group(ctx, g, CAN_TX_KIND_PERIODIC, CAN_TX_RESULT_DROPPED);
            ctx->stats.tx_periodic_dropped++;
        }
        attempts++;
        if (attempts >= CAN_ROUTER_PERIODIC_ATTEMPT_MAX) {
            break;
        }
    }
}

extern "C" void can_router_core_drain_tx(can_channel_t ch, uint32_t now_ms)
{
    can_router_ctx_t *ctx = ctx_of(ch);
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
