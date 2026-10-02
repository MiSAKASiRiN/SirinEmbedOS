#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "can_router_collections.hpp"
#include "can_router_core_internal.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace SRNOS_CANRouterDetail;

static uint32_t g_now_ms;
static bool g_backend_busy;
static bool g_detach_during_push;
static void *g_detach_obj;
static SRNOS_CANChannel g_detach_ch;

extern "C" uint32_t HAL_GetTick(void)
{
    return g_now_ms;
}

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
int main()
{
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
    SRNOS_CAN_FreezeConfiguration();
    assert(SRNOS_CAN_ConfigurationFrozen());
    SRNOS_CANRxFilter filter = {};
    assert(SRNOS_CANRouterAttachAdapter(1, nullptr, nullptr, 1) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterDetachAdapter(1, nullptr) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterSubscribeRaw(1, filter, nullptr, nullptr) == SRNOS_CAN_FROZEN);
    assert(SRNOS_CANRouterUnsubscribeRaw(1, filter, nullptr, nullptr) == SRNOS_CAN_FROZEN);
    SRNOS_CANFrame runtime_frame = frame(0x55u);
    assert(SRNOS_CANRouterPostOneshot(1, &runtime_frame, 1, 10) == 0);
    std::puts("SRNOS_CANRouterCoreHostTest: PASS (including frozen registration)");
    return 0;
}
