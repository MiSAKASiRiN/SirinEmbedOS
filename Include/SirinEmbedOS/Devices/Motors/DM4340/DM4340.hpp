#ifndef SRNOS_MOTOR_DM4340_HPP
#define SRNOS_MOTOR_DM4340_HPP
#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>

#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Devices/CANFrame.h"

namespace SRNOS::Devices {


// DM4340 feedback snapshot (MIT protocol per DM manual)
struct DM4340State {
  uint8_t  motor_id = 0;
  uint8_t  error_code = 0;

  int16_t  pos_raw = 0;          // signed 16-bit mapped position
  int32_t  pos_raw_total = 0;    // kept for API compatibility (no multi-turn by default)
  float     pos_rad = 0.0f;       // mapped position (rad)
  float     pos_rad_total = 0.0f; // same as pos_rad unless user adds unwrap

  int16_t  speed_raw = 0;        // signed 12-bit mapped speed (stored in int16)
  float     speed_rad_s = 0.0f;   // mapped speed (rad/s)

  int16_t  torque_raw = 0;       // signed 12-bit mapped torque (stored in int16)
  float     torque_nm = 0.0f;     // mapped torque (Nm)

  uint8_t  temp_mos_C = 0;
  uint8_t  temp_rotor_C = 0;

  uint32_t last_rx_ms = 0;
  bool     online = false;
};

/**
 * @brief Control mode for DM4340 V4 driver (per DM-J4340-2EC manual)
 *
 * Mode -> CAN SID mapping:
 *   MIT          : SID = 0x000 + SRNOS_CANId
 *   PosVel       : SID = 0x100 + SRNOS_CANId
 *   Velocity     : SID = 0x200 + SRNOS_CANId
 *   PosVelTorque : SID = 0x300 + SRNOS_CANId
 */
enum class DM4340Mode : uint8_t {
  MIT          = 1,
  PosVel       = 2,
  Velocity     = 3,
  PosVelTorque = 4,
};

class DM4340 {
public:
  explicit DM4340(uint8_t SRNOS_CANId, uint16_t fb_master_sid = 0);

  uint8_t   canId()       const { return SRNOS_CANId; }
  uint16_t  fbMasterSid() const { return fb_master_sid_; }
  DM4340Mode mode()       const {
    return static_cast<DM4340Mode>(mode_.load(std::memory_order_relaxed));
  }

  void set_mode(DM4340Mode m) {
    mode_.store(static_cast<uint8_t>(m), std::memory_order_relaxed);
  }
  void request_enable();
  void request_disable();

  // Command setters/export: task/pre-scheduler or IRQ priority 5..15 only.
  // Each setter publishes all its fields together; export takes one bounded
  // snapshot including mode. Separate setter calls are separate updates.
  // MIT mapping limits are also read together by feedback decoding.
  // MIT mapping limits (must match motor-side PMAX/VMAX/TMAX)
  void set_mit_limits(float pmax_rad, float vmax_rad_s, float tmax_nm);

  // MIT-mode control (physical units)
  void set_mit_cmd(float pos_rad, float vel_rad_s, float kp, float kd, float torque_nm);
  void set_mit_pos_rad(float pos_rad);
  void set_mit_vel_rad_s(float vel_rad_s);
  void set_mit_kp(float kp);
  void set_mit_kd(float kd);
  void set_mit_torque_nm(float torque_nm);

  // PosVel / Velocity / PosVelTorque control (manual-defined encodings)
  void set_posvel_cmd(float pos_rad, float vel_rad_s);
  void set_velocity_cmd(float vel_rad_s);
  void set_posvel_torque_cmd(float pos_rad, float vlim_rad_s, float ilimit_norm);

  // feedback handler / online timeout
  void onRxFeedback(const CANFrame& f, uint32_t now_ms);
  void set_offline_timeout(uint32_t ms) { offline_ms_.store(ms, std::memory_order_relaxed); }
  void tick(uint32_t now_ms);

  // Task context or IRQ priority 5..15; bounded copy, no spin.
  bool snapshot(DM4340State& out) const;
  DM4340State state() const noexcept {
    DM4340State s{};
    (void)snapshot(s);
    return s;
  }

  bool exportTxRaw8(uint16_t* sid, uint8_t out[8]) const;
  bool exportPendingTxRaw8(uint16_t* sid, uint8_t out[8], uint8_t* cmd_token) const;
  void commitPendingTx(uint8_t cmd_token) const;

private:
  static inline int16_t dm_signext12(uint16_t q12) {
    q12 &= 0x0FFFu;
    if (q12 & 0x0800u) q12 |= 0xF000u;
    return static_cast<int16_t>(q12);
  }
  static inline uint8_t payload_id(uint8_t d0)  { return (d0 & 0x0Fu); }
  static inline uint8_t payload_err(uint8_t d0) { return (d0 >> 4); }

  const uint8_t  SRNOS_CANId;
  const uint16_t fb_master_sid_;

  std::atomic<uint8_t>  mode_{static_cast<uint8_t>(DM4340Mode::MIT)};
  std::atomic<uint32_t> cmd_mit_pos_u32_{0};
  std::atomic<uint32_t> cmd_mit_vel_u32_{0};
  std::atomic<uint32_t> cmd_mit_kp_u32_{0};
  std::atomic<uint32_t> cmd_mit_kd_u32_{0};
  std::atomic<uint32_t> cmd_mit_torque_u32_{0};
  std::atomic<uint8_t>  mit_cmd_valid_{0};

  std::atomic<uint32_t> cmd_posvel_pos_u32_{0};
  std::atomic<uint32_t> cmd_posvel_vel_u32_{0};
  std::atomic<uint32_t> cmd_vel_u32_{0};

  std::atomic<uint32_t> cmd_pvt_pos_u32_{0};
  std::atomic<uint16_t> cmd_pvt_vlim_u16_{0};
  std::atomic<uint16_t> cmd_pvt_ilim_u16_{0};
  mutable std::atomic<uint8_t> pending_cmd_{0};

  std::atomic<uint32_t> pmax_u32_{0};
  std::atomic<uint32_t> vmax_u32_{0};
  std::atomic<uint32_t> tmax_u32_{0};

  DM4340State state_{};
  std::atomic<uint32_t> offline_ms_{100};
};

const SRNOS_CANAdapterOps* DM4340_CanOps();
int DM4340_AttachCan(SRNOS_CANChannel ch, DM4340* obj, uint32_t period_ms);
int DM4340_DetachCan(SRNOS_CANChannel ch, DM4340* obj);

} // namespace SRNOS::Devices

#endif // SRNOS_MOTOR_DM4340_HPP
