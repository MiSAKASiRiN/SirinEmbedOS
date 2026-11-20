#include "SirinEmbedOS/Kernel/Thread.h"
#include "can_fd_internal.h"
#include "IO/CAN/can_controller_internal.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "../Native/fdcan_hal.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {
constexpr unsigned route_capacity = 32u;
constexpr unsigned tx_capacity = 64u;
constexpr unsigned drain_limit = 32u;
constexpr uint32_t default_ttl_ms = 20u;

/* Short, nestable single-core guards also work before the scheduler starts
 * and in the CAN ISR. Preserve a stronger caller mask; never mask IRQ 0..4. */
class Guard {
public:
    Guard() : saved_(__get_BASEPRI()) {
        __set_BASEPRI_MAX(configMAX_SYSCALL_INTERRUPT_PRIORITY);
        __DSB();
        __ISB();
    }
    ~Guard() { __DMB(); __set_BASEPRI(saved_); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
private:
    uint32_t saved_;
};

struct Route {
    SRNOS_CANRxFilter filter;
    SRNOS_CANFDRxHandler callback;
    void *user;
};
struct TxSlot {
    SRNOS_CANFDFrame frame;
    uint32_t deadline_ms;
    uint8_t priority;
};
struct TxStorage {
    TxSlot slots[tx_capacity];
    uint8_t order[tx_capacity];
    uint8_t free_slots[tx_capacity];
    uint8_t depth, free_count;
};
/* Fixed 32-bit heap_5: 8-byte header, up to 7 bytes of alignment and
 * up to 16 bytes of unsplit tail. Share this bound with startup admission. */
constexpr size_t tx_allocation_budget = sizeof(TxStorage) + 32u;
struct Context {
    Route routes[route_capacity];
    uint8_t route_count;
    TxStorage *tx;
    SRNOS_CANFDStats stats;
    const SRNOS_CANFDBackendOps *backend;
};

/* Registration survives before heap initialization in ordinary D1 BSS.
 * Only frozen FD channels allocate TX storage, once during SDK startup.
 * Both are CPU-only storage; neither is a DMA staging buffer. */
static Context contexts[SRNOS_CAN_CONTROLLER_COUNT];
static bool frozen, resources_ready;

static Context *context_of(SRNOS_CANChannel channel) {
    return channel >= 1u && channel <= SRNOS_CAN_CONTROLLER_COUNT ? &contexts[channel - 1u] : nullptr;
}
static bool task_context() { return __get_IPSR() == 0u && osKernelGetState() == osKernelRunning; }
static bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
static const SRNOS_CANFDBackendOps *backend_of(const Context &context) {
    return __atomic_load_n(&context.backend, __ATOMIC_ACQUIRE);
}
static uint32_t clock_ms(const Context &context) {
    const auto *backend = backend_of(context);
    return backend && backend->now_ms ? backend->now_ms() : SRNOS_TimeMs();
}
static uint32_t refresh_now(const Context &context, uint32_t previous) {
    const uint32_t current = clock_ms(context);
    return reached(current, previous) ? current : previous;
}
static bool filter_valid(const SRNOS_CANRxFilter &filter) {
    return !(filter.flags & ~SRNOS_CAN_FRAME_FLAG_EXT) &&
           filter.id <= ((filter.flags & SRNOS_CAN_FRAME_FLAG_EXT) ? 0x1fffffffu : 0x7ffu);
}
static bool key_less(const SRNOS_CANRxFilter &a, const SRNOS_CANRxFilter &b) {
    return a.flags < b.flags || (a.flags == b.flags && a.id < b.id);
}
static bool same_key(const SRNOS_CANRxFilter &a, const SRNOS_CANRxFilter &b) {
    return a.flags == b.flags && a.id == b.id;
}
static unsigned lower_bound(const Context &context, const SRNOS_CANRxFilter &key) {
    unsigned first = 0, length = context.route_count;
    while (length) {
        const unsigned half = length / 2u, middle = first + half;
        if (key_less(context.routes[middle].filter, key)) {
            first = middle + 1u;
            length -= half + 1u;
        } else length = half;
    }
    return first;
}
static bool frame_valid(const SRNOS_CANFDFrame &frame, bool transmit) {
    const uint8_t allowed = SRNOS_CANFD_FRAME_FLAG_EXT | SRNOS_CANFD_FRAME_FLAG_BRS |
                            (transmit ? 0u : SRNOS_CANFD_FRAME_FLAG_ESI);
    return !(frame.flags & ~allowed) && SRNOS_CANFDLengthToDlc(frame.length) != 0xffu &&
           frame.id <= ((frame.flags & SRNOS_CANFD_FRAME_FLAG_EXT) ? 0x1fffffffu : 0x7ffu);
}
static SRNOS_CANFDFrame copy_frame(const SRNOS_CANFDFrame &frame) {
    SRNOS_CANFDFrame copy{};
    copy.id = frame.id;
    copy.length = frame.length;
    copy.flags = frame.flags;
    std::memcpy(copy.data, frame.data, frame.length);
    return copy;
}
static void signal_tx(Context &context, SRNOS_CANChannel channel) {
    const auto *backend = backend_of(context);
    if (backend && backend->signal_tx) backend->signal_tx(channel);
}

/* Only the TX worker removes slots. Producers may insert while the worker
 * calls SPI/HAL outside the guard, but cannot recycle that worker's slot.
 * Compact indexes, never payloads; surviving priority/FIFO order is stable. */
static void expire(Context &context, uint32_t now_ms) {
    unsigned kept = 0;
    for (unsigned i = 0; i < context.tx->depth; ++i) {
        const uint8_t slot = context.tx->order[i];
        if (reached(now_ms, context.tx->slots[slot].deadline_ms)) {
            context.tx->free_slots[context.tx->free_count++] = slot;
            ++context.stats.tx_expired;
        } else context.tx->order[kept++] = slot;
    }
    context.tx->depth = static_cast<uint8_t>(kept);
    context.stats.tx_depth = context.tx->depth;
}
static void remove(Context &context, uint8_t slot) {
    for (unsigned i = 0; i < context.tx->depth; ++i) {
        if (context.tx->order[i] != slot) continue;
        std::memmove(context.tx->order + i, context.tx->order + i + 1u, context.tx->depth - i - 1u);
        --context.tx->depth;
        context.tx->free_slots[context.tx->free_count++] = slot;
        context.stats.tx_depth = context.tx->depth;
        return;
    }
}
} // namespace

extern "C" bool SRNOS_CANFDCoreHasRegistrations(SRNOS_CANChannel channel) {
    const auto *context = context_of(channel);
    return context && context->route_count != 0u;
}
extern "C" void SRNOS_CANFDCoreFreeze(void) { Guard guard; frozen = true; }
extern "C" size_t SRNOS_CANFDCoreHeapBudget(const SRNOS_CANControllerConfig configs[SRNOS_CAN_CONTROLLER_COUNT]) {
    if (!configs) return 0;
    size_t bytes = 0;
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i)
        if (configs[i].mode == SRNOS_CAN_MODE_FD) bytes += tx_allocation_budget;
    return bytes;
}
extern "C" bool SRNOS_CANFDCorePrepareResources(void) {
    if (resources_ready) return true;
    if (!frozen || __get_IPSR() || osKernelGetState() != osKernelReady) return false;
    TxStorage *allocated[SRNOS_CAN_CONTROLLER_COUNT]{};
    for (unsigned channel = 1; channel <= SRNOS_CAN_CONTROLLER_COUNT; ++channel) {
        if (!SRNOS_CANControllerIsFD(channel)) continue;
        /* The malloc-failed hook does not return. Check each allocation while
         * startup is single-threaded and ISR allocation is forbidden, so an
         * insufficient block can release this attempt before startup asserts.
         * heap_5 reports free block sizes including their allocation headers. */
        HeapStats_t heap{};
        vPortGetHeapStats(&heap);
        if (heap.xSizeOfLargestFreeBlockInBytes < tx_allocation_budget) {
            for (auto *storage : allocated) if (storage) vPortFree(storage);
            return false;
        }
        void *memory = pvPortMalloc(sizeof(TxStorage));
        if (!memory) {
            for (auto *storage : allocated) if (storage) vPortFree(storage);
            return false;
        }
        auto *storage = new (memory) TxStorage{};
        for (unsigned i = 0; i < tx_capacity; ++i)
            storage->free_slots[i] = static_cast<uint8_t>(tx_capacity - 1u - i);
        storage->free_count = tx_capacity;
        allocated[channel - 1u] = storage;
    }
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i) contexts[i].tx = allocated[i];
    resources_ready = true;
    return true;
}
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel channel, const SRNOS_CANFDBackendOps *ops) {
    auto *context = context_of(channel);
    if (context) __atomic_store_n(&context->backend, ops, __ATOMIC_RELEASE);
}
extern "C" SRNOS_CANFDResult SRNOS_CANFDSubscribeRaw(SRNOS_CANChannel channel,
    SRNOS_CANRxFilter filter, SRNOS_CANFDRxHandler callback, void *user) {
    if (frozen) return SRNOS_CANFD_FROZEN;
    auto *context = context_of(channel);
    if (!context || !callback || !filter_valid(filter) || __get_IPSR()) return SRNOS_CANFD_INVALID;
    Guard guard;
    unsigned position = lower_bound(*context, filter);
    while (position < context->route_count && same_key(context->routes[position].filter, filter)) {
        if (context->routes[position].callback == callback && context->routes[position].user == user)
            return SRNOS_CANFD_OK;
        ++position;
    }
    if (context->route_count == route_capacity) return SRNOS_CANFD_FULL;
    std::memmove(context->routes + position + 1u, context->routes + position,
                 (context->route_count - position) * sizeof(Route));
    context->routes[position] = {filter, callback, user};
    context->stats.route_count = ++context->route_count;
    return SRNOS_CANFD_OK;
}
extern "C" SRNOS_CANFDResult SRNOS_CANFDUnsubscribeRaw(SRNOS_CANChannel channel,
    SRNOS_CANRxFilter filter, SRNOS_CANFDRxHandler callback, void *user) {
    if (frozen) return SRNOS_CANFD_FROZEN;
    auto *context = context_of(channel);
    if (!context || !callback || !filter_valid(filter) || __get_IPSR()) return SRNOS_CANFD_INVALID;
    Guard guard;
    for (unsigned i = lower_bound(*context, filter);
         i < context->route_count && same_key(context->routes[i].filter, filter); ++i) {
        if (context->routes[i].callback != callback || context->routes[i].user != user) continue;
        std::memmove(context->routes + i, context->routes + i + 1u,
                     (context->route_count - i - 1u) * sizeof(Route));
        context->stats.route_count = --context->route_count;
        return SRNOS_CANFD_OK;
    }
    return SRNOS_CANFD_NOT_FOUND;
}
extern "C" SRNOS_CANFDResult SRNOS_CANFDPostOneshot(SRNOS_CANChannel channel,
    const SRNOS_CANFDFrame *frame, uint8_t priority, uint32_t ttl_ms) {
    auto *context = context_of(channel);
    if (!context || !frame || !task_context() || !frozen || !resources_ready ||
        ttl_ms > INT32_MAX || !frame_valid(*frame, true)) return SRNOS_CANFD_INVALID;
    const auto *config = SRNOS_CANControllerConfigOf(channel);
    if (!config || config->mode != SRNOS_CAN_MODE_FD) return SRNOS_CANFD_MODE_MISMATCH;
    if (!context->tx || !!(frame->flags & SRNOS_CANFD_FRAME_FLAG_BRS) != config->brs) return SRNOS_CANFD_INVALID;
    const SRNOS_CANFDFrame copy = copy_frame(*frame);
    const uint32_t deadline = clock_ms(*context) + (ttl_ms ? ttl_ms : default_ttl_ms);
    {
        Guard guard;
        if (!context->tx->free_count) {
            ++context->stats.tx_queue_full;
            return SRNOS_CANFD_FULL;
        }
        const uint8_t slot = context->tx->free_slots[--context->tx->free_count];
        context->tx->slots[slot] = {copy, deadline, priority};
        unsigned position = context->tx->depth;
        while (position && context->tx->slots[context->tx->order[position - 1u]].priority > priority) {
            context->tx->order[position] = context->tx->order[position - 1u];
            --position;
        }
        context->tx->order[position] = slot;
        context->stats.tx_depth = ++context->tx->depth;
        if (context->tx->depth > context->stats.tx_high_water) context->stats.tx_high_water = context->tx->depth;
        ++context->stats.tx_enqueued;
    }
    signal_tx(*context, channel);
    return SRNOS_CANFD_OK;
}
extern "C" bool SRNOS_CANFDGetStats(SRNOS_CANChannel channel, SRNOS_CANFDStats *out) {
    auto *context = context_of(channel);
    if (!context || !out || !task_context()) return false;
    Guard guard;
    *out = context->stats;
    return true;
}
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel channel) {
    if (auto *context = context_of(channel)) { Guard guard; ++context->stats.rx_rejected; }
}
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel channel, bool by_isr) {
    if (auto *context = context_of(channel)) {
        Guard guard;
        ++(by_isr ? context->stats.rx_wake_by_isr : context->stats.rx_wake_by_timeout);
    }
}
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel channel) {
    if (auto *context = context_of(channel)) { Guard guard; ++context->stats.tx_complete_events; }
}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel channel, const SRNOS_CANFDFrame *frame, uint32_t now_ms) {
    (void)now_ms;
    auto *context = context_of(channel);
    if (!context) return;
    if (!frame || !frozen || !resources_ready || !SRNOS_CANControllerIsFD(channel) || !frame_valid(*frame, false)) {
        SRNOS_CANFDCoreNoteRxRejected(channel);
        return;
    }
    const SRNOS_CANFDFrame copy = copy_frame(*frame);
    const SRNOS_CANRxFilter key{copy.id, static_cast<uint8_t>(copy.flags & SRNOS_CANFD_FRAME_FLAG_EXT)};
    unsigned delivered = 0;
    /* Frozen entries have firmware lifetime and cannot be modified by callbacks. */
    for (unsigned i = lower_bound(*context, key);
         i < context->route_count && same_key(context->routes[i].filter, key); ++i) {
        const Route &route = context->routes[i];
        route.callback(channel, &copy, route.user);
        ++delivered;
    }
    Guard guard;
    ++context->stats.rx_received;
    context->stats.rx_delivered += delivered;
    if (!delivered) ++context->stats.rx_no_subscriber;
}
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel channel, uint32_t now_ms) {
    auto *context = context_of(channel);
    if (!context || !resources_ready || !context->tx || !SRNOS_CANControllerIsFD(channel)) return UINT32_MAX;
    uint32_t wait = UINT32_MAX;
    const SRNOS_CANFDBackendOps *backend;
    {
        Guard guard;
        if (!context->tx->depth) return UINT32_MAX;
        for (unsigned i = 0; i < context->tx->depth; ++i) {
            const uint32_t deadline = context->tx->slots[context->tx->order[i]].deadline_ms;
            const uint32_t remaining = reached(now_ms, deadline) ? 0u : deadline - now_ms;
            if (remaining < wait) wait = remaining;
        }
        backend = backend_of(*context);
    }
    if (backend && backend->retry_wait_ms) {
        const uint32_t retry = backend->retry_wait_ms(channel, now_ms);
        if (retry < wait) wait = retry;
    }
    return wait;
}
static unsigned drain_tx(SRNOS_CANChannel channel, uint32_t now_ms, unsigned limit) {
    unsigned accepted=0;
    auto *context = context_of(channel);
    if (!context || !frozen || !resources_ready || !context->tx || !SRNOS_CANControllerIsFD(channel)) return accepted;
    for (unsigned attempt = 0; attempt < limit; ++attempt) {
        SRNOS_CANFDFrame frame;
        uint8_t slot;
        uint32_t deadline;
        const SRNOS_CANFDBackendOps *backend;
        now_ms = refresh_now(*context, now_ms);
        {
            Guard guard;
            expire(*context, now_ms);
            if (!context->tx->depth) return accepted;
            slot = context->tx->order[0];
            frame = context->tx->slots[slot].frame;
            deadline = context->tx->slots[slot].deadline_ms;
            backend = backend_of(*context);
        }
        if (!backend || !backend->try_push ||
            (backend->get_tx_free && !backend->get_tx_free(channel))) {
            Guard guard;
            ++context->stats.tx_backend_busy;
            return accepted;
        }
        /* CAN4 admission may block on shared SPI. Do not start a hardware
         * submission for a frame that expired while checking FIFO space. */
        now_ms = refresh_now(*context, now_ms);
        if (reached(now_ms, deadline)) {
            Guard guard;
            expire(*context, now_ms);
            continue;
        }
        if (!backend->try_push(channel, &frame)) {
            Guard guard;
            ++context->stats.tx_backend_busy;
            return accepted;
        }
        {
            Guard guard;
            remove(*context, slot);
            ++context->stats.tx_accepted;
            ++accepted;
        }
    }
    bool pending;
    { Guard guard; pending = context->tx->depth != 0u; }
    if (pending && limit>1) signal_tx(*context, channel);
    return accepted;
}

extern "C" bool SRNOS_CANFDCoreStepTx(SRNOS_CANChannel channel,uint32_t now_ms) {
    return drain_tx(channel,now_ms,1)!=0;
}
extern "C" void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel channel,uint32_t now_ms) {
    (void)drain_tx(channel,now_ms,drain_limit);
}
