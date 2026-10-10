#include "SirinEmbedOS/Devices/Motors/C610/C610.hpp"
#include "SirinEmbedOS/Devices/Motors/C620/C620.hpp"
#include "SirinEmbedOS/Devices/Motors/GM6020/GM6020.hpp"
#include "SirinEmbedOS/Devices/Motors/DM4310/DM4310.hpp"
#include "SirinEmbedOS/Devices/Motors/DM4340/DM4340.hpp"
#include "SirinEmbedOS/Devices/Motors/LK8016E/LK8016E.hpp"
#include "state_guard.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using namespace SRNOS::Devices;
extern "C" int SRNOS_CANRouterAttachAdapter(SRNOS_CANChannel, void*, const SRNOS_CANAdapterOps*, uint32_t) { return 0; }
extern "C" int SRNOS_CANRouterDetachAdapter(SRNOS_CANChannel, void*) { return 0; }

static void be16(uint8_t* p, uint16_t value) { p[0] = value >> 8; p[1] = value & 255; }
static void le16(uint8_t* p, uint16_t value) { p[0] = value & 255; p[1] = value >> 8; }
static CANFrame dji_frame(uint32_t id, uint32_t sequence) {
    CANFrame f{}; f.id = id; f.dlc = 8;
    const uint16_t angle = sequence & 0x1fff;
    be16(f.data, angle); be16(f.data + 2, angle + 100);
    be16(f.data + 4, static_cast<uint16_t>(-static_cast<int16_t>(angle + 100)));
    f.data[6] = sequence & 255;
    return f;
}
static CANFrame dm_frame(uint32_t sequence) {
    CANFrame f{}; f.dlc = 8; f.data[0] = 1;
    be16(f.data + 1, sequence & 0xffff);
    const uint16_t vel = sequence & 0xfff, torque = vel ^ 0xaaa;
    f.data[3] = vel >> 4; f.data[4] = static_cast<uint8_t>((vel << 4) | (torque >> 8));
    f.data[5] = torque & 255; f.data[6] = sequence & 255; f.data[7] = (sequence >> 8) & 255;
    return f;
}
static CANFrame lk_frame(uint32_t sequence) {
    CANFrame f{}; f.id = 0x141; f.dlc = 8; f.data[0] = 0xa1;
    f.data[1] = sequence & 0x7f;
    le16(f.data + 2, sequence & 0x7ff); le16(f.data + 4, (sequence & 0x7ff) + 400);
    le16(f.data + 6, sequence & 0xffff);
    return f;
}

template<class Motor, class Make, class Check>
static void exercise(const char* name, Motor& motor, Make make, Check check) {
    auto state = motor.state();
    assert(!state.online && state.last_rx_ms == 0);
    motor.onRxFeedback(make(1000), 1000);
    assert(motor.snapshot(state)); check(state);
    motor.tick(899); assert(motor.state().online); // stale TX time predates new RX
    motor.tick(1100); assert(motor.state().online); // strict > threshold
    motor.tick(1101); assert(!motor.state().online);
    motor.onRxFeedback(make(UINT32_MAX - 10u), UINT32_MAX - 10u);
    motor.tick(5); assert(motor.state().online); // normal tick rollover
    motor.tick(100); assert(!motor.state().online);
    motor.set_offline_timeout(0);
    motor.onRxFeedback(make(801), 801);
    motor.tick(801); assert(motor.state().online);
    motor.tick(802); assert(!motor.state().online);
    motor.set_offline_timeout(100);
    motor.onRxFeedback(make(1), 1);

    // Preserve the caller's mask, including a stronger mask and nested guards.
    for (uint32_t saved : {0u, 48u, 112u}) {
        __set_BASEPRI(saved);
        assert(motor.snapshot(state)); assert(__get_BASEPRI() == saved);
        { const Detail::StateGuard outer;
          const uint32_t masked = __get_BASEPRI();
          assert(masked && masked <= configMAX_SYSCALL_INTERRUPT_PRIORITY);
          assert(motor.snapshot(state)); assert(__get_BASEPRI() == masked); }
        assert(__get_BASEPRI() == saved);
        __set_BASEPRI(0);
    }
    SRNOS_TestMotorException(16, 5);
    assert(motor.snapshot(state)); assert(__get_BASEPRI() == 0);
    SRNOS_TestMotorException(0, 0);

    std::atomic<uint32_t> published{1};
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint32_t i = 2; i <= 20000; ++i) {
            motor.onRxFeedback(make(i), i);
            published.store(i, std::memory_order_release);
        }
        done.store(true, std::memory_order_release);
    });
    std::thread ticker([&] {
        while (!done.load(std::memory_order_acquire)) {
            motor.tick(published.load(std::memory_order_acquire) - 1u);
            motor.set_offline_timeout(100);
        }
    });
    const auto read = [&] {
        do {
            auto sample = motor.state();
            assert(sample.online);
            check(sample);
        } while (!done.load(std::memory_order_acquire));
    };
    std::thread reader1(read), reader2(read);
    writer.join(); ticker.join(); reader1.join(); reader2.join();
    assert(motor.snapshot(state) && state.last_rx_ms == 20000 && state.online);
    check(state);
    auto invalid = make(30000); invalid.dlc = 0;
    motor.onRxFeedback(invalid, 30000);
    assert(motor.state().last_rx_ms == 20000);
    std::printf("PASS: %s coherent RX/tick/snapshot, stale time, rollover, nested masks\n", name);
}

int main() {
    const auto dji_check = [](const auto& s) {
        assert(s.mech_angle_raw == (s.last_rx_ms & 0x1fff));
        assert(s.speed_raw == static_cast<int16_t>(s.mech_angle_raw + 100));
        assert(s.iq_raw == -s.speed_raw);
    };
    DJI_C610 c610(1); exercise("C610", c610, [](uint32_t n) { return dji_frame(0x201, n); }, dji_check);
    DJI_C620 c620(1); exercise("C620", c620, [](uint32_t n) { return dji_frame(0x201, n); }, dji_check);
    DJI_GM6020 gm6020(1); exercise("GM6020", gm6020, [](uint32_t n) { return dji_frame(0x205, n); }, dji_check);
    DM4310 dm4310(1); exercise("DM4310", dm4310, dm_frame, [](const auto& s) {
        assert(static_cast<uint16_t>(s.pos_raw) == (s.last_rx_ms & 0xffff));
        assert(s.speed_rpm == static_cast<int16_t>(s.last_rx_ms & 0xfff));
        assert(s.torque_raw == (s.speed_rpm ^ 0xaaa));
    });
    DM4340 dm4340(1); exercise("DM4340", dm4340, dm_frame, [](const auto& s) {
        assert(s.pos_raw == static_cast<int16_t>((s.last_rx_ms & 0xffff) - 32768));
        assert(s.speed_raw == static_cast<int16_t>((s.last_rx_ms & 0xfff) - 2048));
        assert(s.torque_raw == static_cast<int16_t>(((s.last_rx_ms & 0xfff) ^ 0xaaa) - 2048));
    });
    LK8016E lk8016e(1); exercise("LK8016E", lk8016e, lk_frame, [](const auto& s) {
        assert(s.encoder_raw == (s.last_rx_ms & 0xffff));
        assert(s.iq_raw == static_cast<int16_t>(s.last_rx_ms & 0x7ff));
        assert(s.speed_dps == s.iq_raw + 400);
    });
}
