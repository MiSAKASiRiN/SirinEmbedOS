// === GM6020.cpp ===

#include "../state_guard.hpp"
#include "SirinEmbedOS/Devices/Motors/GM6020/GM6020.hpp"
#include "SirinEmbedOS/Core/Endian.hpp"
#include <cstring>
#include <cstddef>

namespace SRNOS::Devices {

// ---- 单一事实来源：映射规则 ----
static inline uint8_t clamp_id_1_7(uint8_t id) { return (id < 1) ? 1 : (id > 7 ? 7 : id); }

// GM6020 反馈：0x205..0x20B（按常见惯例：0x205 + (id-1)）
static inline uint32_t gm6020_map_rx_id(uint8_t motor_id) {
    return 0x205u + static_cast<uint32_t>(motor_id - 1);
}

// GM6020 发送聚合：1..4 -> 0x1FF(slot 0..3)，5..7 -> 0x2FF(slot 0..2; slot3 置 0)
static inline bool gm6020_map_group_slot(uint8_t motor_id, uint16_t& gid, uint8_t& slot) {
    if (motor_id >= 1 && motor_id <= 4)  { gid = 0x1FF; slot = static_cast<uint8_t>(motor_id - 1); return true; }
    if (motor_id >= 5 && motor_id <= 7)  { gid = 0x2FF; slot = static_cast<uint8_t>(motor_id - 5); return true; }
    // 如以后放开到 8：gid=0x2FF, slot=3 也能兼容
    return false;
}

// ---- 构造 ----
DJI_GM6020::DJI_GM6020(uint8_t motor_id, uint32_t rx_id)
    : motor_id_(clamp_id_1_7(motor_id)),
      group_id_([&](){ uint16_t g; uint8_t s; gm6020_map_group_slot(clamp_id_1_7(motor_id), g, s); return static_cast<uint32_t>(g); }()),
      rx_id_(rx_id ? rx_id : gm6020_map_rx_id(clamp_id_1_7(motor_id)))
{}

// ---- 控制值写入（atomic）----
void DJI_GM6020::set_give_cmd(int16_t cmd_raw) {
    if (cmd_raw >  30000) cmd_raw =  30000;
    if (cmd_raw < -30000) cmd_raw = -30000;
    give_cmd_raw_.store(cmd_raw, std::memory_order_relaxed);
}

// ---- 反馈解析与有界发布 ----
void DJI_GM6020::onRxFeedback(const CANFrame& f, uint32_t now_ms) {
    if (f.is_ext || f.dlc < 8 || f.id != rx_id_) return;

    const SRNOS::Memory::ByteSpan feedback(f.data);
    uint16_t angle_raw = 0u, speed_raw = 0u, iq_raw = 0u;
    if (!SRNOS::Endian::loadBE16(feedback, 0u, angle_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 2u, speed_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 4u, iq_raw)) return;
    GM6020State next{};
    next.mech_angle_raw = angle_raw & 0x1FFFu;
    next.speed_raw = static_cast<int16_t>(speed_raw);
    next.iq_raw = static_cast<int16_t>(iq_raw);
    next.temp_C = f.data[6];
    next.mech_angle_deg = (static_cast<float>(next.mech_angle_raw) * 360.0f) / 8192.0f;
    next.last_rx_ms = now_ms;
    next.online = true;

    const Detail::StateGuard guard;
    next.prev_angle_raw = state_.mech_angle_raw;
    next.prev_speed_raw = state_.speed_raw;
    next.prev_iq_raw = state_.iq_raw;
    state_ = next;
}

void DJI_GM6020::tick(uint32_t now_ms) {
    const Detail::StateGuard guard;
    if (state_.online && Detail::feedbackExpired(now_ms, state_.last_rx_ms,
                                                offline_ms_.load(std::memory_order_relaxed))) {
        state_.online = false;
    }
}

bool DJI_GM6020::snapshot(GM6020State& out) const {
    const Detail::StateGuard guard;
    out = state_;
    return true;
}

bool DJI_GM6020::exportTx16(uint16_t* group_id, uint8_t* slot, int16_t* val) const {
    if (!group_id || !slot || !val) return false;
    if (!gm6020_map_group_slot(motor_id_, *group_id, *slot)) return false;
    *val = give_cmd_raw_.load(std::memory_order_relaxed);
    return true;
}

// ---- 清帧工具 ----
static void clear_frame(CANFrame& f, uint32_t id) {
    f.id = id;
    f.dlc = 8;
    f.is_ext = false;
    std::memset(f.data, 0, sizeof(f.data));
}

// ---- 批量组帧（最小系统/Bring-up 用）----
void GM6020_ComposeTxFrames(DJI_GM6020* const motors[],
                            std::size_t n,
                            CANFrame out[],
                            std::size_t& out_count)
{
    int16_t cur_1_4[4] = {0,0,0,0}; // group 0x1FF
    int16_t cur_5_8[4] = {0,0,0,0}; // group 0x2FF（我们用到 5..7，slot3 留空=0）
    bool have_1FF = false, have_2FF = false;

    for (std::size_t i = 0; i < n; ++i) {
        DJI_GM6020* m = motors[i];
        if (!m) continue;

        uint16_t gid; uint8_t slot; int16_t v;
        if (!m->exportTx16(&gid, &slot, &v)) continue;

        if (gid == 0x1FF) { cur_1_4[slot] = v; have_1FF = true; }
        else              { cur_5_8[slot] = v; have_2FF = true; }
    }

    std::size_t idx = 0;

    if (have_1FF) {
        clear_frame(out[idx], 0x1FFu);
        SRNOS::Memory::MutableByteSpan payload(out[idx].data);
        (void)SRNOS::Endian::storeBE16(payload, 0u, static_cast<uint16_t>(cur_1_4[0]));
        (void)SRNOS::Endian::storeBE16(payload, 2u, static_cast<uint16_t>(cur_1_4[1]));
        (void)SRNOS::Endian::storeBE16(payload, 4u, static_cast<uint16_t>(cur_1_4[2]));
        (void)SRNOS::Endian::storeBE16(payload, 6u, static_cast<uint16_t>(cur_1_4[3]));
        idx++;
    }
    if (have_2FF) {
        clear_frame(out[idx], 0x2FFu);
        SRNOS::Memory::MutableByteSpan payload(out[idx].data);
        (void)SRNOS::Endian::storeBE16(payload, 0u, static_cast<uint16_t>(cur_5_8[0]));
        (void)SRNOS::Endian::storeBE16(payload, 2u, static_cast<uint16_t>(cur_5_8[1]));
        (void)SRNOS::Endian::storeBE16(payload, 4u, static_cast<uint16_t>(cur_5_8[2]));
        (void)SRNOS::Endian::storeBE16(payload, 6u, static_cast<uint16_t>(cur_5_8[3]));
        idx++;
    }

    out_count = idx;
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

size_t gm6020_rx_filter_count(void* obj) {
    return obj ? 1u : 0u;
}

bool gm6020_rx_filter_at(void* obj, size_t index, SRNOS_CANRxFilter* out) {
    if (!obj || !out || index != 0u) return false;
    auto* motor = static_cast<DJI_GM6020*>(obj);
    out->id = motor->rxId();
    out->flags = 0u;
    return true;
}

void gm6020_on_rx(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<DJI_GM6020*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t gm6020_poll_tx(void* obj,
                      SRNOS_CANTxKind kind,
                      SRNOS_CANTxItem* out,
                      size_t max,
                      uint32_t now_ms) {
    (void)now_ms;
    if (!obj || !out || max == 0u || kind != SRNOS_CAN_TX_KIND_PERIODIC) return 0u;
    auto* motor = static_cast<DJI_GM6020*>(obj);
    uint16_t group_id = 0u;
    uint8_t slot = 0u;
    int16_t value = 0;
    if (!motor->exportTx16(&group_id, &slot, &value)) return 0u;

    std::memset(&out[0], 0, sizeof(out[0]));
    out[0].format = SRNOS_CAN_TX_FORMAT_GROUP_I16;
    out[0].group_i16.group_id = group_id;
    out[0].group_i16.flags = 0u;
    out[0].group_i16.slot = slot;
    out[0].group_i16.endian = SRNOS_CAN_TX_ENDIAN_BE;
    out[0].group_i16.value = value;
    return 1u;
}

void gm6020_tick(void* obj, uint32_t now_ms) {
    if (!obj) return;
    static_cast<DJI_GM6020*>(obj)->tick(now_ms);
}

const SRNOS_CANAdapterOps s_gm6020_can_ops = {
    gm6020_rx_filter_count,
    gm6020_rx_filter_at,
    gm6020_on_rx,
    gm6020_poll_tx,
    nullptr,
    gm6020_tick,
};
} // namespace

const SRNOS_CANAdapterOps* DJI_GM6020_CanOps() {
    return &s_gm6020_can_ops;
}

int DJI_GM6020_AttachCan(SRNOS_CANChannel ch, DJI_GM6020* obj, uint32_t period_ms) {
    return SRNOS_CANRouterAttachAdapter(ch, obj, DJI_GM6020_CanOps(), period_ms);
}

int DJI_GM6020_DetachCan(SRNOS_CANChannel ch, DJI_GM6020* obj) {
    return SRNOS_CANRouterDetachAdapter(ch, obj);
}

} // namespace SRNOS::Devices
