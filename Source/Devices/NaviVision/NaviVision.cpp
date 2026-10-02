// === NaviVision.cpp ===

#include "SirinEmbedOS/Devices/NaviVision/NaviVision.hpp"
#include "SirinEmbedOS/Core/Endian.hpp"
#include <cstddef>
#include <cstring>

namespace SRNOS::Devices {

Navi::Navi(uint16_t rx_id)
    : rx_id_(rx_id)
{}

void Navi::onRxFeedback(const CANFrame& f, uint32_t now_ms) {
    if (f.is_ext || f.dlc < 8 || f.id != rx_id_) return;

    const SRNOS::Memory::ByteSpan feedback(f.data);
    uint16_t vx = 0u;
    uint16_t vy = 0u;
    uint16_t omega_z = 0u;
    if (!SRNOS::Endian::loadLE16(feedback, 0u, vx) ||
        !SRNOS::Endian::loadLE16(feedback, 2u, vy) ||
        !SRNOS::Endian::loadLE16(feedback, 4u, omega_z)) return;

    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);

    NaviState s = state_;
    s.vx = static_cast<int16_t>(vx);
    s.vy = static_cast<int16_t>(vy);
    s.omega_z = static_cast<int16_t>(omega_z);
    s.reserved0 = f.data[6];
    s.reserved1 = f.data[7];
    s.is_navi_online = (f.data[6] == 0xFFu) && (f.data[7] == 0xFFu);
    s.last_rx_ms = now_ms;
    s.rx_count++;

    state_ = s;

    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
}

bool Navi::snapshot(NaviState& out) const {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t s1 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;

        NaviState tmp;
        std::memcpy(&tmp, &state_, sizeof(tmp));

        uint32_t s2 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 == s2) {
            out = tmp;
            return true;
        }
    }
    return false;
}

Vision::Vision(uint16_t b1_id, uint32_t timeout_ms)
    : b1_id_(b1_id)
    , timeout_ms_(timeout_ms)
{
    std::memset(raw_, 0, sizeof(raw_));
    for (uint8_t i = 0; i < SRNOS_VISION_ITEM_COUNT; ++i) {
        raw_seq_[i] = 0;
        last_seq_[i] = 0;
    }
    std::memset(&state_, 0, sizeof(state_));
}

VisionItem* Vision::item_by_index(VisionState* s, uint8_t idx)
{
    if (!s || idx >= SRNOS_VISION_ITEM_COUNT) return nullptr;

    static const size_t offsets[SRNOS_VISION_ITEM_COUNT] = {
        offsetof(VisionState, B1),
        offsetof(VisionState, B2),
        offsetof(VisionState, B3),
        offsetof(VisionState, B4),
        offsetof(VisionState, B5),
        offsetof(VisionState, BOut),
        offsetof(VisionState, BSen),
        offsetof(VisionState, R1),
        offsetof(VisionState, R2),
        offsetof(VisionState, R3),
        offsetof(VisionState, R4),
        offsetof(VisionState, R5),
        offsetof(VisionState, ROut),
        offsetof(VisionState, RSen),
    };

    return reinterpret_cast<VisionItem*>(reinterpret_cast<uint8_t*>(s) + offsets[idx]);
}

const VisionItem* Vision::item_by_index(const VisionState* s, uint8_t idx)
{
    return item_by_index(const_cast<VisionState*>(s), idx);
}

bool Vision::raw_snapshot(uint8_t idx, VisionRaw& out, uint32_t& seq) const
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t s1 = __atomic_load_n(&raw_seq_[idx], __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;

        VisionRaw tmp = raw_[idx];

        uint32_t s2 = __atomic_load_n(&raw_seq_[idx], __ATOMIC_ACQUIRE);
        if (s1 == s2) {
            out = tmp;
            seq = s2;
            return true;
        }
    }
    return false;
}

void Vision::onRxFeedback(const CANFrame& f, uint32_t now_ms)
{
    if (f.is_ext || f.dlc < 8) return;
    if (f.data[4] || f.data[5] || f.data[6]) return;

    const uint16_t base = b1_id_;
    if (f.id < base || f.id >= (uint16_t)(base + SRNOS_VISION_ITEM_COUNT)) return;
    const uint8_t idx = static_cast<uint8_t>(f.id - base);

    const SRNOS::Memory::ByteSpan feedback(f.data);
    uint16_t dx = 0u;
    uint16_t dy = 0u;
    if (!SRNOS::Endian::loadLE16(feedback, 0u, dx) ||
        !SRNOS::Endian::loadLE16(feedback, 2u, dy)) return;

    __atomic_fetch_add(&raw_seq_[idx], 1u, __ATOMIC_ACQ_REL);

    VisionRaw r = raw_[idx];
    r.dx = static_cast<int16_t>(dx);
    r.dy = static_cast<int16_t>(dy);
    r.fire = (f.data[7] == 0xFFu) ? 1u : 0u;
    r.last_rx_ms = now_ms;
    raw_[idx] = r;

    __atomic_fetch_add(&raw_seq_[idx], 1u, __ATOMIC_RELEASE);
    __atomic_fetch_add(&rx_any_count_, 1u, __ATOMIC_RELAXED);
}

void Vision::update(uint32_t now_ms)
{
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);

    for (uint8_t idx = 0; idx < SRNOS_VISION_ITEM_COUNT; ++idx) {
        VisionItem* item = item_by_index(&state_, idx);
        if (!item) continue;

        VisionRaw raw{};
        uint32_t seq = 0;
        if (raw_snapshot(idx, raw, seq)) {
            const bool has_new_frame = (seq != last_seq_[idx]);
            if (has_new_frame) {
                last_seq_[idx] = seq;
            }

            bool within_timeout = false;
            if (raw.last_rx_ms != 0u) {
                if (timeout_ms_ == 0u) {
                    within_timeout = true;
                } else {
                    const uint32_t age_ms = now_ms - raw.last_rx_ms;
                    within_timeout = (age_ms <= timeout_ms_);
                }
            }

            if (has_new_frame || within_timeout) {
                item->dx = raw.dx;
                item->dy = raw.dy;
                item->fire = raw.fire;
                item->is_appear = 1u;
            } else {
                item->dx = 0;
                item->dy = 0;
                item->fire = 0;
                item->is_appear = 0u;
            }
        } else {
            item->dx = 0;
            item->dy = 0;
            item->fire = 0;
            item->is_appear = 0u;
        }
    }

    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
}

bool Vision::snapshot(VisionState& out) const
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t s1 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;

        VisionState tmp;
        std::memcpy(&tmp, &state_, sizeof(tmp));

        uint32_t s2 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
        if (s1 == s2) {
            out = tmp;
            return true;
        }
    }
    return false;
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

size_t navi_rx_filter_count(void* obj) {
    return obj ? 1u : 0u;
}

bool navi_rx_filter_at(void* obj, size_t index, SRNOS_CANRxFilter* out) {
    if (!obj || !out || index != 0u) return false;
    auto* navi = static_cast<Navi*>(obj);
    out->id = navi->rxId();
    out->flags = 0u;
    return true;
}

void navi_on_rx(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<Navi*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

const SRNOS_CANAdapterOps s_navi_can_ops = {
    navi_rx_filter_count,
    navi_rx_filter_at,
    navi_on_rx,
    nullptr,
    nullptr,
    nullptr,
};

size_t vision_rx_filter_count(void* obj) {
    return obj ? SRNOS_VISION_ITEM_COUNT : 0u;
}

bool vision_rx_filter_at(void* obj, size_t index, SRNOS_CANRxFilter* out) {
    if (!obj || !out || index >= SRNOS_VISION_ITEM_COUNT) return false;
    auto* vision = static_cast<Vision*>(obj);
    out->id = static_cast<uint32_t>(vision->base_id() + index);
    out->flags = 0u;
    return true;
}

void vision_on_rx(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
    if (!obj || !frame) return;
    static_cast<Vision*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

void vision_tick(void* obj, uint32_t now_ms) {
    if (!obj) return;
    static_cast<Vision*>(obj)->update(now_ms);
}

const SRNOS_CANAdapterOps s_vision_can_ops = {
    vision_rx_filter_count,
    vision_rx_filter_at,
    vision_on_rx,
    nullptr,
    nullptr,
    vision_tick,
};
} // namespace

const SRNOS_CANAdapterOps* Navi_CanOps() {
    return &s_navi_can_ops;
}

int Navi_AttachCan(SRNOS_CANChannel ch, Navi* obj) {
    return SRNOS_CANRouterAttachAdapter(ch, obj, Navi_CanOps(), 1000u);
}

int Navi_DetachCan(SRNOS_CANChannel ch, Navi* obj) {
    return SRNOS_CANRouterDetachAdapter(ch, obj);
}

const SRNOS_CANAdapterOps* Vision_CanOps() {
    return &s_vision_can_ops;
}

int Vision_AttachCan(SRNOS_CANChannel ch, Vision* obj) {
    return SRNOS_CANRouterAttachAdapter(ch, obj, Vision_CanOps(), 1u);
}

int Vision_DetachCan(SRNOS_CANChannel ch, Vision* obj) {
    return SRNOS_CANRouterDetachAdapter(ch, obj);
}

} // namespace SRNOS::Devices
