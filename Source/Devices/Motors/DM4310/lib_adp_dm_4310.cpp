#include "SirinEmbedOS/Devices/Motors/DM4310/lib_adp_dm_4310.h"
#include <cstring>
#include <cstddef>

// uint32_t rx_1 = 0;
// uint32_t rx_2 = 0;
// uint32_t rx_3 = 0;
// uint32_t rx_4 = 0;

// ===== MIT-mode ranges and float→fixed mapping =====
namespace {
    // DM4310 MIT ranges.
    // Defaults can vary across DM firmwares/motors (values may be readable from motor config).
    // You can override these at build time via target_compile_definitions().
    #ifndef DM4310_MIT_P_MIN
    #define DM4310_MIT_P_MIN (-3.2f)
    #endif
    #ifndef DM4310_MIT_P_MAX
    #define DM4310_MIT_P_MAX (3.2f)
    #endif
    #ifndef DM4310_MIT_V_MIN
    #define DM4310_MIT_V_MIN (-45.0f)
    #endif
    #ifndef DM4310_MIT_V_MAX
    #define DM4310_MIT_V_MAX (45.0f)
    #endif
    #ifndef DM4310_MIT_KP_MIN
    #define DM4310_MIT_KP_MIN (0.0f)
    #endif
    #ifndef DM4310_MIT_KP_MAX
    #define DM4310_MIT_KP_MAX (500.0f)
    #endif
    #ifndef DM4310_MIT_KD_MIN
    #define DM4310_MIT_KD_MIN (0.0f)
    #endif
    #ifndef DM4310_MIT_KD_MAX
    #define DM4310_MIT_KD_MAX (5.0f)
    #endif
    #ifndef DM4310_MIT_T_MIN
    #define DM4310_MIT_T_MIN (-18.0f)
    #endif
    #ifndef DM4310_MIT_T_MAX
    #define DM4310_MIT_T_MAX (18.0f)
    #endif

    constexpr float P_MIN  = DM4310_MIT_P_MIN;
    constexpr float P_MAX  = DM4310_MIT_P_MAX;
    constexpr float V_MIN  = DM4310_MIT_V_MIN;
    constexpr float V_MAX  = DM4310_MIT_V_MAX;
    constexpr float KP_MIN = DM4310_MIT_KP_MIN;
    constexpr float KP_MAX = DM4310_MIT_KP_MAX;
    constexpr float KD_MIN = DM4310_MIT_KD_MIN;
    constexpr float KD_MAX = DM4310_MIT_KD_MAX;
    constexpr float T_MIN  = DM4310_MIT_T_MIN;
    constexpr float T_MAX  = DM4310_MIT_T_MAX;
    constexpr uint8_t DM_CMD_ENABLE = 0xFCu;
    constexpr uint8_t DM_CMD_DISABLE = 0xFDu;

    // Q16 raw → position mapping step (P_MIN..P_MAX)
    constexpr float POS_RAW_STEP = (P_MAX - P_MIN) / 65535.0f;

    // Map float to unsigned fixed-point, same idea as C mit_ctrl.
    static inline uint16_t float_to_uint(float x, float x_min, float x_max, uint8_t bits)
    {
        const float span   = x_max - x_min;
        float       offset = x - x_min;
        if (offset < 0.0f) offset = 0.0f;
        if (offset > span) offset = span;

        const uint32_t levels = (1u << bits) - 1u;
        uint32_t ret = static_cast<uint32_t>(offset * static_cast<float>(levels) / span + 0.5f);
        if (ret > levels) ret = levels;
        return static_cast<uint16_t>(ret);
    }

    // Map unsigned fixed-point to float (matches dm4310_drv.c)
    static inline float uint_to_float(uint32_t x_int, float x_min, float x_max, uint8_t bits)
    {
        const float span = x_max - x_min;
        const float levels = static_cast<float>((1u << bits) - 1u);
        return static_cast<float>(x_int) * span / levels + x_min;
    }

    // Little-endian float32 writer (PosVel / Velocity modes)
    static inline void wr_le_f32(uint8_t* p, float v) {
        static_assert(sizeof(float) == 4, "float must be 32-bit IEEE754");
        uint32_t u;
        std::memcpy(&u, &v, 4);
        p[0] = static_cast<uint8_t>(u >> 0);
        p[1] = static_cast<uint8_t>(u >> 8);
        p[2] = static_cast<uint8_t>(u >> 16);
        p[3] = static_cast<uint8_t>(u >> 24);
    }

    static inline bool is_dm_mode_cmd(uint8_t cmd) {
        return cmd == DM_CMD_ENABLE || cmd == DM_CMD_DISABLE;
    }

    static inline uint16_t dm4310_mode_base(DM4310Mode mode) {
        switch (mode) {
            case DM4310Mode::MIT:      return 0x000u;
            case DM4310Mode::PosVel:   return 0x100u;
            case DM4310Mode::Velocity: return 0x200u;
            default:                   return 0x000u;
        }
    }
} // namespace

DM4310::DM4310(uint8_t can_id, uint16_t fb_master_sid)
: can_id_(can_id), fb_master_sid_(fb_master_sid) {}

void DM4310::request_enable()
{
    pending_cmd_.store(DM_CMD_ENABLE, std::memory_order_release);
}

void DM4310::request_disable()
{
    pending_cmd_.store(DM_CMD_DISABLE, std::memory_order_release);
}

void DM4310::set_mit_cmd(float pos_rad, float vel_rad_s, float kp, float kd, float torque_nm)
{
    cmd_mit_pos_u32_.store(f2u(static_cast<float>(pos_rad)), std::memory_order_relaxed);
    cmd_mit_vel_u32_.store(f2u(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
    cmd_mit_kp_u32_.store(f2u(static_cast<float>(kp)), std::memory_order_relaxed);
    cmd_mit_kd_u32_.store(f2u(static_cast<float>(kd)), std::memory_order_relaxed);
    cmd_mit_torque_u32_.store(f2u(static_cast<float>(torque_nm)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::set_mit_pos_rad(float pos_rad)
{
    cmd_mit_pos_u32_.store(f2u(static_cast<float>(pos_rad)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::set_mit_vel_rad_s(float vel_rad_s)
{
    cmd_mit_vel_u32_.store(f2u(static_cast<float>(vel_rad_s)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::set_mit_kp(float kp)
{
    cmd_mit_kp_u32_.store(f2u(static_cast<float>(kp)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::set_mit_kd(float kd)
{
    cmd_mit_kd_u32_.store(f2u(static_cast<float>(kd)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::set_mit_torque_nm(float torque_nm)
{
    cmd_mit_torque_u32_.store(f2u(static_cast<float>(torque_nm)), std::memory_order_relaxed);
    mit_cmd_valid_.store(1u, std::memory_order_relaxed);
}

void DM4310::onRxFeedback(const CanFrame& f, uint32_t now_ms) {
    if (f.is_ext || f.dlc < 8 || f.id != fb_master_sid_) {
        return;
    }

    const uint8_t* d = f.data;
    const uint8_t id_in = dm4310_payload_id(d[0]);
    const uint8_t err   = dm4310_payload_err(d[0]);

    // If you use 8-bit ID in protocol, change this to: if (d[0] == can_id_) ...
    if (id_in != (can_id_ & 0x0F)) {
        return;
    }

    // begin write: make seq odd
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);

    DM4310State s = state_;

    // keep previous values
    s.prev_pos_raw    = s.pos_raw;
    s.prev_speed_raw  = s.speed_rpm;
    s.prev_torque_raw = s.torque_raw;
    const bool was_online = s.online;

    // POS 16b; VEL/TQ 12b (unsigned, mapped to ranges)
    const uint16_t pos_u16 = static_cast<uint16_t>(
        (static_cast<uint16_t>(d[1]) << 8) | d[2]
    );
    const uint16_t vel_u12 = static_cast<uint16_t>(
        (static_cast<uint16_t>(d[3]) << 4) | (d[4] >> 4)
    );
    const uint16_t tq_u12  = static_cast<uint16_t>(
        (static_cast<uint16_t>(d[4] & 0x0F) << 8) | d[5]
    );

    // ===== multi-turn accumulator =====
    if (!was_online) {
        // First valid frame (or re-online after timeout):
        // initialize accumulator from current single-turn value.
        s.pos_raw_total = static_cast<int32_t>(pos_u16);
    } else {
        // Compute step with wrap-around compensation.
        const uint16_t prev_pos_u16 = static_cast<uint16_t>(s.prev_pos_raw);
        int32_t delta = static_cast<int32_t>(pos_u16) -
                        static_cast<int32_t>(prev_pos_u16);

        // When 16-bit wraps through ±32768, the raw difference will be ~±65536.
        if (delta > 32767) {
            delta -= 65536;
        } else if (delta < -32768) {
            delta += 65536;
        }

        s.pos_raw_total += delta;
    }

    // fill feedback (single-turn + multi-turn)
    s.motor_id     = id_in;
    s.error_code   = err;
    s.pos_raw      = static_cast<int16_t>(pos_u16);
    s.pos_rad      = static_cast<float>(uint_to_float(pos_u16, P_MIN, P_MAX, 16));
    s.pos_rad_total= static_cast<float>(static_cast<float>(s.pos_raw_total) * POS_RAW_STEP + P_MIN);
    s.speed_rpm    = static_cast<int16_t>(vel_u12);
    s.speed_rad_s  = static_cast<float>(uint_to_float(vel_u12, V_MIN, V_MAX, 12));
    s.torque_raw   = static_cast<int16_t>(tq_u12);
    s.temp_mos_C   = d[6];
    s.temp_rotor_C = d[7];
    s.last_rx_ms   = now_ms;
    s.online       = true;

    state_ = s;

    // end write: make seq even
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
}

void DM4310::tick(uint32_t now_ms) {
    const uint32_t last = __atomic_load_n(&state_.last_rx_ms, __ATOMIC_RELAXED);
    const bool was_online = __atomic_load_n(&state_.online, __ATOMIC_RELAXED);

    if (was_online && (now_ms - last > offline_ms_)) {
        __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);
        state_.online = false;
        __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
    }
}

bool DM4310::snapshot(DM4310State& out) const {
    for (int i = 0; i < 3; ++i) {
        uint32_t s1 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 & 1u) {
            // writer in progress
            continue;
        }

        DM4310State tmp;
        std::memcpy(&tmp, &state_, sizeof(tmp));

        uint32_t s2 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 == s2) {
            out = tmp;
            return true;
        }
    }
    return false;
}

bool DM4310::exportTxRaw8(uint16_t* sid, uint8_t out[8]) const {
    if (!sid || !out) {
        return false;
    }

    const DM4310Mode m = mode();
    switch (m) {
        case DM4310Mode::MIT: {
            // ===== 1. Read command registers as "physical" values =====
            const bool mit_valid = (mit_cmd_valid_.load(std::memory_order_relaxed) != 0);
            float pos;
            float vel;
            float kp;
            float kd;
            float torq;
            if (mit_valid) {
                pos  = u2f(cmd_mit_pos_u32_.load(std::memory_order_relaxed));
                vel  = u2f(cmd_mit_vel_u32_.load(std::memory_order_relaxed));
                kp   = u2f(cmd_mit_kp_u32_.load(std::memory_order_relaxed));
                kd   = u2f(cmd_mit_kd_u32_.load(std::memory_order_relaxed));
                torq = u2f(cmd_mit_torque_u32_.load(std::memory_order_relaxed));
            } else {
                pos  = get_pos_cmd_rad();
                vel  = get_vel_cmd_lim();
                torq = static_cast<float>(cmd_torque_raw_.load(std::memory_order_relaxed));
                kp   = 0.0f;
                kd   = 0.0f;
            }

            // ===== 2. Float → fixed-point mapping =====
            const uint16_t pos_tmp = float_to_uint(pos,  P_MIN,  P_MAX,  16);
            const uint16_t vel_tmp = float_to_uint(vel,  V_MIN,  V_MAX,  12);
            const uint16_t kp_tmp  = float_to_uint(kp,   KP_MIN, KP_MAX, 12);
            const uint16_t kd_tmp  = float_to_uint(kd,   KD_MIN, KD_MAX, 12);
            const uint16_t tor_tmp = float_to_uint(torq, T_MIN,  T_MAX,  12);

            // ===== 3. Pack 8B according to MIT protocol =====
            out[0] = static_cast<uint8_t>(pos_tmp >> 8);
            out[1] = static_cast<uint8_t>(pos_tmp & 0xFFu);
            out[2] = static_cast<uint8_t>(vel_tmp >> 4);
            out[3] = static_cast<uint8_t>(((vel_tmp & 0x0Fu) << 4) | (kp_tmp >> 8));
            out[4] = static_cast<uint8_t>(kp_tmp & 0xFFu);
            out[5] = static_cast<uint8_t>(kd_tmp >> 4);
            out[6] = static_cast<uint8_t>(((kd_tmp & 0x0Fu) << 4) | (tor_tmp >> 8));
            out[7] = static_cast<uint8_t>(tor_tmp & 0xFFu);

            *sid = static_cast<uint16_t>(0x000u + can_id_);
            return true;
        }

        case DM4310Mode::PosVel: {
            // position + max velocity (both LE float)
            // Modified to use get_pos_cmd_rad() / get_vel_cmd_lim() as requested
            float p_rad = get_pos_cmd_rad();
            float v_lim = get_vel_cmd_lim();

            wr_le_f32(&out[0], p_rad);
            wr_le_f32(&out[4], v_lim);
            *sid = static_cast<uint16_t>(0x100u + can_id_);
            return true;
        }

        case DM4310Mode::Velocity: {
            // Velocity mode usually takes velocity in D[0]-D[3]
            float v = get_vel_cmd_lim();
            wr_le_f32(&out[0], v);
            out[4] = out[5] = out[6] = out[7] = 0;
            *sid = static_cast<uint16_t>(0x200u + can_id_);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool DM4310::exportPendingTxRaw8(uint16_t* sid, uint8_t out[8], uint8_t* cmd_token) const
{
    if (!sid || !out || !cmd_token) {
        return false;
    }

    const uint8_t cmd = pending_cmd_.load(std::memory_order_acquire);
    if (!is_dm_mode_cmd(cmd)) {
        return false;
    }

    *sid = static_cast<uint16_t>(dm4310_mode_base(mode()) + can_id_);
    *cmd_token = cmd;
    for (uint8_t i = 0; i < 7u; ++i) {
        out[i] = 0xFFu;
    }
    out[7] = cmd;
    return true;
}

void DM4310::commitPendingTx(uint8_t cmd_token) const
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

void DM4310_CollectTxFrames(DM4310* const motors[],
                            std::size_t n,
                            CanFrame out[],
                            std::size_t& out_count)
{
    std::size_t idx = 0;
    for (std::size_t i = 0; i < n; ++i) {
        DM4310* m = motors[i];
        if (!m) {
            continue;
        }

        uint16_t sid;
        uint8_t payload[8];
        if (!m->exportTxRaw8(&sid, payload)) {
            continue;
        }

        CanFrame f{};
        f.id     = sid;
        f.is_ext = false;
        f.dlc    = 8;
        std::memcpy(f.data, payload, 8);

        out[idx++] = f;
    }
    out_count = idx;
}

namespace {
CanFrame to_adp_frame(const can_frame_t* frame) {
    CanFrame out{};
    if (!frame) return out;
    out.id = frame->id;
    out.dlc = frame->dlc;
    out.is_ext = (frame->flags & CAN_FRAME_FLAG_EXT) != 0u;
    std::memcpy(out.data, frame->data, sizeof(out.data));
    return out;
}

size_t dm4310_rx_filter_count(void* obj) {
    return obj ? 1u : 0u;
}

bool dm4310_rx_filter_at(void* obj, size_t index, can_rx_filter_t* out) {
    if (!obj || !out || index != 0u) return false;
    auto* motor = static_cast<DM4310*>(obj);
    out->id = motor->fbMasterSid();
    out->flags = 0u;
    return true;
}

void dm4310_on_rx(void* obj, const can_frame_t* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<DM4310*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t dm4310_poll_tx(void* obj,
                      can_tx_kind_t kind,
                      can_tx_item_t* out,
                      size_t max,
                      uint32_t now_ms) {
    (void)now_ms;
    if (!obj || !out || max == 0u) return 0u;

    auto* motor = static_cast<DM4310*>(obj);
    uint16_t sid = 0u;
    uint8_t payload[8] = {0};
    uint8_t token = 0u;

    if (kind == CAN_TX_KIND_URGENT) {
        if (!motor->exportPendingTxRaw8(&sid, payload, &token)) return 0u;
    } else if (kind == CAN_TX_KIND_PERIODIC) {
        if (!motor->exportTxRaw8(&sid, payload)) return 0u;
    } else {
        return 0u;
    }

    std::memset(&out[0], 0, sizeof(out[0]));
    out[0].format = CAN_TX_FORMAT_RAW_FRAME;
    out[0].token = token;
    out[0].frame.id = sid;
    out[0].frame.dlc = 8u;
    out[0].frame.flags = 0u;
    for (size_t i = 0u; i < sizeof(out[0].frame.data); ++i) {
        out[0].frame.data[i] = payload[i];
    }
    return 1u;
}

void dm4310_commit_tx(void* obj,
                      can_tx_kind_t kind,
                      const can_tx_item_t* item,
                      can_tx_result_t result) {
    if (!obj || !item) return;
    if (kind == CAN_TX_KIND_URGENT && result == CAN_TX_RESULT_ACCEPTED) {
        static_cast<DM4310*>(obj)->commitPendingTx(static_cast<uint8_t>(item->token));
    }
}

void dm4310_tick(void* obj, uint32_t now_ms) {
    if (!obj) return;
    static_cast<DM4310*>(obj)->tick(now_ms);
}

const can_adapter_ops_t s_dm4310_can_ops = {
    dm4310_rx_filter_count,
    dm4310_rx_filter_at,
    dm4310_on_rx,
    dm4310_poll_tx,
    dm4310_commit_tx,
    dm4310_tick,
};
} // namespace

const can_adapter_ops_t* DM4310_CanOps() {
    return &s_dm4310_can_ops;
}

int DM4310_AttachCan(can_channel_t ch, DM4310* obj, uint32_t period_ms) {
    return can_router_attach_adapter(ch, obj, DM4310_CanOps(), period_ms);
}

int DM4310_DetachCan(can_channel_t ch, DM4310* obj) {
    return can_router_detach_adapter(ch, obj);
}
