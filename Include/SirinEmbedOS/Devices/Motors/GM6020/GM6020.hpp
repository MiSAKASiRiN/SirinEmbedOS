// === GM6020.hpp ===
// GM6020 适配：
// - motor_id: 1..7
// - RX 反馈 ID：rx_id=0 -> auto = 0x205 + (motor_id-1)
// - TX 聚合：1..4 -> 0x1FF，5..7 -> 0x2FF；每帧 4×int16（BE）
// - 提供 exportTx16() 供 TX 路由器聚合；保留 GM6020_ComposeTxFrames() 直发两帧
//
// 线程安全：
// - set_give_cmd() 使用 atomic
// - onRxFeedback()/tick()/snapshot() 通过短临界区保护 state_
// - 其他任务读取请用 snapshot()

#ifndef SRNOS_MOTOR_GM6020_HPP
#define SRNOS_MOTOR_GM6020_HPP

#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Devices/CANFrame.h"

namespace SRNOS::Devices {


struct GM6020State {
    // feedback
    uint16_t mech_angle_raw = 0;   // 0..8191
    float    mech_angle_deg = 0.0f;
    int16_t  speed_raw = 0;
    int16_t  iq_raw = 0;
    uint8_t  temp_C = 0;

    // meta
    uint32_t last_rx_ms = 0;
    bool     online = false;

    // previous snapshot
    uint16_t prev_angle_raw = 0;
    int16_t  prev_speed_raw = 0;
    int16_t  prev_iq_raw = 0;
};

class DJI_GM6020 {
public:
    /**
     * motor_id: 1..7
     * rx_id:    0 = auto (0x205 + (motor_id-1)); 非 0 则使用传入值
     * group_id: auto by motor_id (1..4 -> 0x1FF, 5..7 -> 0x2FF)
     */
    explicit DJI_GM6020(uint8_t motor_id, uint32_t rx_id = 0);

    // identification（构造后固定）
    uint8_t  motorId() const { return motor_id_; }
    uint32_t groupId() const { return group_id_; }
    uint32_t rxId()    const { return rx_id_;   }

    // === control (protocol raw) ===
    // 常用为 16-bit 电流/力矩指令；范围按项目约定（给出合理的夹限）
    void     set_give_cmd(int16_t cmd_raw);
    int16_t  give_cmd() const { return give_cmd_raw_.load(std::memory_order_relaxed); }

    // === feedback ===
    void onRxFeedback(const CANFrame& f, uint32_t now_ms);
    void set_offline_timeout(uint32_t ms) { offline_ms_.store(ms, std::memory_order_relaxed); }
    void tick(uint32_t now_ms);

    // ---- 快照读取（推荐给其他任务用）----
    // 短临界区复制，无自旋；任务或 IRQ 优先级 5..15 调用。
    // Task context or IRQ priority 5..15; bounded copy, no spin.
    bool snapshot(GM6020State& out) const;
    GM6020State state() const noexcept {
        GM6020State s{};
        (void)snapshot(s);
        return s;
    }

    // === TX 路由导出接口（供 SRNOS_NativeCANTxRouterWorker 调用）===
    // 返回 (group_id, slot[0..3], 16-bit 值)，打包端统一走 BE
    bool exportTx16(uint16_t* group_id, uint8_t* slot, int16_t* val) const;

private:
    const uint8_t  motor_id_;      // 1..7
    const uint32_t group_id_;      // 0x1FF or 0x2FF
    const uint32_t rx_id_;         // 0x205 + (motor_id-1) when rx_id==0

    // 下发命令
    std::atomic<int16_t> give_cmd_raw_{0};

    // 状态 & 并发保护
    // RX、超时与快照由实现内的短临界区串行。
    GM6020State state_{};

    std::atomic<uint32_t> offline_ms_{100};
};

/**
 * 批量组帧（最多两帧）：
 *   - 0x1FF for ids 1..4  (bytes: id1,id2,id3,id4 each int16 **BE**)
 *   - 0x2FF for ids 5..7  (bytes: id5,id6,id7,id8 each int16 **BE**；id8 通常留空=0)
 * 缺失槽位补 0。
 *
 * motors: 指针数组（可含 nullptr）
 * n:      元素数量
 * out:    至少容纳 2 帧
 * out_count: 返回有效帧数(0..2)
 */
void GM6020_ComposeTxFrames(DJI_GM6020* const motors[],
                            std::size_t n,
                            CANFrame out[],
                            std::size_t& out_count);

const SRNOS_CANAdapterOps* DJI_GM6020_CanOps();
int DJI_GM6020_AttachCan(SRNOS_CANChannel ch, DJI_GM6020* obj, uint32_t period_ms);
int DJI_GM6020_DetachCan(SRNOS_CANChannel ch, DJI_GM6020* obj);

} // namespace SRNOS::Devices

#endif // SRNOS_MOTOR_GM6020_HPP
