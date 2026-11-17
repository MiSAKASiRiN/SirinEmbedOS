#include "../state_guard.hpp"
#include "SirinEmbedOS/Devices/Motors/DM4340/DM4340.hpp"
#include <cstring>

namespace SRNOS::Devices {

namespace {
  constexpr uint8_t DM_CMD_ENABLE = 0xFCu;
  constexpr uint8_t DM_CMD_DISABLE = 0xFDu;

  static inline uint32_t f32_to_u32(float v)
  {
    static_assert(sizeof(float) == 4, "float must be 32-bit IEEE754");
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
  }

  static inline float u32_to_f32(uint32_t u)
  {
    static_assert(sizeof(float) == 4, "float must be 32-bit IEEE754");
    float v;
    std::memcpy(&v, &u, 4);
    return v;
  }

  static inline void wr_le_f32(uint8_t* p, float v)
  {
    uint32_t u = f32_to_u32(v);
    p[0] = static_cast<uint8_t>(u >> 0);
    p[1] = static_cast<uint8_t>(u >> 8);
    p[2] = static_cast<uint8_t>(u >> 16);
    p[3] = static_cast<uint8_t>(u >> 24);
  }

  static inline uint16_t float_to_uint(float x, float x_min, float x_max, uint8_t bits)
  {
    const float span = x_max - x_min;
    float offset = x - x_min;
    if (offset < 0.0f) offset = 0.0f;
    if (offset > span) offset = span;
    const uint32_t levels = (1u << bits) - 1u;
    uint32_t ret = static_cast<uint32_t>(offset * static_cast<float>(levels) / span + 0.5f);
    if (ret > levels) ret = levels;
    return static_cast<uint16_t>(ret);
  }

  static inline float uint_to_float(uint16_t x_int, float x_min, float x_max, uint8_t bits)
  {
    const float span = x_max - x_min;
    const float levels = static_cast<float>((1u << bits) - 1u);
    return (static_cast<float>(x_int) * span / levels) + x_min;
  }

  static inline bool is_dm_mode_cmd(uint8_t cmd) {
    return cmd == DM_CMD_ENABLE || cmd == DM_CMD_DISABLE;
  }

  static inline uint16_t dm4340_mode_base(DM4340Mode mode) {
    switch (mode) {
      case DM4340Mode::MIT:          return 0x000u;
      case DM4340Mode::PosVel:       return 0x100u;
      case DM4340Mode::Velocity:     return 0x200u;
      case DM4340Mode::PosVelTorque: return 0x300u;
      default:                       return 0x000u;
    }
  }
} // namespace

DM4340::DM4340(uint8_t SRNOS_CANId, uint16_t fb_master_sid)
: SRNOS_CANId(SRNOS_CANId), fb_master_sid_(fb_master_sid)
{
  // Conservative defaults; must match motor-side PMAX/VMAX/TMAX for correct scaling.
  set_mit_limits(3.2f, 45.0f, 18.0f);
}

void DM4340::request_enable()
{
  pending_cmd_.store(DM_CMD_ENABLE, std::memory_order_release);
}

void DM4340::request_disable()
{
  pending_cmd_.store(DM_CMD_DISABLE, std::memory_order_release);
}

void DM4340::set_mit_limits(float pmax_rad, float vmax_rad_s, float tmax_nm)
{
  const Detail::StateGuard guard;
  pmax_u32_.store(f32_to_u32(static_cast<float>(pmax_rad)), std::memory_order_relaxed);
  vmax_u32_.store(f32_to_u32(static_cast<float>(vmax_rad_s)), std::memory_order_relaxed);
  tmax_u32_.store(f32_to_u32(static_cast<float>(tmax_nm)), std::memory_order_relaxed);
}

void DM4340::set_mit_cmd(float pos_rad, float vel_rad_s, float kp, float kd, float torque_nm)
{
  const Detail::StateGuard guard;
  cmd_mit_pos_u32_.store(f32_to_u32(static_cast<float>(pos_rad)), std::memory_order_relaxed);
  cmd_mit_vel_u32_.store(f32_to_u32(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
  cmd_mit_kp_u32_.store(f32_to_u32(static_cast<float>(kp)), std::memory_order_relaxed);
  cmd_mit_kd_u32_.store(f32_to_u32(static_cast<float>(kd)), std::memory_order_relaxed);
  cmd_mit_torque_u32_.store(f32_to_u32(static_cast<float>(torque_nm)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_mit_pos_rad(float pos_rad)
{
  const Detail::StateGuard guard;
  cmd_mit_pos_u32_.store(f32_to_u32(static_cast<float>(pos_rad)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_mit_vel_rad_s(float vel_rad_s)
{
  const Detail::StateGuard guard;
  cmd_mit_vel_u32_.store(f32_to_u32(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_mit_kp(float kp)
{
  const Detail::StateGuard guard;
  cmd_mit_kp_u32_.store(f32_to_u32(static_cast<float>(kp)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_mit_kd(float kd)
{
  const Detail::StateGuard guard;
  cmd_mit_kd_u32_.store(f32_to_u32(static_cast<float>(kd)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_mit_torque_nm(float torque_nm)
{
  const Detail::StateGuard guard;
  cmd_mit_torque_u32_.store(f32_to_u32(static_cast<float>(torque_nm)), std::memory_order_relaxed);
  mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4340::set_posvel_cmd(float pos_rad, float vel_rad_s)
{
  const Detail::StateGuard guard;
  cmd_posvel_pos_u32_.store(f32_to_u32(static_cast<float>(pos_rad)), std::memory_order_relaxed);
  cmd_posvel_vel_u32_.store(f32_to_u32(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
}

void DM4340::set_velocity_cmd(float vel_rad_s)
{
  cmd_vel_u32_.store(f32_to_u32(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
}

void DM4340::set_posvel_torque_cmd(float pos_rad, float vlim_rad_s, float ilimit_norm)
{
  // manual: v_des scaled by 100, range 0..10000 => 0..100 rad/s
  float v = static_cast<float>(vlim_rad_s) * 100.0f;
  if (v < 0.0f) v = 0.0f;
  if (v > 10000.0f) v = 10000.0f;
  uint16_t v_u16 = static_cast<uint16_t>(v + 0.5f);

  // manual: i_des scaled by 10000, range 0..10000 => 0..1.0 (normalized)
  float i = static_cast<float>(ilimit_norm) * 10000.0f;
  if (i < 0.0f) i = 0.0f;
  if (i > 10000.0f) i = 10000.0f;
  uint16_t i_u16 = static_cast<uint16_t>(i + 0.5f);

  const Detail::StateGuard guard;
  cmd_pvt_pos_u32_.store(f32_to_u32(static_cast<float>(pos_rad)), std::memory_order_relaxed);
  cmd_pvt_vlim_u16_.store(v_u16, std::memory_order_relaxed);
  cmd_pvt_ilim_u16_.store(i_u16, std::memory_order_relaxed);
}

void DM4340::onRxFeedback(const CANFrame& f, uint32_t now_ms)
{
  if (f.is_ext || f.dlc < 8) {
    return;
  }

  const uint16_t sid = static_cast<uint16_t>(f.id & 0x7FFu);
  const bool sid_ok =
      (sid == fb_master_sid_) ||
      (sid == static_cast<uint16_t>(0x000u + SRNOS_CANId)) ||
      (sid == static_cast<uint16_t>(0x100u + SRNOS_CANId)) ||
      (sid == static_cast<uint16_t>(0x200u + SRNOS_CANId)) ||
      (sid == static_cast<uint16_t>(0x300u + SRNOS_CANId));
  if (!sid_ok) {
    return;
  }

  const uint8_t* d = f.data;
  const uint8_t id_in = payload_id(d[0]);
  const uint8_t err   = payload_err(d[0]);

  if (id_in != (SRNOS_CANId & 0x0F) && id_in != SRNOS_CANId) {
    return;
  }

  DM4340State s{};

  const int16_t pos_q16 = static_cast<int16_t>(
    (static_cast<uint16_t>(d[1]) << 8) | d[2]
  );
  const int16_t vel_q12 = dm_signext12(
    (static_cast<uint16_t>(d[3]) << 4) | (d[4] >> 4)
  );
  const int16_t tq_q12  = dm_signext12(
    (static_cast<uint16_t>(d[4] & 0x0F) << 8) | d[5]
  );

  uint32_t pmax_bits, vmax_bits, tmax_bits;
  {
    const Detail::StateGuard guard;
    pmax_bits = pmax_u32_.load(std::memory_order_relaxed);
    vmax_bits = vmax_u32_.load(std::memory_order_relaxed);
    tmax_bits = tmax_u32_.load(std::memory_order_relaxed);
  }
  const float pmax = u32_to_f32(pmax_bits);
  const float vmax = u32_to_f32(vmax_bits);
  const float tmax = u32_to_f32(tmax_bits);

  const uint16_t pos_u16 = static_cast<uint16_t>(pos_q16);
  const uint16_t vel_u12 = static_cast<uint16_t>(vel_q12) & 0x0FFFu;
  const uint16_t tq_u12  = static_cast<uint16_t>(tq_q12) & 0x0FFFu;

  s.motor_id      = id_in;
  s.error_code    = err;
  // For MIT protocol, motor uses unsigned mapping over [min,max]. Keep "raw" as signed-centered for debug/API.
  s.pos_raw       = static_cast<int16_t>(static_cast<int32_t>(pos_u16) - 32768);
  s.pos_raw_total = s.pos_raw;
  s.pos_rad       = static_cast<float>(uint_to_float(pos_u16, -pmax, +pmax, 16));
  s.pos_rad_total = s.pos_rad;
  s.speed_raw     = static_cast<int16_t>(static_cast<int32_t>(vel_u12) - 2048);
  s.speed_rad_s   = static_cast<float>(uint_to_float(vel_u12, -vmax, +vmax, 12));
  s.torque_raw    = static_cast<int16_t>(static_cast<int32_t>(tq_u12) - 2048);
  s.torque_nm     = static_cast<float>(uint_to_float(tq_u12, -tmax, +tmax, 12));
  s.temp_mos_C    = d[6];
  s.temp_rotor_C  = d[7];
  s.last_rx_ms    = now_ms;
  s.online        = true;

  const Detail::StateGuard guard;
  state_ = s;
}

void DM4340::tick(uint32_t now_ms) {
    const Detail::StateGuard guard;
    if (state_.online && Detail::feedbackExpired(now_ms, state_.last_rx_ms,
                                                offline_ms_.load(std::memory_order_relaxed))) {
        state_.online = false;
    }
}

bool DM4340::snapshot(DM4340State& out) const {
    const Detail::StateGuard guard;
    out = state_;
    return true;
}

bool DM4340::exportTxRaw8(uint16_t* sid, uint8_t out[8]) const
{
  if (!sid || !out) return false;

  struct Command {
    DM4340Mode mode;
    uint32_t pmax, vmax, tmax, pos, vel, kp, kd, torque;
    uint32_t posvel_pos, posvel_vel, velocity, pvt_pos;
    uint16_t pvt_vlim, pvt_ilim;
  } command;
  {
    const Detail::StateGuard guard;
    command = {mode(),
      pmax_u32_.load(std::memory_order_relaxed),
      vmax_u32_.load(std::memory_order_relaxed),
      tmax_u32_.load(std::memory_order_relaxed),
      cmd_mit_pos_u32_.load(std::memory_order_relaxed),
      cmd_mit_vel_u32_.load(std::memory_order_relaxed),
      cmd_mit_kp_u32_.load(std::memory_order_relaxed),
      cmd_mit_kd_u32_.load(std::memory_order_relaxed),
      cmd_mit_torque_u32_.load(std::memory_order_relaxed),
      cmd_posvel_pos_u32_.load(std::memory_order_relaxed),
      cmd_posvel_vel_u32_.load(std::memory_order_relaxed),
      cmd_vel_u32_.load(std::memory_order_relaxed),
      cmd_pvt_pos_u32_.load(std::memory_order_relaxed),
      cmd_pvt_vlim_u16_.load(std::memory_order_relaxed),
      cmd_pvt_ilim_u16_.load(std::memory_order_relaxed)};
  }
  const DM4340Mode m = command.mode;
  uint16_t base = 0x000u;
  switch (m) {
    case DM4340Mode::MIT:          base = 0x000u; break;
    case DM4340Mode::PosVel:       base = 0x100u; break;
    case DM4340Mode::Velocity:     base = 0x200u; break;
    case DM4340Mode::PosVelTorque: base = 0x300u; break;
    default:                       base = 0x000u; break;
  }
  *sid = static_cast<uint16_t>(base + SRNOS_CANId);

  for (int i = 0; i < 8; ++i) out[i] = 0;

  if (m == DM4340Mode::MIT) {
    const float pmax = u32_to_f32(command.pmax);
    const float vmax = u32_to_f32(command.vmax);
    const float tmax = u32_to_f32(command.tmax);

    const float p = u32_to_f32(command.pos);
    const float v = u32_to_f32(command.vel);
    const float kp = u32_to_f32(command.kp);
    const float kd = u32_to_f32(command.kd);
    const float tq = u32_to_f32(command.torque);

    const uint16_t p_u16 = float_to_uint(p, -pmax, +pmax, 16);
    const uint16_t v_u12 = float_to_uint(v, -vmax, +vmax, 12);
    const uint16_t kp_u = float_to_uint(kp, 0.0f, 500.0f, 12);
    const uint16_t kd_u = float_to_uint(kd, 0.0f, 5.0f, 12);
    const uint16_t t_u12 = float_to_uint(tq, -tmax, +tmax, 12);

    // Pack 8B according to the MIT protocol (same 8B layout as DM4310 adaptor).
    // NOTE: The motor ID is carried by CAN SID (0x000 + SRNOS_CANId), not in the data bytes.
    out[0] = static_cast<uint8_t>(p_u16 >> 8);
    out[1] = static_cast<uint8_t>(p_u16 & 0xFFu);
    out[2] = static_cast<uint8_t>(v_u12 >> 4);
    out[3] = static_cast<uint8_t>(((v_u12 & 0x0Fu) << 4) | ((kp_u >> 8) & 0x0Fu));
    out[4] = static_cast<uint8_t>(kp_u & 0xFFu);
    out[5] = static_cast<uint8_t>((kd_u >> 4) & 0xFFu);
    out[6] = static_cast<uint8_t>(((kd_u & 0x0Fu) << 4) | ((t_u12 >> 8) & 0x0Fu));
    out[7] = static_cast<uint8_t>(t_u12 & 0xFFu);
    return true;
  }

  if (m == DM4340Mode::PosVel) {
    wr_le_f32(&out[0], u32_to_f32(command.posvel_pos));
    wr_le_f32(&out[4], u32_to_f32(command.posvel_vel));
    return true;
  }

  if (m == DM4340Mode::Velocity) {
    wr_le_f32(&out[0], u32_to_f32(command.velocity));
    return true;
  }

  if (m == DM4340Mode::PosVelTorque) {
    wr_le_f32(&out[0], u32_to_f32(command.pvt_pos));
    const uint16_t v_u16 = command.pvt_vlim;
    const uint16_t i_u16 = command.pvt_ilim;
    out[4] = static_cast<uint8_t>(v_u16 & 0xFFu);
    out[5] = static_cast<uint8_t>((v_u16 >> 8) & 0xFFu);
    out[6] = static_cast<uint8_t>(i_u16 & 0xFFu);
    out[7] = static_cast<uint8_t>((i_u16 >> 8) & 0xFFu);
    return true;
  }

  return false;
}

bool DM4340::exportPendingTxRaw8(uint16_t* sid, uint8_t out[8], uint8_t* cmd_token) const
{
  if (!sid || !out || !cmd_token) {
    return false;
  }

  uint8_t cmd;
  DM4340Mode selected_mode;
  {
      const Detail::StateGuard guard;
      cmd = pending_cmd_.load(std::memory_order_acquire);
      selected_mode = mode();
  }
  if (!is_dm_mode_cmd(cmd)) {
    return false;
  }

  *sid = static_cast<uint16_t>(dm4340_mode_base(selected_mode) + SRNOS_CANId);
  *cmd_token = cmd;
  for (uint8_t i = 0; i < 7u; ++i) {
    out[i] = 0xFFu;
  }
  out[7] = cmd;
  return true;
}

void DM4340::commitPendingTx(uint8_t cmd_token) const
{
  if (!is_dm_mode_cmd(cmd_token)) {
    return;
  }
  uint8_t expected = cmd_token;
  (void)pending_cmd_.compare_exchange_strong(expected,
                                             0u,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
}

namespace {
CANFrame to_adp_frame(const SRNOS_CANFrame* frame) {
  CANFrame out{};
  if (!frame) return out;
  out.id = frame->id;
  out.dlc = frame->dlc;
  out.is_ext = (frame->flags & SRNOS_CAN_FRAME_FLAG_EXT) != 0u;
  std::memcpy(out.data, frame->data, sizeof(out.data));
  return out;
}

size_t dm4340_rx_filter_count(void* obj) {
  return obj ? 1u : 0u;
}

bool dm4340_rx_filter_at(void* obj, size_t index, SRNOS_CANRxFilter* out) {
  if (!obj || !out || index != 0u) return false;
  auto* motor = static_cast<DM4340*>(obj);
  out->id = motor->fbMasterSid();
  out->flags = 0u;
  return true;
}

void dm4340_on_rx(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
  if (!obj || !frame) return;
  static_cast<DM4340*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t dm4340_poll_tx(void* obj,
                      SRNOS_CANTxKind kind,
                      SRNOS_CANTxItem* out,
                      size_t max,
                      uint32_t now_ms) {
  (void)now_ms;
  if (!obj || !out || max == 0u) return 0u;

  auto* motor = static_cast<DM4340*>(obj);
  uint16_t sid = 0u;
  uint8_t payload[8] = {0};
  uint8_t token = 0u;

  if (kind == SRNOS_CAN_TX_KIND_URGENT) {
    if (!motor->exportPendingTxRaw8(&sid, payload, &token)) return 0u;
  } else if (kind == SRNOS_CAN_TX_KIND_PERIODIC) {
    if (!motor->exportTxRaw8(&sid, payload)) return 0u;
  } else {
    return 0u;
  }

  std::memset(&out[0], 0, sizeof(out[0]));
  out[0].format = SRNOS_CAN_TX_FORMAT_RAW_FRAME;
  out[0].token = token;
  out[0].frame.id = sid;
  out[0].frame.dlc = 8u;
  out[0].frame.flags = 0u;
  for (size_t i = 0u; i < sizeof(out[0].frame.data); ++i) {
    out[0].frame.data[i] = payload[i];
  }
  return 1u;
}

void dm4340_commit_tx(void* obj,
                      SRNOS_CANTxKind kind,
                      const SRNOS_CANTxItem* item,
                      SRNOS_CANTxResult result) {
  if (!obj || !item) return;
  if (kind == SRNOS_CAN_TX_KIND_URGENT && result == SRNOS_CAN_TX_RESULT_ACCEPTED) {
    static_cast<DM4340*>(obj)->commitPendingTx(static_cast<uint8_t>(item->token));
  }
}

void dm4340_tick(void* obj, uint32_t now_ms) {
  if (!obj) return;
  static_cast<DM4340*>(obj)->tick(now_ms);
}

const SRNOS_CANAdapterOps s_dm4340_can_ops = {
    dm4340_rx_filter_count,
    dm4340_rx_filter_at,
    dm4340_on_rx,
    dm4340_poll_tx,
    dm4340_commit_tx,
    dm4340_tick,
};
} // namespace

const SRNOS_CANAdapterOps* DM4340_CanOps() {
  return &s_dm4340_can_ops;
}

int DM4340_AttachCan(SRNOS_CANChannel ch, DM4340* obj, uint32_t period_ms) {
  return SRNOS_CANRouterAttachAdapter(ch, obj, DM4340_CanOps(), period_ms);
}

int DM4340_DetachCan(SRNOS_CANChannel ch, DM4340* obj) {
  return SRNOS_CANRouterDetachAdapter(ch, obj);
}

} // namespace SRNOS::Devices
