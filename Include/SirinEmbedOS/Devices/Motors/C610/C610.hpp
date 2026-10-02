// === C610.hpp ===
// C610 最小适配：
// - 与 C620 的 ID/组帧规则一致：1..4 -> TX 0x200，5..8 -> TX 0x1FF；反馈 RX = 0x200 + motor_id
// - 16-bit 指令（大端上行），常见作为电流原始值
// - C610 指令范围：[-10000, 10000]
// - C610 反馈 data[6]/data[7] 为空（无温度反馈），temp_C 固定为 0

#ifndef SRNOS_MOTOR_C610_HPP
#define SRNOS_MOTOR_C610_HPP

#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Devices/CANFrame.h"

namespace SRNOS::Devices {

// ---- 公共 CAN 帧描述（最小集合）----

// ---- 反馈状态 ----
struct C610State {
    // feedback
    uint16_t mech_angle_raw = 0;
    float    mech_angle_deg = 0.0f;
    int16_t  speed_raw = 0;
    int16_t  iq_raw = 0;
    uint8_t  temp_C = 0; // C610 无温度反馈，固定 0

    // meta
    uint32_t last_rx_ms = 0;
    bool     online = false;

    // previous snapshot
    uint16_t prev_angle_raw = 0;
    int16_t  prev_speed_raw = 0;
    int16_t  prev_iq_raw = 0;
};

class DJI_C610 {
public:
    /**
     * motor_id: 1..8
     * rx_id:    0 = auto (0x200 + motor_id); 非 0 则使用传入值
     * group_id: auto by motor_id (1..4 -> 0x200, 5..8 -> 0x1FF)
     */
    explicit DJI_C610(uint8_t motor_id, uint32_t rx_id = 0);

    uint8_t  motorId() const { return motor_id_; }
    uint32_t groupId() const { return group_id_; }
    uint32_t rxId()    const { return rx_id_;   }

    // C610 current raw range: [-10000, 10000]
    void     set_give_cmd(int16_t cmd_raw);
    int16_t  give_cmd() const { return give_cmd_raw_.load(std::memory_order_relaxed); }
    int16_t  dbg_give_cmd_shadow() const { return dbg_cmd_shadow_; }

    void onRxFeedback(const CANFrame& f, uint32_t now_ms);
    C610State state() const noexcept {
        C610State s{};
        (void)snapshot(s);
        return s;
    }
    bool snapshot(C610State& out) const;

    void set_offline_timeout(uint32_t ms) { offline_ms_ = ms; }
    void tick(uint32_t now_ms);

    // 导出到 TX 路由：一个对象 -> (group_id, slot, 16-bit)
    bool exportTx16(uint16_t* group_id, uint8_t* slot, int16_t* val) const;

private:
    const uint8_t  motor_id_;
    const uint32_t group_id_;
    const uint32_t rx_id_;

    std::atomic<int16_t> give_cmd_raw_{0};
    volatile int16_t dbg_cmd_shadow_{0};
    mutable uint32_t state_seq_{0};
    mutable C610State state_{};
    uint32_t  offline_ms_ = 100;
};

void C610_ComposeTxFrames(DJI_C610* const motors[],
                          std::size_t n,
                          CANFrame out[],
                          std::size_t& out_count);

const SRNOS_CANAdapterOps* DJI_C610_CanOps();
int DJI_C610_AttachCan(SRNOS_CANChannel ch, DJI_C610* obj, uint32_t period_ms);
int DJI_C610_DetachCan(SRNOS_CANChannel ch, DJI_C610* obj);

} // namespace SRNOS::Devices

#endif // SRNOS_MOTOR_C610_HPP
