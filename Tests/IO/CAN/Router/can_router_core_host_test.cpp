#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "can_router_collections.hpp"
#include "can_router_core_internal.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace SRNOS_CANRouterDetail;

static uint32_t g_now_ms;
static bool g_backend_busy;
static bool g_detach_during_push;
static void *g_detach_obj;
static SRNOS_CANChannel g_detach_ch;

extern "C" uint32_t HAL_GetTick(void) { return g_now_ms + 12u; }
extern "C" uint32_t SRNOS_TimeMs(void) { return g_now_ms; }

static uint32_t test_now_ms()
{
    return g_now_ms;
}

static uint8_t test_tx_free(SRNOS_CANChannel)
{
    return g_backend_busy ? 0u : 8u;
}

static bool test_try_push(SRNOS_CANChannel, const SRNOS_CANFrame *, SRNOS_CANRouterTxClass)
{
    if (g_backend_busy) {
        return false;
    }
    if (g_detach_during_push) {
        g_detach_during_push = false;
        assert(SRNOS_CANRouterDetachAdapter(g_detach_ch, g_detach_obj) == 0);
    }
    return true;
}

static void test_signal(SRNOS_CANChannel)
{
}

static const SRNOS_CANRouterBackendOps k_backend = {
    nullptr,
    test_tx_free,
    test_try_push,
    test_signal,
    test_now_ms,
    nullptr,
};

static void raw_count(SRNOS_CANChannel, const SRNOS_CANFrame *, void *user)
{
    (*static_cast<uint32_t *>(user))++;
}

static SRNOS_CANRxFilter filter(uint32_t id, bool ext = false)
{
    SRNOS_CANRxFilter result = {};
    result.id = id;
    result.flags = ext ? SRNOS_CAN_FRAME_FLAG_EXT : 0u;
    return result;
}

static SRNOS_CANFrame frame(uint32_t id, bool ext = false)
{
    SRNOS_CANFrame result = {};
    result.id = id;
    result.flags = ext ? SRNOS_CAN_FRAME_FLAG_EXT : 0u;
    result.dlc = 8u;
    return result;
}

static SRNOS_CANRouterStats stats_copy(SRNOS_CANChannel ch)
{SRNOS_CANRouterStats out{};assert(SRNOS_CANRouterGetStats(ch,&out));return out;}
static void test_route_capacity_and_order()
{
    uint32_t hits[65] = {};
    for (uint32_t i = 0u; i < 64u; ++i) {
        assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN1,
                                        filter(63u - i),
                                        raw_count,
                                        &hits[i]) == 0);
        if (i == 0u) {
            assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_count == 1u);
        }
    }
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_count == 64u);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_high_water == 64u);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN1,
                                    filter(63u),
                                    raw_count,
                                    &hits[0]) == 0);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN1,
                                    filter(0x700u),
                                    raw_count,
                                    &hits[64]) == -2);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_capacity_rejects == 1u);
    SRNOS_CANFrame lowest = frame(0u);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN1, &lowest, 1u);
    assert(hits[63] == 1u);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_lookup_steps <= 8u);
}

static void test_raw_idempotence_fanout_and_ext()
{
    uint32_t raw_a = 0u;
    uint32_t raw_b = 0u;
    uint32_t ext = 0u;
    const SRNOS_CANRxFilter sid = filter(0x123u);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN2, sid, raw_count, &raw_a) == 0);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN2, sid, raw_count, &raw_a) == 0);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN2, sid, raw_count, &raw_b) == 0);
    assert(SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN2,
                                    filter(0x123u, true),
                                    raw_count,
                                    &ext) == 0);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN2).rx_route_count == 3u);

    SRNOS_CANFrame sid_frame = frame(0x123u);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN2, &sid_frame, 1u);
    assert(raw_a == 1u && raw_b == 1u && ext == 0u);
    SRNOS_CANFrame ext_frame = frame(0x123u, true);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN2, &ext_frame, 2u);
    assert(raw_a == 1u && raw_b == 1u && ext == 1u);
    assert(SRNOS_CANRouterUnsubscribeRaw(SRNOS_CAN_CH_NTFDCAN2, sid, raw_count, &raw_a) == 0);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN2, &sid_frame, 3u);
    assert(raw_a == 1u && raw_b == 2u);
}

struct adapter_probe_t {
    SRNOS_CANRxFilter filters[4];
    size_t filter_count;
    uint32_t rx_count;
    uint32_t commit_count;
    bool emit_group;
};

static size_t adapter_filter_count(void *obj)
{
    return static_cast<adapter_probe_t *>(obj)->filter_count;
}

static bool adapter_filter_at(void *obj, size_t index, SRNOS_CANRxFilter *out)
{
    adapter_probe_t *probe = static_cast<adapter_probe_t *>(obj);
    if (index >= probe->filter_count) {
        return false;
    }
    *out = probe->filters[index];
    return true;
}

static void adapter_on_rx(void *obj, const SRNOS_CANFrame *, uint32_t)
{
    static_cast<adapter_probe_t *>(obj)->rx_count++;
}

static size_t adapter_poll(void *obj,
                           SRNOS_CANTxKind kind,
                           SRNOS_CANTxItem *out,
                           size_t max,
                           uint32_t)
{
    adapter_probe_t *probe = static_cast<adapter_probe_t *>(obj);
    if (!probe->emit_group || kind != SRNOS_CAN_TX_KIND_PERIODIC || max == 0u) {
        return 0u;
    }
    out[0].format = SRNOS_CAN_TX_FORMAT_GROUP_I16;
    out[0].group_i16.group_id = 0x200u;
    out[0].group_i16.slot = 0u;
    out[0].group_i16.value = 123;
    return 1u;
}

static void adapter_commit(void *obj,
                           SRNOS_CANTxKind,
                           const SRNOS_CANTxItem *,
                           SRNOS_CANTxResult result)
{
    if (result == SRNOS_CAN_TX_RESULT_ACCEPTED) {
        static_cast<adapter_probe_t *>(obj)->commit_count++;
    }
}

static const SRNOS_CANAdapterOps k_adapter_ops = {
    adapter_filter_count,
    adapter_filter_at,
    adapter_on_rx,
    adapter_poll,
    adapter_commit,
    nullptr,
};

static void test_adapter_capacity()
{
    static adapter_probe_t adapters[65] = {};
    static adapter_probe_t route_reject = {};
    route_reject.filters[0] = filter(0x500u);
    route_reject.filter_count = 1u;
    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN1,
                                     &route_reject,
                                     &k_adapter_ops,
                                     1u) == -4);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).rx_route_capacity_rejects == 2u);
    for (uint32_t i = 0u; i < 64u; ++i) {
        assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN1,
                                         &adapters[i],
                                         &k_adapter_ops,
                                         1u) == 0);
        if (i == 0u) {
            assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).tx_adapter_count == 1u);
        }
    }
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).tx_adapter_count == 64u);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN1).tx_adapter_high_water == 64u);
    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN1,
                                     &adapters[64],
                                     &k_adapter_ops,
                                     1u) == -3);
}

static void test_adapter_filters_detach_and_swap_refs()
{
    static adapter_probe_t first = {};
    first.filters[0] = filter(0x301u);
    first.filters[1] = filter(0x302u);
    first.filter_count = 2u;
    static adapter_probe_t second = {};
    second.filters[0] = filter(0x302u);
    second.filter_count = 1u;
    second.emit_group = true;

    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN3, &first, &k_adapter_ops, 1u) == 0);
    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN3, &second, &k_adapter_ops, 1u) == 0);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN3).rx_route_count == 3u);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN3).tx_adapter_count == 2u);
    SRNOS_CANFrame rx = frame(0x302u);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN3, &rx, 1u);
    assert(first.rx_count == 1u && second.rx_count == 1u);

    assert(SRNOS_CANRouterDetachAdapter(SRNOS_CAN_CH_NTFDCAN3, &first) == 0);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN3).rx_route_count == 1u);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN3).tx_adapter_count == 1u);
    SRNOS_CANRouterCoreOnRxFrame(SRNOS_CAN_CH_NTFDCAN3, &rx, 2u);
    assert(first.rx_count == 1u && second.rx_count == 2u);

    static adapter_probe_t prefix = {};
    static adapter_probe_t producer = {};
    producer.emit_group = true;
    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_EXFDCAN0, &prefix, &k_adapter_ops, 1u) == 0);
    assert(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_EXFDCAN0, &producer, &k_adapter_ops, 1u) == 0);
    g_detach_ch = SRNOS_CAN_CH_EXFDCAN0;
    g_detach_obj = &prefix;
    g_detach_during_push = true;
    SRNOS_CANRouterCoreDrainTx(SRNOS_CAN_CH_EXFDCAN0, 10u);
    assert(producer.commit_count == 1u);
    assert(prefix.commit_count == 0u);
    assert(stats_copy(SRNOS_CAN_CH_EXFDCAN0).tx_adapter_count == 1u);
}

static void test_oneshot_dual_heap()
{
    oneshot_queue_t queue = {};
    const SRNOS_CANFrame f1 = frame(1u);
    const SRNOS_CANFrame f2 = frame(2u);
    const SRNOS_CANFrame f3 = frame(3u);
    assert(oneshot_enqueue(queue, f1, 4u, 100u, 1u));
    assert(oneshot_enqueue(queue, f2, 1u, 80u, 2u));
    assert(oneshot_enqueue(queue, f3, 1u, 90u, 3u));

    uint8_t index = 0u;
    oneshot_slot_t slot = {};
    assert(oneshot_peek(queue, &index, &slot));
    assert(slot.frame.id == 2u); // lower priority number, then FIFO sequence
    assert(oneshot_remove_if_seq(queue, index, slot.seq));
    assert(oneshot_peek(queue, &index, &slot) && slot.frame.id == 3u);
    assert(oneshot_expire(queue, 95u) == 1u);
    assert(oneshot_peek(queue, &index, &slot) && slot.frame.id == 1u);

    oneshot_queue_t full = {};
    for (uint32_t i = 0u; i < 64u; ++i) {
        assert(oneshot_enqueue(full, frame(i), (uint8_t)i, 1000u, i + 1u));
    }
    assert(!oneshot_enqueue(full, frame(65u), 0u, 1000u, 65u));

    oneshot_queue_t wrap = {};
    assert(oneshot_enqueue(wrap, f1, 0u, 0xFFFFFFF5u, 0xFFFFFFFEu));
    assert(oneshot_enqueue(wrap, f2, 0u, 0x00000005u, 0xFFFFFFFFu));
    assert(oneshot_expire(wrap, 0xFFFFFFF6u) == 1u);
    assert(oneshot_expire(wrap, 0x00000006u) == 1u);
}

static void test_oneshot_busy_keeps_item()
{
    SRNOS_CANFrame tx = frame(0x55u);
    g_now_ms = 10u;
    assert(SRNOS_CANRouterPostOneshot(SRNOS_CAN_CH_NTFDCAN2, &tx, 0u, 100u) == 0);
    const uint32_t depth = stats_copy(SRNOS_CAN_CH_NTFDCAN2).tx_oneshot_depth;
    g_backend_busy = true;
    SRNOS_CANRouterCoreDrainTx(SRNOS_CAN_CH_NTFDCAN2, g_now_ms);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN2).tx_oneshot_depth == depth);
    g_backend_busy = false;
    SRNOS_CANRouterCoreDrainTx(SRNOS_CAN_CH_NTFDCAN2, g_now_ms);
    assert(stats_copy(SRNOS_CAN_CH_NTFDCAN2).tx_oneshot_depth == depth - 1u);
}

static void test_next_wait()
{
    // Use an otherwise empty bus before the registration capacity suites.
    const SRNOS_CANChannel ch=1;
    assert(SRNOS_CANRouterCoreNextTxWait(ch,10)==UINT32_MAX);
    SRNOS_CANFrame tx=frame(0x71);g_now_ms=UINT32_MAX-2;
    assert(SRNOS_CANRouterPostOneshot(ch,&tx,0,10)==0);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,g_now_ms)==10);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,3)==4);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,7)==0);
    SRNOS_CANRouterCoreDrainTx(ch,7);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,7)==UINT32_MAX);
    g_now_ms=0;
    SRNOS_CANAdapterOps ops{};ops.tick=[](void *,uint32_t){};int object=0;
    assert(SRNOS_CANRouterAttachAdapter(ch,&object,&ops,100)==0);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,0)==1);
    assert(SRNOS_CANRouterDetachAdapter(ch,&object)==0);
    assert(SRNOS_CANRouterCoreNextTxWait(ch,0)==UINT32_MAX);
}

struct scripted_tx_probe_t {
    SRNOS_CANTxItem urgent[4], periodic[4];
    size_t urgent_count, periodic_count;
    struct commit_t { uintptr_t token; SRNOS_CANTxKind kind; } commits[8];
    size_t commit_count;
};
static scripted_tx_probe_t g_scripted_tx;
static struct captured_tx_t {
    SRNOS_CANFrame frame;
    SRNOS_CANRouterTxClass kind;
} g_captured_tx[8];
static size_t g_captured_count;

static size_t scripted_poll(void *obj, SRNOS_CANTxKind kind, SRNOS_CANTxItem *out,
                            size_t max, uint32_t)
{
    const auto &probe = *static_cast<scripted_tx_probe_t *>(obj);
    const bool urgent = kind == SRNOS_CAN_TX_KIND_URGENT;
    const size_t count = urgent ? probe.urgent_count : probe.periodic_count;
    assert(count <= max);
    std::memcpy(out, urgent ? probe.urgent : probe.periodic, count * sizeof(*out));
    return count;
}

static void scripted_commit(void *obj, SRNOS_CANTxKind kind, const SRNOS_CANTxItem *item,
                            SRNOS_CANTxResult result)
{
    auto &probe = *static_cast<scripted_tx_probe_t *>(obj);
    assert(result == SRNOS_CAN_TX_RESULT_ACCEPTED && probe.commit_count < 8u);
    probe.commits[probe.commit_count++] = {item->token, kind};
}

static bool capture_push(SRNOS_CANChannel ch, const SRNOS_CANFrame *tx,
                         SRNOS_CANRouterTxClass kind)
{
    assert(ch == SRNOS_CAN_CH_NTFDCAN2 && g_captured_count < 8u);
    g_captured_tx[g_captured_count++] = {*tx, kind};
    return true;
}

static SRNOS_CANTxItem group_item(uint32_t id, uint8_t slot, int16_t value, uintptr_t token)
{
    SRNOS_CANTxItem item{};
    item.format = SRNOS_CAN_TX_FORMAT_GROUP_I16;
    item.group_i16 = {id, 0u, slot, SRNOS_CAN_TX_ENDIAN_BE, value};
    item.token = token;
    return item;
}

static SRNOS_CANTxItem raw_item(uintptr_t token)
{
    SRNOS_CANTxItem item{};
    item.format = SRNOS_CAN_TX_FORMAT_RAW_FRAME;
    item.frame = frame(0x350u);
    item.frame.data[0] = 0xabu;
    item.token = token;
    return item;
}

static void drain_script(size_t frames, size_t commits)
{
    g_captured_count = 0u;
    g_scripted_tx.commit_count = 0u;
    SRNOS_CANRouterCoreDrainTx(SRNOS_CAN_CH_NTFDCAN2, ++g_now_ms);
    assert(g_captured_count == frames && g_scripted_tx.commit_count == commits);
}

static void expect_group(size_t index, uint32_t id, SRNOS_CANRouterTxClass kind,
                         uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    const auto &captured = g_captured_tx[index];
    assert(captured.kind == kind && captured.frame.id == id);
    assert(captured.frame.flags == 0u && captured.frame.dlc == 8u);
    const uint16_t values[] = {a, b, c, d};
    for (size_t slot = 0u; slot < 4u; ++slot) {
        assert(captured.frame.data[slot * 2u] == (values[slot] >> 8u));
        assert(captured.frame.data[slot * 2u + 1u] == (values[slot] & 0xffu));
    }
}

static void expect_commit(size_t index, uintptr_t token, SRNOS_CANTxKind kind)
{
    assert(g_scripted_tx.commits[index].token == token);
    assert(g_scripted_tx.commits[index].kind == kind);
}

static void expect_raw(size_t index)
{
    const auto &captured = g_captured_tx[index];
    const SRNOS_CANFrame expected = raw_item(0u).frame;
    assert(captured.kind == SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC);
    assert(captured.frame.id == expected.id && captured.frame.dlc == expected.dlc);
    assert(captured.frame.flags == expected.flags);
    assert(std::memcmp(captured.frame.data, expected.data, sizeof(expected.data)) == 0);
}

static void test_frozen_routes_and_group_scratch()
{
    const SRNOS_CANChannel ch = SRNOS_CAN_CH_NTFDCAN2;
    static adapter_probe_t receivers[3]{};
    static uint32_t raw_hits;
    for (size_t i = 0u; i < 3u; ++i) {
        receivers[i].filters[0] = filter(0x654u, i == 2u);
        receivers[i].filter_count = 1u;
        assert(SRNOS_CANRouterAttachAdapter(ch, &receivers[i], &k_adapter_ops, 1u) == 0);
    }
    assert(SRNOS_CANRouterSubscribeRaw(ch, filter(0x654u), raw_count, &raw_hits) == 0);
    static const SRNOS_CANAdapterOps script_ops = {
        nullptr, nullptr, nullptr, scripted_poll, scripted_commit, nullptr,
    };
    static const SRNOS_CANRouterBackendOps capture_backend = {
        nullptr, test_tx_free, capture_push, test_signal, test_now_ms, nullptr,
    };
    assert(SRNOS_CANRouterAttachAdapter(ch, &g_scripted_tx, &script_ops, 1u) == 0);
    SRNOS_CANRouterCoreSetBackend(ch, &capture_backend);
    SRNOS_CAN_FreezeConfiguration();

    const auto before = stats_copy(ch);
    SRNOS_CANFrame rx = frame(0x654u);
    SRNOS_CANRouterCoreOnRxFrame(ch, &rx, 100u);
    assert(receivers[0].rx_count == 1u && receivers[1].rx_count == 1u);
    assert(receivers[2].rx_count == 0u && raw_hits == 1u);
    rx.flags = SRNOS_CAN_FRAME_FLAG_EXT;
    SRNOS_CANRouterCoreOnRxFrame(ch, &rx, 101u);
    rx.id = 0x655u;
    SRNOS_CANRouterCoreOnRxFrame(ch, &rx, 102u);
    const auto after = stats_copy(ch);
    assert(receivers[0].rx_count == 1u && receivers[1].rx_count == 1u);
    assert(receivers[2].rx_count == 1u && raw_hits == 1u);
    assert(after.rx_routed_adapter - before.rx_routed_adapter == 3u);
    assert(after.rx_routed_raw - before.rx_routed_raw == 1u);
    assert(after.rx_no_subscriber - before.rx_no_subscriber == 1u);

    // Seed multiple groups and slots, then reuse only one accumulator for a new ID.
    g_scripted_tx.periodic[0] = group_item(0x210u, 0u, 0x1111, 1u);
    g_scripted_tx.periodic[1] = group_item(0x210u, 2u, 0x2222, 2u);
    g_scripted_tx.periodic[2] = group_item(0x211u, 3u, 0x3333, 3u);
    g_scripted_tx.periodic_count = 3u;
    drain_script(2u, 3u);
    expect_group(0u, 0x210u, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC, 0x1111, 0u, 0x2222, 0u);
    expect_group(1u, 0x211u, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC, 0u, 0u, 0u, 0x3333);
    expect_commit(0u, 1u, SRNOS_CAN_TX_KIND_PERIODIC);
    expect_commit(1u, 2u, SRNOS_CAN_TX_KIND_PERIODIC);
    expect_commit(2u, 3u, SRNOS_CAN_TX_KIND_PERIODIC);
    g_scripted_tx.periodic[0] = group_item(0x212u, 1u, 0x4444, 4u);
    g_scripted_tx.periodic_count = 1u;
    drain_script(1u, 1u);
    expect_group(0u, 0x212u, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC, 0u, 0x4444, 0u, 0u);
    expect_commit(0u, 4u, SRNOS_CAN_TX_KIND_PERIODIC);

    // A RAW-only periodic round must neither resend nor recommit the previous group.
    g_scripted_tx.periodic[0] = raw_item(5u);
    drain_script(1u, 1u);
    expect_raw(0u);
    expect_commit(0u, 5u, SRNOS_CAN_TX_KIND_PERIODIC);

    // Urgent and periodic share scratch, but each distinct ID starts with clean slots.
    g_scripted_tx.urgent[0] = group_item(0x213u, 0u, 0x5555, 6u);
    g_scripted_tx.urgent_count = 1u;
    g_scripted_tx.periodic[0] = group_item(0x214u, 3u, 0x6666, 7u);
    drain_script(2u, 2u);
    expect_group(0u, 0x213u, SRNOS_CAN_ROUTER_TX_CLASS_URGENT, 0x5555, 0u, 0u, 0u);
    expect_group(1u, 0x214u, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC, 0u, 0u, 0u, 0x6666);
    expect_commit(0u, 6u, SRNOS_CAN_TX_KIND_URGENT);
    expect_commit(1u, 7u, SRNOS_CAN_TX_KIND_PERIODIC);

    // Accepted slots of the same ID survive via its shadow, without stale commits.
    g_scripted_tx.urgent[0] = group_item(0x215u, 0u, 0x1234, 8u);
    g_scripted_tx.periodic[0] = group_item(0x215u, 1u, 0x5678, 9u);
    drain_script(2u, 2u);
    expect_group(0u, 0x215u, SRNOS_CAN_ROUTER_TX_CLASS_URGENT, 0x1234, 0u, 0u, 0u);
    expect_group(1u, 0x215u, SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC, 0x1234, 0x5678, 0u, 0u);
    expect_commit(0u, 8u, SRNOS_CAN_TX_KIND_URGENT);
    expect_commit(1u, 9u, SRNOS_CAN_TX_KIND_PERIODIC);

    g_scripted_tx.urgent[0] = group_item(0x216u, 2u, 0x7777, 10u);
    g_scripted_tx.periodic[0] = raw_item(11u);
    drain_script(2u, 2u);
    expect_group(0u, 0x216u, SRNOS_CAN_ROUTER_TX_CLASS_URGENT, 0u, 0u, 0x7777, 0u);
    expect_raw(1u);
    expect_commit(0u, 10u, SRNOS_CAN_TX_KIND_URGENT);
    expect_commit(1u, 11u, SRNOS_CAN_TX_KIND_PERIODIC);
    g_scripted_tx.urgent_count = g_scripted_tx.periodic_count = 0u;
    drain_script(0u, 0u);
}

static void test_unbound_clock()
{
    // Configuration/startup can enqueue before the backend worker binds.
    // HAL is already 12 ms ahead; this queue must keep task-clock deadlines.
    const auto f = frame(0x55);
    for (uint32_t start : {0u, UINT32_MAX - 5u}) {
        SRNOS_CANRouterCoreSetBackend(1, nullptr);
        g_now_ms = start;
        assert(SRNOS_CANRouterPostOneshot(1, &f, 0, 10) == 0);
        SRNOS_CANRouterCoreSetBackend(1, &k_backend);
        assert(SRNOS_CANRouterCoreNextTxWait(1, g_now_ms) == 10);
        g_now_ms += 10u;
        SRNOS_CANRouterCoreDrainTx(1, g_now_ms);
        assert(SRNOS_CANRouterCoreNextTxWait(1, g_now_ms) == UINT32_MAX);
    }
    g_now_ms = 0;
}
int main()
{
    test_unbound_clock();
    for (SRNOS_CANChannel ch = 1u; ch <= SRNOS_CAN_ROUTER_CHANNEL_COUNT; ++ch) {
        SRNOS_CANRouterCoreSetBackend(ch, &k_backend);
    }
    test_next_wait();
    test_route_capacity_and_order();
    test_raw_idempotence_fanout_and_ext();
    test_adapter_capacity();
    test_adapter_filters_detach_and_swap_refs();
    test_oneshot_dual_heap();
    test_oneshot_busy_keeps_item();
    assert(!SRNOS_CAN_ConfigurationFrozen());
    test_frozen_routes_and_group_scratch();
    assert(SRNOS_CAN_ConfigurationFrozen());
    SRNOS_CANRxFilter filter = {};
    assert(SRNOS_CANRouterAttachAdapter(1, nullptr, nullptr, 1) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterDetachAdapter(1, nullptr) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterSubscribeRaw(1, filter, nullptr, nullptr) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterUnsubscribeRaw(1, filter, nullptr, nullptr) == SRNOS_CAN_FROZEN);
    SRNOS_CANFrame runtime_frame = frame(0x55u);
    assert(SRNOS_CANRouterPostOneshot(1, &runtime_frame, 1, 10) == 0);
    std::puts("SRNOS_CANRouterCoreHostTest: PASS (frozen registration/fanout, group scratch reuse and shadow retention)");
    return 0;
}
