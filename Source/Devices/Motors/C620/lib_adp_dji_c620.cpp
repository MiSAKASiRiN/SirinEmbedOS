// === lib_adp_dji_c620.cpp ===

#include "SirinEmbedOS/Devices/Motors/C620/lib_adp_dji_c620.h"
#include "SirinEmbedOS/Core/Endian.hpp"
#include <cstring>
#include <cstddef>
#include <atomic>

// ---- 映射规则（型号绑定单一事实来源）----
static inline uint32_t c620_map_rx_id(uint8_t motor_id) {
    return 0x200u + motor_id; // 反馈 0x201..0x208
}
static inline bool c620_map_group_slot(uint8_t motor_id, uint16_t& gid, uint8_t& slot) {
    if (motor_id >= 1 && motor_id <= 4)  { gid = 0x200; slot = static_cast<uint8_t>(motor_id - 1); return true; }
    if (motor_id >= 5 && motor_id <= 8)  { gid = 0x1FF; slot = static_cast<uint8_t>(motor_id - 5); return true; }
    return false;
}

static inline uint8_t clamp_id_1_8(uint8_t id) { return (id < 1) ? 1 : (id > 8 ? 8 : id); }

// ---- 构造 ----
DJI_C620::DJI_C620(uint8_t motor_id, uint32_t rx_id)
    : motor_id_(clamp_id_1_8(motor_id)),
      group_id_([&](){ uint16_t g; uint8_t s; c620_map_group_slot(clamp_id_1_8(motor_id), g, s); return g; }()),
      rx_id_(rx_id ? rx_id : c620_map_rx_id(clamp_id_1_8(motor_id)))
{}

// ---- 控制值写入 ----
void DJI_C620::set_give_cmd(int16_t cmd_raw) {
    if (cmd_raw >  16384) cmd_raw =  16384;
    if (cmd_raw < -16384) cmd_raw = -16384;
    give_cmd_raw_.store(cmd_raw, std::memory_order_relaxed);
    dbg_cmd_shadow_ = cmd_raw;
}

// ---- 反馈解析（仅处理本电机的反馈帧）----
void DJI_C620::onRxFeedback(const CanFrame& f, uint32_t now_ms) {
    if (f.is_ext || f.dlc < 8 || f.id != rx_id_) return;

    // seqlock 写开始：置奇数
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);
    C620State s = state_;

    s.prev_angle_raw = s.mech_angle_raw;
    s.prev_speed_raw = s.speed_raw;
    s.prev_iq_raw    = s.iq_raw;

    const SRNOS::Memory::ByteSpan feedback(f.data);
    uint16_t angle_raw = 0u;
    uint16_t speed_raw = 0u;
    uint16_t iq_raw = 0u;
    if (!SRNOS::Endian::loadBE16(feedback, 0u, angle_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 2u, speed_raw) ||
        !SRNOS::Endian::loadBE16(feedback, 4u, iq_raw)) return;
    s.mech_angle_raw = angle_raw & 0x1FFFu; // 常见 13 位有效
    s.speed_raw      = static_cast<int16_t>(speed_raw);
    s.iq_raw         = static_cast<int16_t>(iq_raw);
    s.temp_C         = f.data[6];
    s.mech_angle_deg = (static_cast<float>(s.mech_angle_raw) * 360.0f) / 8192.0f;

    s.last_rx_ms = now_ms;
    s.online     = true;

    state_ = s;
    // seqlock 写结束：置偶数
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
}

// ---- 离线判定 ----
void DJI_C620::tick(uint32_t now_ms) {
    // 轻读：原子读取 last_rx_ms / online
    const uint32_t last = __atomic_load_n(&state_.last_rx_ms, __ATOMIC_RELAXED);
        const bool was_online = __atomic_load_n(&state_.online, __ATOMIC_RELAXED);
        if (was_online && (now_ms - last > offline_ms_)) {
            __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);
            state_.online = false;
            __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
        }
}

bool DJI_C620::snapshot(C620State& out) const {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t s1 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue; // 写入中，重试

        C620State tmp;
        std::memcpy(&tmp, &state_, sizeof(tmp));

        uint32_t s2 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 == s2) { out = tmp; return true; }
    }
    return false;
}


// ---- 导出给 TX 路由器：一个对象 -> (group_id, slot, val) ----
bool DJI_C620::exportTx16(uint16_t* group_id, uint8_t* slot, int16_t* val) const {
    if (!group_id || !slot || !val) return false;
    if (!c620_map_group_slot(motor_id_, *group_id, *slot)) return false;
    *val = give_cmd_raw_.load(std::memory_order_relaxed);
    return true;
}

// ---- 清帧工具 ----
static void clear_frame(CanFrame& f, uint32_t id) {
    f.id = id;
    f.dlc = 8;
    f.is_ext = false;
    std::memset(f.data, 0, sizeof(f.data));
}

// ---- 批量组帧（最小系统/Bring-up 用）----
void C620_ComposeTxFrames(DJI_C620* const motors[],
                          std::size_t n,
                          CanFrame out[],
                          std::size_t& out_count)
{
    int16_t cur_1_4[4] = {0,0,0,0};
    int16_t cur_5_8[4] = {0,0,0,0};
    bool have_200 = false, have_1FF = false;

    for (std::size_t i = 0; i < n; ++i) {
        DJI_C620* m = motors[i];
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
CanFrame to_adp_frame(const can_frame_t* frame) {
    CanFrame out{};
    if (!frame) return out;
    out.id = frame->id;
    out.dlc = frame->dlc;
    out.is_ext = (frame->flags & CAN_FRAME_FLAG_EXT) != 0u;
    std::memcpy(out.data, frame->data, sizeof(out.data));
    return out;
}

size_t c620_rx_filter_count(void* obj) {
    return obj ? 1u : 0u;
}

bool c620_rx_filter_at(void* obj, size_t index, can_rx_filter_t* out) {
    if (!obj || !out || index != 0u) return false;
    auto* motor = static_cast<DJI_C620*>(obj);
    out->id = motor->rxId();
    out->flags = 0u;
    return true;
}

void c620_on_rx(void* obj, const can_frame_t* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<DJI_C620*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t c620_poll_tx(void* obj,
                    can_tx_kind_t kind,
                    can_tx_item_t* out,
                    size_t max,
                    uint32_t now_ms) {
    (void)now_ms;
    if (!obj || !out || max == 0u || kind != CAN_TX_KIND_PERIODIC) return 0u;
    auto* motor = static_cast<DJI_C620*>(obj);
    uint16_t group_id = 0u;
    uint8_t slot = 0u;
    int16_t value = 0;
    if (!motor->exportTx16(&group_id, &slot, &value)) return 0u;

    std::memset(&out[0], 0, sizeof(out[0]));
    out[0].format = CAN_TX_FORMAT_GROUP_I16;
    out[0].group_i16.group_id = group_id;
    out[0].group_i16.flags = 0u;
    out[0].group_i16.slot = slot;
    out[0].group_i16.endian = CAN_TX_ENDIAN_BE;
    out[0].group_i16.value = value;
    return 1u;
}

void c620_tick(void* obj, uint32_t now_ms) {
    if (!obj) return;
    static_cast<DJI_C620*>(obj)->tick(now_ms);
}

const can_adapter_ops_t s_c620_can_ops = {
    c620_rx_filter_count,
    c620_rx_filter_at,
    c620_on_rx,
    c620_poll_tx,
    nullptr,
    c620_tick,
};
} // namespace

const can_adapter_ops_t* DJI_C620_CanOps() {
    return &s_c620_can_ops;
}

int DJI_C620_AttachCan(can_channel_t ch, DJI_C620* obj, uint32_t period_ms) {
    return can_router_attach_adapter(ch, obj, DJI_C620_CanOps(), period_ms);
}

int DJI_C620_DetachCan(can_channel_t ch, DJI_C620* obj) {
    return can_router_detach_adapter(ch, obj);
}
