#include "IO/CAN/FD/can_fd_internal.h"
#include "IO/CAN/Router/can_router_core_internal.h"
#include "IO/CAN/can_controller_internal.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

uint32_t SRNOS_TestFDMask, SRNOS_TestFDException;
static uint32_t now_ms, critical_saved[8];
static unsigned critical_depth, signals, attempts, deliveries;
static int kernel_state = osKernelInactive;
static uint8_t free_level = 255;
static bool push_fails, inject_enqueue, callback_enqueue;
static uint32_t retry_ms = UINT32_MAX;
static uint32_t admission_delay_ms;
static std::vector<SRNOS_CANFDFrame> sent, received;
extern "C" uint32_t HAL_GetTick(void) { return now_ms + 12u; }
extern "C" uint32_t SRNOS_TimeMs(void) { return now_ms; }
extern "C" int osKernelGetState(void) { return kernel_state; }
extern "C" void SRNOS_TestFDCriticalEnter(void) {
    assert(critical_depth < 8); critical_saved[critical_depth++] = SRNOS_TestFDMask;
    __set_BASEPRI_MAX(80);
}
extern "C" void SRNOS_TestFDCriticalExit(void) {
    assert(critical_depth); SRNOS_TestFDMask = critical_saved[--critical_depth];
}
extern "C" SRNOS_SerialStatus SRNOS_SerialValidate(const SRNOS_SerialConfig *, size_t) {
    return SRNOS_SERIAL_OK; /* CAN/System tests replace unrelated UART validation only. */
}
static SRNOS_CANFDFrame frame(uint32_t id = 0x123, uint8_t length = 64, uint8_t flags = 0) {
    SRNOS_CANFDFrame result{};
    result.id = id; result.length = length; result.flags = flags;
    std::memset(result._pad, 0xaa, sizeof(result._pad));
    for (unsigned i = 0; i < 64; ++i) result.data[i] = static_cast<uint8_t>(i + id);
    return result;
}
static void check_copy(const SRNOS_CANFDFrame &copy, const SRNOS_CANFDFrame &original) {
    assert(copy.id == original.id && copy.length == original.length && copy.flags == original.flags);
    assert(!copy._pad[0] && !copy._pad[1]);
    assert(std::memcmp(copy.data, original.data, copy.length) == 0);
    for (unsigned i = copy.length; i < 64; ++i) assert(copy.data[i] == 0);
}
static uint8_t tx_free(SRNOS_CANChannel) {
    assert(!SRNOS_TestFDMask);
    now_ms += admission_delay_ms; admission_delay_ms = 0;
    return free_level;
}
static bool push(SRNOS_CANChannel channel, const SRNOS_CANFDFrame *value) {
    assert(!SRNOS_TestFDMask); ++attempts;
    if (inject_enqueue) {
        inject_enqueue = false;
        auto newer = frame(0x111, 12);
        assert(SRNOS_CANFDPostOneshot(channel, &newer, 0, 20) == SRNOS_CANFD_OK);
    }
    if (push_fails) return false;
    sent.push_back(*value); return true;
}
static void signal(SRNOS_CANChannel channel) { assert(!SRNOS_TestFDMask && channel); ++signals; }
static uint32_t clock_now() { return now_ms; }
static uint32_t retry(SRNOS_CANChannel, uint32_t) { assert(!SRNOS_TestFDMask); return retry_ms; }
static const SRNOS_CANFDBackendOps backend{tx_free, push, signal, clock_now, retry};
static void receive(SRNOS_CANChannel channel, const SRNOS_CANFDFrame *value, void *) {
    assert(!SRNOS_TestFDMask); ++deliveries; received.push_back(*value);
    if (callback_enqueue) {
        auto outgoing = frame(0x321, 64);
        assert(SRNOS_CANFDPostOneshot(channel, &outgoing, 0, 20) == SRNOS_CANFD_OK);
    }
    assert(SRNOS_CANFDUnsubscribeRaw(channel, {value->id, static_cast<uint8_t>(value->flags & 1u)},
                                    receive, nullptr) == SRNOS_CANFD_FROZEN);
}
static void classic_receive(SRNOS_CANChannel, const SRNOS_CANFrame *, void *) {}
static void adapter_tick(void *, uint32_t) {}
static std::array<SRNOS_CANControllerConfig, 4> configs(bool all_fd = false, bool brs = false) {
    std::array<SRNOS_CANControllerConfig, 4> result;
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = (all_fd || i == 0) ? SRNOS_CANControllerConfig{SRNOS_CAN_MODE_FD, 1000000, brs ? 4000000u : 1000000u, brs}
                                     : SRNOS_CANControllerConfig{SRNOS_CAN_MODE_CLASSIC, 1000000, 0, false};
    return result;
}
static void ready(bool all_fd = false, bool brs = false) {
    auto config = configs(all_fd, brs);
    assert(SRNOS_CANControllerPrepare(config.data()));
    SRNOS_CAN_FreezeConfiguration();
    SRNOS_CANFDCorePrepareResources();
    kernel_state = osKernelRunning;
    for (uint8_t channel = 1; channel <= 4; ++channel) SRNOS_CANFDCoreSetBackend(channel, &backend);
}
static SRNOS_CANFDStats stats(uint8_t channel = 1) {
    SRNOS_CANFDStats result{}; assert(SRNOS_CANFDGetStats(channel, &result)); return result;
}
static void registration() {
    assert(!SRNOS_CANFDCoreHasRegistrations(0));
    assert(SRNOS_CANFDSubscribeRaw(0, {1, 0}, receive, nullptr) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDSubscribeRaw(1, {0x800, 0}, receive, nullptr) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDSubscribeRaw(1, {0x20000000, 1}, receive, nullptr) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDSubscribeRaw(1, {1, 2}, receive, nullptr) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDSubscribeRaw(1, {1, 0}, nullptr, nullptr) == SRNOS_CANFD_INVALID);
    SRNOS_TestFDException = 16;
    assert(SRNOS_CANFDSubscribeRaw(1, {1, 0}, receive, nullptr) == SRNOS_CANFD_INVALID);
    SRNOS_TestFDException = 0;
    /* FD subscriptions are legal while ConfigOf still says default Classic. */
    assert(SRNOS_CANControllerIsClassic(1));
    for (unsigned i = 0; i < 32; ++i)
        assert(SRNOS_CANFDSubscribeRaw(1, {31u - i, 0}, receive, nullptr) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDSubscribeRaw(1, {0, 0}, receive, nullptr) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDSubscribeRaw(1, {32, 0}, receive, nullptr) == SRNOS_CANFD_FULL);
    assert(SRNOS_CANFDUnsubscribeRaw(1, {10, 0}, receive, nullptr) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDUnsubscribeRaw(1, {10, 0}, receive, nullptr) == SRNOS_CANFD_NOT_FOUND);
    assert(SRNOS_CANFDSubscribeRaw(1, {31, 0}, receive, reinterpret_cast<void *>(1)) == SRNOS_CANFD_OK);
    ready();
    auto value = frame(31, 64);
    callback_enqueue = true;
    SRNOS_CANFDCoreOnRxFrame(1, &value, now_ms);
    assert(deliveries == 2 && stats().route_count == 32 && stats().tx_depth == 2);
    SRNOS_CANFDCoreDrainTx(1, now_ms);
    assert(sent.size() == 2);
    assert(SRNOS_CANFDSubscribeRaw(1, {40, 0}, receive, nullptr) == SRNOS_CANFD_FROZEN);
}
static void lengths() {
    assert(SRNOS_CANFDSubscribeRaw(1, {0x123, 0}, receive, nullptr) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDSubscribeRaw(1, {0x1fffffff, 1}, receive, nullptr) == SRNOS_CANFD_OK);
    ready();
    constexpr uint8_t lengths[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    for (unsigned i = 0; i < sizeof(lengths); ++i) {
        const auto original = frame(0x123, lengths[i]);
        auto outgoing = original;
        assert(SRNOS_CANFDLengthToDlc(lengths[i]) == i && SRNOS_CANFDDlcToLength(i) == lengths[i]);
        assert(SRNOS_CANFDPostOneshot(1, &outgoing, 1, 20) == SRNOS_CANFD_OK);
        std::memset(outgoing.data, 0, 64); /* Queue owns the full submitted payload. */
        SRNOS_CANFDCoreDrainTx(1, now_ms);
        check_copy(sent.back(), original);
        auto incoming = original; incoming.flags |= SRNOS_CANFD_FRAME_FLAG_BRS | SRNOS_CANFD_FRAME_FLAG_ESI;
        SRNOS_CANFDCoreOnRxFrame(1, &incoming, now_ms);
        check_copy(received.back(), incoming);
    }
    auto extended = frame(0x1fffffff, 64, 1);
    assert(SRNOS_CANFDPostOneshot(1, &extended, 1, 0) == SRNOS_CANFD_OK);
    SRNOS_CANFDCoreDrainTx(1, now_ms); check_copy(sent.back(), extended);
    SRNOS_CANFDCoreOnRxFrame(1, &extended, now_ms); check_copy(received.back(), extended);
    assert(deliveries == 17 && stats().tx_accepted == 17 && stats().rx_received == 17);
    for (unsigned length = 0; length <= 255; ++length) {
        bool legal = false; for (uint8_t candidate : lengths) legal |= length == candidate;
        if (legal) continue;
        auto invalid = frame(); invalid.length = static_cast<uint8_t>(length);
        assert(SRNOS_CANFDPostOneshot(1, &invalid, 0, 20) == SRNOS_CANFD_INVALID);
        SRNOS_CANFDCoreOnRxFrame(1, &invalid, now_ms);
    }
    assert(stats().rx_rejected == 240);
    assert(SRNOS_CANFDDlcToLength(16) == 0xff);
}
static void invalid() {
    auto value = frame();
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    auto config = configs(false, true); assert(SRNOS_CANControllerPrepare(config.data()));
    SRNOS_CAN_FreezeConfiguration(); kernel_state = osKernelRunning;
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    SRNOS_CANFDCorePrepareResources(); SRNOS_CANFDCoreSetBackend(1, &backend);
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID); // Missing BRS.
    value.flags = SRNOS_CANFD_FRAME_FLAG_BRS;
    assert(SRNOS_CANFDPostOneshot(2, &value, 0, 20) == SRNOS_CANFD_MODE_MISMATCH);
    assert(SRNOS_CANFDPostOneshot(5, &value, 0, 20) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDPostOneshot(1, nullptr, 0, 20) == SRNOS_CANFD_INVALID);
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, UINT32_MAX) == SRNOS_CANFD_INVALID);
    for (uint8_t flags : {uint8_t(4), uint8_t(6), uint8_t(8), uint8_t(255)}) {
        value.flags = flags; assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    }
    value.flags = 2; value.id = 0x800;
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    value.flags = 3; value.id = 0x20000000;
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    value = frame(0x123, 64, 2); SRNOS_TestFDException = 16;
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_INVALID);
    SRNOS_CANFDStats snapshot{}; assert(!SRNOS_CANFDGetStats(1, &snapshot));
    SRNOS_CANFDCoreNoteTxComplete(1); SRNOS_CANFDCoreNoteRxWake(1, true);
    SRNOS_TestFDException = 0;
    for (uint32_t mask : {0u, 48u, 112u}) {
        SRNOS_TestFDMask = mask;
        assert(SRNOS_CANFDGetStats(1, &snapshot) && SRNOS_TestFDMask == mask);
    }
    SRNOS_TestFDMask = 0;
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, INT32_MAX) == SRNOS_CANFD_OK);
    SRNOS_CANFDCoreDrainTx(1, 0); assert(sent.size() == 1 && sent[0].flags == 2);
    value.flags = 8; SRNOS_CANFDCoreOnRxFrame(1, &value, 0);
    value = frame(); SRNOS_CANFDCoreOnRxFrame(2, &value, 0);
    assert(stats().rx_rejected == 1 && stats(2).rx_rejected == 1 && stats().tx_complete_events == 1);
    SRNOS_CANFrame classic{}; classic.id = 1; classic.dlc = 1;
    assert(SRNOS_CANRouterPostOneshot(1, &classic, 0, 20) != 0);
}
static void queue() {
    ready();
    for (unsigned i = 0; i < 64; ++i) {
        const auto value = frame(i, 64);
        assert(SRNOS_CANFDPostOneshot(1, &value, static_cast<uint8_t>(i % 4), 20) == SRNOS_CANFD_OK);
    }
    auto value = frame(99, 64);
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_FULL);
    assert(stats().tx_depth == 64 && stats().tx_high_water == 64 && signals == 64);
    SRNOS_CANFDCorePrepareResources(); // Idempotence must not recycle live slots.
    SRNOS_CANFDCoreDrainTx(1, 0);
    assert(sent.size() == 32 && stats().tx_depth == 32 && signals == 65);
    SRNOS_CANFDCoreDrainTx(1, 0);
    assert(sent.size() == 64 && stats().tx_depth == 0 && signals == 65);
    for (unsigned i = 0; i < 64; ++i) assert(sent[i].id == (i % 16) * 4 + i / 16);
    assert(SRNOS_CANFDCoreNextTxWait(1, 0) == UINT32_MAX);
    /* A higher-priority producer during an unlocked hardware call cannot
     * overwrite/remove the selected slot or disturb its payload. */
    assert(SRNOS_CANFDPostOneshot(1, &value, 5, 20) == SRNOS_CANFD_OK);
    inject_enqueue = true;
    SRNOS_CANFDCoreDrainTx(1, 0);
    assert(sent[64].id == 99 && sent[65].id == 0x111 && stats().tx_depth == 0);
    for (unsigned i = 0; i < 64; ++i) assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDPostOneshot(1, &value, 0, 20) == SRNOS_CANFD_FULL);
}
static void unbound_clock() {
    ready();
    const auto value=frame(0x55);
    for(uint32_t start : {0u,UINT32_MAX-5u}) {
        SRNOS_CANFDCoreSetBackend(1,nullptr);
        now_ms=start;
        assert(SRNOS_CANFDPostOneshot(1,&value,0,10)==SRNOS_CANFD_OK);
        SRNOS_CANFDCoreSetBackend(1,&backend);
        assert(SRNOS_CANFDCoreNextTxWait(1,now_ms)==10);
        now_ms+=10u;SRNOS_CANFDCoreDrainTx(1,now_ms);
        assert(stats().tx_depth==0 && sent.empty());
    }
    assert(stats().tx_expired==2);
}
static void expiry() {
    ready(true);
    now_ms = UINT32_MAX - 5u;
    auto old = frame(1), longer = frame(2);
    assert(SRNOS_CANFDPostOneshot(1, &old, 0, 10) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDPostOneshot(1, &longer, 1, 20) == SRNOS_CANFD_OK);
    push_fails = true; retry_ms = 1;
    SRNOS_CANFDCoreDrainTx(1, now_ms);
    assert(attempts == 1 && stats().tx_depth == 2 && SRNOS_CANFDCoreNextTxWait(1, now_ms) == 1);
    retry_ms = UINT32_MAX;
    assert(SRNOS_CANFDCoreNextTxWait(1, now_ms) == 10);
    now_ms += 9u; SRNOS_CANFDCoreDrainTx(1, now_ms);
    assert(stats().tx_expired == 0 && SRNOS_CANFDCoreNextTxWait(1, now_ms) == 1);
    ++now_ms; push_fails = false; SRNOS_CANFDCoreDrainTx(1, now_ms);
    assert(sent.size() == 1 && sent[0].id == 2 && stats().tx_expired == 1);
    assert(SRNOS_CANFDCoreNextTxWait(1, now_ms) == UINT32_MAX);
    now_ms = 100;
    for (unsigned i = 0; i < 64; ++i) {
        auto value = frame(i);
        assert(SRNOS_CANFDPostOneshot(4, &value, i % 3, (i % 2) ? 20 : 1) == SRNOS_CANFD_OK);
    }
    free_level = 0; now_ms = 101; SRNOS_CANFDCoreDrainTx(4, now_ms);
    assert(stats(4).tx_expired == 32 && stats(4).tx_depth == 32);
    assert(SRNOS_CANFDCoreNextTxWait(4, now_ms) == 19);
    now_ms = 120; SRNOS_CANFDCoreDrainTx(4, now_ms);
    assert(stats(4).tx_expired == 64 && stats(4).tx_depth == 0);
    assert(SRNOS_CANFDCoreNextTxWait(4, now_ms) == UINT32_MAX);
    assert(SRNOS_CANFDPostOneshot(4, &old, 0, 0) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDCoreNextTxWait(4, now_ms) == 20);
    /* FIFO admission itself uses SPI on CAN4. Its latency does not extend
     * the queued TTL; a later unexpired frame can still be submitted. */
    now_ms = 200; free_level = 255; admission_delay_ms = 10;
    assert(SRNOS_CANFDPostOneshot(2, &old, 0, 5) == SRNOS_CANFD_OK);
    assert(SRNOS_CANFDPostOneshot(2, &longer, 1, 20) == SRNOS_CANFD_OK);
    SRNOS_CANFDCoreDrainTx(2, now_ms);
    assert(stats(2).tx_expired == 1 && stats(2).tx_accepted == 1 && sent.back().id == 2);
}
static void conflicts(const char *kind) {
    auto config = configs();
    if (!std::strcmp(kind, "fd_on_classic")) {
        config[0] = {SRNOS_CAN_MODE_CLASSIC, 1000000, 0, false};
        assert(SRNOS_CANFDSubscribeRaw(1, {1, 0}, receive, nullptr) == SRNOS_CANFD_OK);
    } else if (!std::strcmp(kind, "classic_on_fd")) {
        assert(SRNOS_CANRouterSubscribeRaw(1, {1, 0}, classic_receive, nullptr) == 0);
    } else if (!std::strcmp(kind, "queued_classic")) {
        SRNOS_CANFrame value{}; assert(SRNOS_CANRouterPostOneshot(1, &value, 0, 20) == 0);
    } else {
        static unsigned motor;
        SRNOS_CANAdapterOps ops{}; ops.tick = adapter_tick;
        assert(SRNOS_CANRouterAttachAdapter(1, &motor, &ops, 1) == 0);
        config[0] = {SRNOS_CAN_MODE_CLASSIC, 500000, 0, false};
    }
    assert(!SRNOS_CANControllerPrepare(config.data()));
    assert(SRNOS_CANControllerIsClassic(1) && !SRNOS_CAN_ConfigurationFrozen());
    assert(!SRNOS_CANControllerGetConfig(1, &config[0]));
}
static void stress() {
    ready();
    struct Pending { SRNOS_CANFDFrame value; uint32_t deadline; uint8_t priority; };
    std::vector<Pending> expected;
    unsigned sequence = 0, expected_expired = 0, expected_accepted = 0;
    uint32_t random = 0x156f02ab;
    now_ms = UINT32_MAX - 1000u;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        random = random * 1664525u + 1013904223u;
        if ((random & 3u) != 0u) {
            auto value = frame(sequence & 0x7ffu);
            std::memcpy(value.data, &sequence, sizeof(sequence)); ++sequence;
            const uint8_t priority = (random >> 4) & 7u;
            const uint32_t ttl = 1u + ((random >> 8) & 31u);
            const bool full = expected.size() == 64;
            const auto result = SRNOS_CANFDPostOneshot(1, &value, priority, ttl);
            assert(result == (full ? SRNOS_CANFD_FULL : SRNOS_CANFD_OK));
            if (!full) {
                value._pad[0] = value._pad[1] = 0;
                auto position = std::find_if(expected.begin(), expected.end(),
                    [priority](const Pending &p) { return p.priority > priority; });
                expected.insert(position, {value, now_ms + ttl, priority});
            }
        } else {
            now_ms += (random >> 12) & 7u;
            free_level = (random & 0x100u) ? 255 : 0;
            push_fails = (random & 0x600u) == 0;
            const size_t before = expected.size();
            expected.erase(std::remove_if(expected.begin(), expected.end(), [](const Pending &p) {
                return static_cast<int32_t>(now_ms - p.deadline) >= 0;
            }), expected.end());
            expected_expired += before - expected.size();
            const size_t accepted = free_level && !push_fails ? std::min<size_t>(32, expected.size()) : 0;
            const size_t sent_before = sent.size();
            SRNOS_CANFDCoreDrainTx(1, now_ms);
            assert(sent.size() == sent_before + accepted);
            for (size_t i = 0; i < accepted; ++i)
                assert(!std::memcmp(&sent[sent_before + i], &expected[i].value, sizeof(SRNOS_CANFDFrame)));
            expected.erase(expected.begin(), expected.begin() + accepted);
            expected_accepted += accepted;
        }
        const auto current = stats();
        assert(current.tx_depth == expected.size() && current.tx_expired == expected_expired &&
               current.tx_accepted == expected_accepted);
    }
}
static void system_config() {
    SRNOS_SystemConfig config;
    SRNOS_SystemConfigDefaults(&config);
    assert(SRNOS_SystemValidate(&config) == SRNOS_SYSTEM_OK);
    for (const auto &can : config.can)
        assert(can.mode == SRNOS_CAN_MODE_CLASSIC && can.nominal_bitrate == 1000000 && !can.brs && !can.data_bitrate);
    const uint32_t heap = SRNOS_SystemHeapBudget(&config);
    for (unsigned i = 0; i < 4; ++i) config.can[i] = {SRNOS_CAN_MODE_FD, 1000000, 4000000, true};
    assert(SRNOS_SystemValidate(&config) == SRNOS_SYSTEM_OK && SRNOS_SystemHeapBudget(&config) == heap);
    config.can[2].data_bitrate = 3000000;
    assert(SRNOS_SystemValidate(&config) == SRNOS_SYSTEM_INVALID);
    config.can[2] = {SRNOS_CAN_MODE_FD, 250000, 250000, false};
    assert(SRNOS_SystemValidate(&config) == SRNOS_SYSTEM_OK);
    config.can[2].data_bitrate = 1000000;
    assert(SRNOS_SystemValidate(&config) == SRNOS_SYSTEM_INVALID);
    auto settings = configs(true);
    assert(SRNOS_CANControllerPrepare(settings.data()));
    assert(!SRNOS_CANControllerPrepare(settings.data()));
    SRNOS_CAN_FreezeConfiguration();
    SRNOS_CANControllerConfig copy{}; assert(SRNOS_CANControllerGetConfig(4, &copy));
    assert(copy.mode == SRNOS_CAN_MODE_FD && copy.data_bitrate == 1000000);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!std::strcmp(argv[1], "registration")) registration();
    else if (!std::strcmp(argv[1], "lengths")) lengths();
    else if (!std::strcmp(argv[1], "invalid")) invalid();
    else if (!std::strcmp(argv[1], "queue")) queue();
    else if (!std::strcmp(argv[1], "expiry")) expiry();
    else if (!std::strcmp(argv[1], "unbound_clock")) unbound_clock();
    else if (!std::strcmp(argv[1], "system")) system_config();
    else if (!std::strcmp(argv[1], "stress")) stress();
    else conflicts(argv[1]);
    assert(!SRNOS_TestFDMask && !critical_depth);
    std::printf("CAN FD core %s: PASS\n", argv[1]);
}
