// === C610.cpp ===

#include "../state_guard.hpp"
#include "SirinEmbedOS/Devices/Motors/C610/C610.hpp"
#include "SirinEmbedOS/Core/Endian.hpp"
#include <cstring>
#include <cstddef>
#include <atomic>

namespace SRNOS::Devices {

static inline uint32_t c610_map_rx_id(uint8_t motor_id) {
    return 0x200u + motor_id; // 反馈 0x201..0x208
}
static inline bool c610_map_group_slot(uint8_t motor_id, uint16_t& gid, uint8_t& slot) {
    if (motor_id >= 1 && motor_id <= 4)  { gid = 0x200; slot = static_cast<uint8_t>(motor_id - 1); return true; }
    if (motor_id >= 5 && motor_id <= 8)  { gid = 0x1FF; slot = static_cast<uint8_t>(motor_id - 5); return true; }
    return false;
}

static inline uint8_t clamp_id_1_8(uint8_t id) { return (id < 1) ? 1 : (id > 8 ? 8 : id); }

DJI_C610::DJI_C610(uint8_t motor_id, uint32_t rx_id)
    : motor_id_(clamp_id_1_8(motor_id)),
      group_id_([&](){ uint16_t g; uint8_t s; c610_map_group_slot(clamp_id_1_8(motor_id), g, s); return g; }()),
      rx_id_(rx_id ? rx_id : c610_map_rx_id(clamp_id_1_8(motor_id)))
{}

void DJI_C610::set_give_cmd(int16_t cmd_raw) {
    if (cmd_raw >  10000) cmd_raw =  10000;
    if (cmd_raw < -10000) cmd_raw = -10000;
    give_cmd_raw_.store(cmd_raw, std::memory_order_relaxed);
    dbg_cmd_shadow_ = cmd_raw;
}

void DJI_C610::onRxFeedback(const CANFrame& f, uint32_t now_ms) {
    if (f.is_ext || f.dlc < 6 || f.id != rx_id_) return;

    const SRNOS::Memory::ByteSpan feedback(f.data);
    uint16_t angle_raw = 0u, speed_raw = 0u, iq_raw = 0u;
    if (!SRNOS::Endian::loadBE16(feedback, 0u, angle_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 2u, speed_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 4u, iq_raw)) return;
    C610State next{};
    next.mech_angle_raw = angle_raw & 0x1FFFu;
    next.speed_raw = static_cast<int16_t>(speed_raw);
    next.iq_raw = static_cast<int16_t>(iq_raw);
    next.temp_C = 0;
    next.mech_angle_deg = (static_cast<float>(next.mech_angle_raw) * 360.0f) / 8192.0f;
    next.last_rx_ms = now_ms;
    next.online = true;

    const Detail::StateGuard guard;
    next.prev_angle_raw = state_.mech_angle_raw;
    next.prev_speed_raw = state_.speed_raw;
    next.prev_iq_raw = state_.iq_raw;
    state_ = next;
}

void DJI_C610::tick(uint32_t now_ms) {
    const Detail::StateGuard guard;
    if (state_.online && Detail::feedbackExpired(now_ms, state_.last_rx_ms,
                                                offline_ms_.load(std::memory_order_relaxed))) {
        state_.online = false;
    }
}

bool DJI_C610::snapshot(C610State& out) const {
    const Detail::StateGuard guard;
    out = state_;
    return true;
}

bool DJI_C610::exportTx16(uint16_t* group_id, uint8_t* slot, int16_t* val) const {
    if (!group_id || !slot || !val) return false;
    if (!c610_map_group_slot(motor_id_, *group_id, *slot)) return false;
    *val = give_cmd_raw_.load(std::memory_order_relaxed);
    return true;
}

static void clear_frame(CANFrame& f, uint32_t id) {
    f.id = id;
    f.dlc = 8;
    f.is_ext = false;
    std::memset(f.data, 0, sizeof(f.data));
}

void C610_ComposeTxFrames(DJI_C610* const motors[],
                          std::size_t n,
                          CANFrame out[],
                          std::size_t& out_count)
{
    int16_t cur_1_4[4] = {0,0,0,0};
    int16_t cur_5_8[4] = {0,0,0,0};
    bool have_200 = false, have_1FF = false;

    for (std::size_t i = 0; i < n; ++i) {
        DJI_C610* m = motors[i];
        if (!m) continue;

        uint16_t gid; uint8_t slot; int16_t v;
        if (!m->exportTx16(&gid, &slot, &v)) continue;

        if (gid == 0x200) { cur_1_4[slot] = v; have_200 = true; }
        else              { cur_5_8[slot] = v; have_1FF = true; }
    }

    std::size_t idx = 0;

    if (have_200) {
        clear_frame(out[idx], 0x200u);
        SRNOS::Memory::MutableByteSpan payload(out[idx].data);
        (void)SRNOS::Endian::storeBE16(payload, 0u, static_cast<uint16_t>(cur_1_4[0]));
        (void)SRNOS::Endian::storeBE16(payload, 2u, static_cast<uint16_t>(cur_1_4[1]));
        (void)SRNOS::Endian::storeBE16(payload, 4u, static_cast<uint16_t>(cur_1_4[2]));
        (void)SRNOS::Endian::storeBE16(payload, 6u, static_cast<uint16_t>(cur_1_4[3]));
        idx++;
    }
    if (have_1FF) {
        clear_frame(out[idx], 0x1FFu);
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

size_t c610_rx_filter_count(void* obj) {
    return obj ? 1u : 0u;
}

bool c610_rx_filter_at(void* obj, size_t index, SRNOS_CANRxFilter* out) {
    if (!obj || !out || index != 0u) return false;
    auto* motor = static_cast<DJI_C610*>(obj);
    out->id = motor->rxId();
    out->flags = 0u;
    return true;
}

void c610_on_rx(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<DJI_C610*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t c610_poll_tx(void* obj,
                    SRNOS_CANTxKind kind,
                    SRNOS_CANTxItem* out,
                    size_t max,
                    uint32_t now_ms) {
    (void)now_ms;
    if (!obj || !out || max == 0u || kind != SRNOS_CAN_TX_KIND_PERIODIC) return 0u;
    auto* motor = static_cast<DJI_C610*>(obj);
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

void c610_tick(void* obj, uint32_t now_ms) {
    if (!obj) return;
    static_cast<DJI_C610*>(obj)->tick(now_ms);
}

const SRNOS_CANAdapterOps s_c610_can_ops = {
    c610_rx_filter_count,
    c610_rx_filter_at,
    c610_on_rx,
    c610_poll_tx,
    nullptr,
    c610_tick,
};
} // namespace

const SRNOS_CANAdapterOps* DJI_C610_CanOps() {
    return &s_c610_can_ops;
}

int DJI_C610_AttachCan(SRNOS_CANChannel ch, DJI_C610* obj, uint32_t period_ms) {
    return SRNOS_CANRouterAttachAdapter(ch, obj, DJI_C610_CanOps(), period_ms);
}

int DJI_C610_DetachCan(SRNOS_CANChannel ch, DJI_C610* obj) {
    return SRNOS_CANRouterDetachAdapter(ch, obj);
}

} // namespace SRNOS::Devices
