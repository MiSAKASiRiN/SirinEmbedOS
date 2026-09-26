#ifndef OMX_CAN_ROUTER_COLLECTIONS_HPP
#define OMX_CAN_ROUTER_COLLECTIONS_HPP

#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CAN_ROUTER_ROUTE_CAPACITY
#define CAN_ROUTER_ROUTE_CAPACITY 64u
#endif

#ifndef CAN_ROUTER_ONESHOT_Q_LEN
#define CAN_ROUTER_ONESHOT_Q_LEN 64u
#endif

namespace can_router_detail {

static_assert(CAN_ROUTER_ROUTE_CAPACITY > 0u && CAN_ROUTER_ROUTE_CAPACITY <= 255u,
              "route count storage requires capacity in 1..255");
static_assert(CAN_ROUTER_ONESHOT_Q_LEN > 0u && CAN_ROUTER_ONESHOT_Q_LEN <= 255u,
              "one-shot heap indexes require capacity in 1..255");

enum class route_kind_t : uint8_t {
    raw,
    adapter,
};

union route_callback_t {
    can_router_rx_handler_t raw;
    void (*adapter)(void *obj, const can_frame_t *frame, uint32_t now_ms);
};

struct route_entry_t {
    can_rx_filter_t filter;
    route_kind_t kind;
    void *target;
    route_callback_t callback;
};

struct route_target_t {
    route_kind_t kind;
    void *target;
    route_callback_t callback;
};

struct route_table_t {
    route_entry_t entries[CAN_ROUTER_ROUTE_CAPACITY];
    uint8_t count;
};

inline bool same_key(const can_rx_filter_t &lhs, const can_rx_filter_t &rhs)
{
    return lhs.id == rhs.id && lhs.flags == rhs.flags;
}

inline bool key_less(const can_rx_filter_t &lhs, const can_rx_filter_t &rhs)
{
    const uint8_t lhs_ext = (uint8_t)(lhs.flags & CAN_FRAME_FLAG_EXT);
    const uint8_t rhs_ext = (uint8_t)(rhs.flags & CAN_FRAME_FLAG_EXT);
    return (lhs_ext < rhs_ext) || ((lhs_ext == rhs_ext) && (lhs.id < rhs.id));
}

inline size_t route_lower_bound(const route_table_t &table,
                                const can_rx_filter_t &key,
                                uint32_t *steps = nullptr)
{
    size_t first = 0u;
    size_t length = table.count;
    while (length > 0u) {
        const size_t half = length / 2u;
        const size_t middle = first + half;
        if (steps) {
            (*steps)++;
        }
        if (key_less(table.entries[middle].filter, key)) {
            first = middle + 1u;
            length -= half + 1u;
        } else {
            length = half;
        }
    }
    return first;
}

inline size_t route_upper_bound(const route_table_t &table,
                                const can_rx_filter_t &key,
                                uint32_t *steps = nullptr)
{
    size_t first = 0u;
    size_t length = table.count;
    while (length > 0u) {
        const size_t half = length / 2u;
        const size_t middle = first + half;
        if (steps) {
            (*steps)++;
        }
        if (!key_less(key, table.entries[middle].filter)) {
            first = middle + 1u;
            length -= half + 1u;
        } else {
            length = half;
        }
    }
    return first;
}

inline bool route_insert(route_table_t &table, const route_entry_t &entry)
{
    if (table.count >= CAN_ROUTER_ROUTE_CAPACITY) {
        return false;
    }
    const size_t pos = route_upper_bound(table, entry.filter);
    if (pos < table.count) {
        std::memmove(&table.entries[pos + 1u],
                     &table.entries[pos],
                     (table.count - pos) * sizeof(table.entries[0]));
    }
    table.entries[pos] = entry;
    table.count++;
    return true;
}

inline bool route_raw_exists(const route_table_t &table,
                             const can_rx_filter_t &filter,
                             can_router_rx_handler_t cb,
                             void *user)
{
    for (size_t i = route_lower_bound(table, filter);
         i < table.count && same_key(table.entries[i].filter, filter);
         ++i) {
        const route_entry_t &entry = table.entries[i];
        if (entry.kind == route_kind_t::raw && entry.callback.raw == cb && entry.target == user) {
            return true;
        }
    }
    return false;
}

inline bool route_erase_raw(route_table_t &table,
                            const can_rx_filter_t &filter,
                            can_router_rx_handler_t cb,
                            void *user)
{
    for (size_t i = route_lower_bound(table, filter);
         i < table.count && same_key(table.entries[i].filter, filter);
         ++i) {
        const route_entry_t &entry = table.entries[i];
        if (entry.kind != route_kind_t::raw || entry.callback.raw != cb || entry.target != user) {
            continue;
        }
        if (i + 1u < table.count) {
            std::memmove(&table.entries[i],
                         &table.entries[i + 1u],
                         (table.count - i - 1u) * sizeof(table.entries[0]));
        }
        table.count--;
        return true;
    }
    return false;
}

inline size_t route_erase_adapter(route_table_t &table, void *obj)
{
    size_t write = 0u;
    size_t removed = 0u;
    for (size_t read = 0u; read < table.count; ++read) {
        const route_entry_t &entry = table.entries[read];
        if (entry.kind == route_kind_t::adapter && entry.target == obj) {
            removed++;
            continue;
        }
        if (write != read) {
            table.entries[write] = entry;
        }
        write++;
    }
    table.count = (uint8_t)write;
    return removed;
}

struct oneshot_slot_t {
    can_frame_t frame;
    uint32_t deadline_ms;
    uint32_t seq;
    uint8_t priority;
    uint8_t priority_pos;
    uint8_t expiry_pos;
    bool used;
};

struct oneshot_queue_t {
    oneshot_slot_t slots[CAN_ROUTER_ONESHOT_Q_LEN];
    uint8_t priority_heap[CAN_ROUTER_ONESHOT_Q_LEN];
    uint8_t expiry_heap[CAN_ROUTER_ONESHOT_Q_LEN];
    uint8_t free_stack[CAN_ROUTER_ONESHOT_Q_LEN];
    uint8_t priority_size;
    uint8_t expiry_size;
    uint8_t free_count;
    uint8_t depth;
    bool initialized;
};

inline bool serial_before(uint32_t lhs, uint32_t rhs)
{
    return (int32_t)(lhs - rhs) < 0;
}

inline bool time_reached(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

inline void oneshot_init(oneshot_queue_t &queue)
{
    if (queue.initialized) {
        return;
    }
    std::memset(&queue, 0, sizeof(queue));
    for (uint8_t i = 0u; i < CAN_ROUTER_ONESHOT_Q_LEN; ++i) {
        queue.free_stack[i] = (uint8_t)(CAN_ROUTER_ONESHOT_Q_LEN - 1u - i);
    }
    queue.free_count = CAN_ROUTER_ONESHOT_Q_LEN;
    queue.initialized = true;
}

inline bool priority_less(const oneshot_queue_t &queue, uint8_t lhs_index, uint8_t rhs_index)
{
    const oneshot_slot_t &lhs = queue.slots[lhs_index];
    const oneshot_slot_t &rhs = queue.slots[rhs_index];
    return (lhs.priority < rhs.priority) ||
           ((lhs.priority == rhs.priority) && serial_before(lhs.seq, rhs.seq));
}

inline bool expiry_less(const oneshot_queue_t &queue, uint8_t lhs_index, uint8_t rhs_index)
{
    const oneshot_slot_t &lhs = queue.slots[lhs_index];
    const oneshot_slot_t &rhs = queue.slots[rhs_index];
    return serial_before(lhs.deadline_ms, rhs.deadline_ms) ||
           ((lhs.deadline_ms == rhs.deadline_ms) && serial_before(lhs.seq, rhs.seq));
}

inline void priority_swap(oneshot_queue_t &queue, uint8_t lhs, uint8_t rhs)
{
    const uint8_t tmp = queue.priority_heap[lhs];
    queue.priority_heap[lhs] = queue.priority_heap[rhs];
    queue.priority_heap[rhs] = tmp;
    queue.slots[queue.priority_heap[lhs]].priority_pos = lhs;
    queue.slots[queue.priority_heap[rhs]].priority_pos = rhs;
}

inline void expiry_swap(oneshot_queue_t &queue, uint8_t lhs, uint8_t rhs)
{
    const uint8_t tmp = queue.expiry_heap[lhs];
    queue.expiry_heap[lhs] = queue.expiry_heap[rhs];
    queue.expiry_heap[rhs] = tmp;
    queue.slots[queue.expiry_heap[lhs]].expiry_pos = lhs;
    queue.slots[queue.expiry_heap[rhs]].expiry_pos = rhs;
}

inline void priority_sift_up(oneshot_queue_t &queue, uint8_t pos)
{
    while (pos > 0u) {
        const uint8_t parent = (uint8_t)((pos - 1u) / 2u);
        if (!priority_less(queue, queue.priority_heap[pos], queue.priority_heap[parent])) {
            break;
        }
        priority_swap(queue, pos, parent);
        pos = parent;
    }
}

inline void expiry_sift_up(oneshot_queue_t &queue, uint8_t pos)
{
    while (pos > 0u) {
        const uint8_t parent = (uint8_t)((pos - 1u) / 2u);
        if (!expiry_less(queue, queue.expiry_heap[pos], queue.expiry_heap[parent])) {
            break;
        }
        expiry_swap(queue, pos, parent);
        pos = parent;
    }
}

inline void priority_sift_down(oneshot_queue_t &queue, uint8_t pos)
{
    while (true) {
        const uint8_t left = (uint8_t)(2u * pos + 1u);
        if (left >= queue.priority_size) {
            return;
        }
        const uint8_t right = (uint8_t)(left + 1u);
        uint8_t best = left;
        if (right < queue.priority_size &&
            priority_less(queue, queue.priority_heap[right], queue.priority_heap[left])) {
            best = right;
        }
        if (!priority_less(queue, queue.priority_heap[best], queue.priority_heap[pos])) {
            return;
        }
        priority_swap(queue, pos, best);
        pos = best;
    }
}

inline void expiry_sift_down(oneshot_queue_t &queue, uint8_t pos)
{
    while (true) {
        const uint8_t left = (uint8_t)(2u * pos + 1u);
        if (left >= queue.expiry_size) {
            return;
        }
        const uint8_t right = (uint8_t)(left + 1u);
        uint8_t best = left;
        if (right < queue.expiry_size &&
            expiry_less(queue, queue.expiry_heap[right], queue.expiry_heap[left])) {
            best = right;
        }
        if (!expiry_less(queue, queue.expiry_heap[best], queue.expiry_heap[pos])) {
            return;
        }
        expiry_swap(queue, pos, best);
        pos = best;
    }
}

inline void priority_remove_at(oneshot_queue_t &queue, uint8_t pos)
{
    const uint8_t last = (uint8_t)(--queue.priority_size);
    if (pos == last) {
        return;
    }
    queue.priority_heap[pos] = queue.priority_heap[last];
    queue.slots[queue.priority_heap[pos]].priority_pos = pos;
    if (pos > 0u && priority_less(queue,
                                 queue.priority_heap[pos],
                                 queue.priority_heap[(pos - 1u) / 2u])) {
        priority_sift_up(queue, pos);
    } else {
        priority_sift_down(queue, pos);
    }
}

inline void expiry_remove_at(oneshot_queue_t &queue, uint8_t pos)
{
    const uint8_t last = (uint8_t)(--queue.expiry_size);
    if (pos == last) {
        return;
    }
    queue.expiry_heap[pos] = queue.expiry_heap[last];
    queue.slots[queue.expiry_heap[pos]].expiry_pos = pos;
    if (pos > 0u && expiry_less(queue,
                               queue.expiry_heap[pos],
                               queue.expiry_heap[(pos - 1u) / 2u])) {
        expiry_sift_up(queue, pos);
    } else {
        expiry_sift_down(queue, pos);
    }
}

inline void oneshot_release(oneshot_queue_t &queue, uint8_t slot_index)
{
    oneshot_slot_t &slot = queue.slots[slot_index];
    priority_remove_at(queue, slot.priority_pos);
    expiry_remove_at(queue, slot.expiry_pos);
    std::memset(&slot, 0, sizeof(slot));
    queue.free_stack[queue.free_count++] = slot_index;
    queue.depth--;
}

inline bool oneshot_enqueue(oneshot_queue_t &queue,
                            const can_frame_t &frame,
                            uint8_t priority,
                            uint32_t deadline_ms,
                            uint32_t seq)
{
    oneshot_init(queue);
    if (queue.free_count == 0u) {
        return false;
    }
    const uint8_t slot_index = queue.free_stack[--queue.free_count];
    oneshot_slot_t &slot = queue.slots[slot_index];
    slot.frame = frame;
    slot.deadline_ms = deadline_ms;
    slot.seq = seq;
    slot.priority = priority;
    slot.used = true;

    slot.priority_pos = queue.priority_size;
    queue.priority_heap[queue.priority_size++] = slot_index;
    priority_sift_up(queue, slot.priority_pos);

    slot.expiry_pos = queue.expiry_size;
    queue.expiry_heap[queue.expiry_size++] = slot_index;
    expiry_sift_up(queue, slot.expiry_pos);
    queue.depth++;
    return true;
}

inline size_t oneshot_expire(oneshot_queue_t &queue, uint32_t now_ms)
{
    oneshot_init(queue);
    size_t expired = 0u;
    while (queue.expiry_size > 0u) {
        const uint8_t slot_index = queue.expiry_heap[0];
        if (!time_reached(now_ms, queue.slots[slot_index].deadline_ms)) {
            break;
        }
        oneshot_release(queue, slot_index);
        expired++;
    }
    return expired;
}

inline bool oneshot_peek(const oneshot_queue_t &queue,
                         uint8_t *slot_index,
                         oneshot_slot_t *slot)
{
    if (!queue.initialized || queue.priority_size == 0u || !slot_index || !slot) {
        return false;
    }
    *slot_index = queue.priority_heap[0];
    *slot = queue.slots[*slot_index];
    return true;
}

inline bool oneshot_remove_if_seq(oneshot_queue_t &queue,
                                  uint8_t slot_index,
                                  uint32_t seq)
{
    if (slot_index >= CAN_ROUTER_ONESHOT_Q_LEN) {
        return false;
    }
    const oneshot_slot_t &slot = queue.slots[slot_index];
    if (!slot.used || slot.seq != seq) {
        return false;
    }
    oneshot_release(queue, slot_index);
    return true;
}

} // namespace can_router_detail

#endif /* OMX_CAN_ROUTER_COLLECTIONS_HPP */
