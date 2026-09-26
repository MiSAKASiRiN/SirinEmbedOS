// === lib_adp_supcap.cpp ===

#include "SirinEmbedOS/Devices/Supercapacitor/lib_adp_supcap.h"
#include "SirinEmbedOS/Core/Endian.hpp"
#include <cstring>

SupCap::SupCap(uint16_t tx_id, uint16_t rx_id)
  : tx_id_(tx_id), rx_id_(rx_id) {}

void SupCap::update_control_bit(uint8_t bit, bool en) {
  if (bit > 7u) return;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  uint8_t control = tx_cmd_.control;
  if (en) control |= static_cast<uint8_t>(1u << bit);
  else    control &= static_cast<uint8_t>(~(1u << bit));
  control &= static_cast<uint8_t>(~SUPCAP_CTRL_RESET_ONCE);
  tx_cmd_.control = control;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_enable_supcap(bool en) {
  update_control_bit(0u, en);
}

void SupCap::set_enable_wireless(bool en) {
  update_control_bit(2u, en);
}

void SupCap::set_game_status(bool in_game) {
  update_control_bit(3u, in_game);
}

void SupCap::request_reset_once() {
  __atomic_store_n(&reset_once_pending_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_control_raw(uint8_t control) {
  control &= static_cast<uint8_t>(~SUPCAP_CTRL_RESET_ONCE);
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  tx_cmd_.control = control;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_referee_power_limit(uint16_t limit_raw) {
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  tx_cmd_.referee_power_limit = limit_raw;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_referee_energy_buffer(uint16_t buffer_raw) {
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  tx_cmd_.referee_energy_buffer = buffer_raw;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_referee_chassis_energy_flags(uint8_t flags) {
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  tx_cmd_.referee_chassis_energy_flags = flags;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::set_tx_command(const SupCapTxCommand& cmd) {
  SupCapTxCommand clean = cmd;
  clean.control &= static_cast<uint8_t>(~SUPCAP_CTRL_RESET_ONCE);
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_ACQ_REL);
  tx_cmd_ = clean;
  __atomic_fetch_add(&tx_seq_, 1u, __ATOMIC_RELEASE);
}

bool SupCap::tx_snapshot(SupCapTxCommand& out) const {
  for (int attempt = 0; attempt < 3; ++attempt) {
    uint32_t s1 = __atomic_load_n(&tx_seq_, __ATOMIC_ACQUIRE);
    if (s1 & 1u) continue;

    SupCapTxCommand tmp;
    std::memcpy(&tmp, &tx_cmd_, sizeof(tmp));

    uint32_t s2 = __atomic_load_n(&tx_seq_, __ATOMIC_ACQUIRE);
    if (s1 == s2) { out = tmp; return true; }
  }
  return false;
}

bool SupCap::exportTxRaw8(uint16_t* sid, uint8_t out[8]) const {
  if (!sid || !out) return false;

  SupCapTxCommand cmd{};
  (void)tx_snapshot(cmd);

  uint8_t control = cmd.control & static_cast<uint8_t>(~SUPCAP_CTRL_RESET_ONCE);

  out[0] = control;
  SRNOS::Memory::MutableByteSpan payload(out, 8u);
  (void)SRNOS::Endian::storeLE16(payload, 1u, cmd.referee_power_limit);
  (void)SRNOS::Endian::storeLE16(payload, 3u, cmd.referee_energy_buffer);
  out[5] = cmd.referee_chassis_energy_flags;
  out[6] = 0;
  out[7] = 0;

  *sid = tx_id_;
  return true;
}

bool SupCap::exportResetTxRaw8(uint16_t* sid, uint8_t out[8], uint8_t* reset_token) const {
  if (!sid || !out || !reset_token) return false;

  const uint8_t reset_once = __atomic_load_n(&reset_once_pending_, __ATOMIC_ACQUIRE);
  if (reset_once == 0u) return false;

  SupCapTxCommand cmd{};
  (void)tx_snapshot(cmd);

  out[0] = static_cast<uint8_t>((cmd.control & static_cast<uint8_t>(~SUPCAP_CTRL_RESET_ONCE)) |
                                SUPCAP_CTRL_RESET_ONCE);
  SRNOS::Memory::MutableByteSpan payload(out, 8u);
  (void)SRNOS::Endian::storeLE16(payload, 1u, cmd.referee_power_limit);
  (void)SRNOS::Endian::storeLE16(payload, 3u, cmd.referee_energy_buffer);
  out[5] = cmd.referee_chassis_energy_flags;
  out[6] = 0;
  out[7] = 0;

  *sid = tx_id_;
  *reset_token = 1u;
  return true;
}

void SupCap::commitResetTx(uint8_t reset_token) const {
  if (reset_token == 0u) return;
  uint8_t expected = reset_token;
  (void)__atomic_compare_exchange_n(&reset_once_pending_,
                                    &expected,
                                    0u,
                                    false,
                                    __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE);
}

void SupCap::onRxFeedback(const CanFrame& f, uint32_t now_ms) {
  if (f.is_ext || f.dlc < 8 || f.id != rx_id_) return;

  const SRNOS::Memory::ByteSpan feedback(f.data);
  uint16_t power_raw = 0u;
  uint16_t limit_raw = 0u;
  if (!SRNOS::Endian::loadLE16(feedback, 1u, power_raw) ||
      !SRNOS::Endian::loadLE16(feedback, 5u, limit_raw)) return;

  __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);
  SupCapState s = state_;

  const uint8_t err = f.data[0];
  s.error_code = err;
  s.err_under_voltage = (err & SUPCAP_ERR_UNDERVOLTAGE) != 0u;
  s.err_over_voltage  = (err & SUPCAP_ERR_OVERVOLTAGE) != 0u;
  s.err_buck_boost    = (err & SUPCAP_ERR_BUCKBOOST) != 0u;
  s.err_short_circuit = (err & SUPCAP_ERR_SHORTCIRCUIT) != 0u;
  s.err_high_temp     = (err & SUPCAP_ERR_HIGHTEMP) != 0u;
  s.err_no_input      = (err & SUPCAP_ERR_NOINPUT) != 0u;
  s.err_capacitor     = (err & SUPCAP_ERR_CAPACITOR) != 0u;
  s.err_dcdc_not_enabled = (err & SUPCAP_ERR_DCDC_NEN) != 0u;

  s.power_raw = power_raw;
  s.power_valid = (power_raw != 0xFFFFu && power_raw != 0xFFFEu);
  s.power_out_of_range = (power_raw == 0xFFFEu);
  s.power_W = s.power_valid ? (static_cast<float>(power_raw) / 100.0f) : 0.0f;

  s.chassis_energy_raw = f.data[3];
  s.chassis_energy_pct = (static_cast<float>(s.chassis_energy_raw) * 100.0f) / 255.0f;

  const uint8_t wils = f.data[4];
  s.wils_status = wils;
  s.wireless_ready = (wils & SUPCAP_WILS_READY) != 0u;
  s.wireless_charging = (wils & SUPCAP_WILS_CHARGING) != 0u;
  s.wireless_fault = (wils & SUPCAP_WILS_FAULT) != 0u;
  s.power_is_wireless = s.wireless_charging;

  s.chassis_power_limit_raw = limit_raw;
  s.chassis_power_limit_valid = (limit_raw != 0xFFFFu && limit_raw != 0xFFFEu);
  s.chassis_power_limit_default = (limit_raw == 0xFFFEu);
  s.chassis_power_limit_W = s.chassis_power_limit_valid
    ? (static_cast<float>(limit_raw) / 50.0f)
    : 0.0f;

  s.cap_energy_raw = f.data[7];
  s.cap_energy_pct = (static_cast<float>(s.cap_energy_raw) * 100.0f) / 255.0f;

  s.last_rx_ms = now_ms;
  s.online = true;
  s.rx_count++;

  state_ = s;
  __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
}

void SupCap::tick(uint32_t now_ms) {
  const uint32_t last = __atomic_load_n(&state_.last_rx_ms, __ATOMIC_RELAXED);
  const bool was_online = __atomic_load_n(&state_.online, __ATOMIC_RELAXED);
  if (was_online && (now_ms - last > offline_ms_)) {
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_ACQ_REL);
    state_.online = false;
    __atomic_fetch_add(&state_seq_, 1u, __ATOMIC_RELEASE);
  }
}

bool SupCap::snapshot(SupCapState& out) const {
  for (int attempt = 0; attempt < 3; ++attempt) {
    uint32_t s1 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
    if (s1 & 1u) continue;

    SupCapState tmp;
    std::memcpy(&tmp, &state_, sizeof(tmp));

    uint32_t s2 = __atomic_load_n(&state_seq_, __ATOMIC_ACQUIRE);
    if (s1 == s2) { out = tmp; return true; }
  }
  return false;
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

void fill_raw_item(can_tx_item_t* item, uint16_t sid, const uint8_t payload[8], uintptr_t token) {
  std::memset(item, 0, sizeof(*item));
  item->format = CAN_TX_FORMAT_RAW_FRAME;
  item->token = token;
  item->frame.id = sid;
  item->frame.dlc = 8u;
  item->frame.flags = 0u;
  for (size_t i = 0u; i < sizeof(item->frame.data); ++i) {
    item->frame.data[i] = payload[i];
  }
}

size_t supcap_rx_filter_count(void* obj) {
  return obj ? 1u : 0u;
}

bool supcap_rx_filter_at(void* obj, size_t index, can_rx_filter_t* out) {
  if (!obj || !out || index != 0u) return false;
  auto* cap = static_cast<SupCap*>(obj);
  out->id = cap->rxId();
  out->flags = 0u;
  return true;
}

void supcap_on_rx(void* obj, const can_frame_t* frame, uint32_t now_ms) {
  if (!obj || !frame) return;
  static_cast<SupCap*>(obj)->onRxFeedback(to_adp_frame(frame), now_ms);
}

size_t supcap_poll_tx(void* obj,
                      can_tx_kind_t kind,
                      can_tx_item_t* out,
                      size_t max,
                      uint32_t now_ms) {
  (void)now_ms;
  if (!obj || !out || max == 0u) return 0u;

  auto* cap = static_cast<SupCap*>(obj);
  uint16_t sid = 0u;
  uint8_t payload[8] = {0};

  if (kind == CAN_TX_KIND_URGENT) {
    uint8_t token = 0u;
    if (!cap->exportResetTxRaw8(&sid, payload, &token)) return 0u;
    fill_raw_item(&out[0], sid, payload, static_cast<uintptr_t>(token));
    return 1u;
  }

  if (kind == CAN_TX_KIND_PERIODIC) {
    if (!cap->exportTxRaw8(&sid, payload)) return 0u;
    fill_raw_item(&out[0], sid, payload, 0u);
    return 1u;
  }

  return 0u;
}

void supcap_commit_tx(void* obj,
                      can_tx_kind_t kind,
                      const can_tx_item_t* item,
                      can_tx_result_t result) {
  if (!obj || !item) return;
  if (kind == CAN_TX_KIND_URGENT && result == CAN_TX_RESULT_ACCEPTED) {
    static_cast<SupCap*>(obj)->commitResetTx(static_cast<uint8_t>(item->token));
  }
}

void supcap_tick(void* obj, uint32_t now_ms) {
  if (!obj) return;
  static_cast<SupCap*>(obj)->tick(now_ms);
}

const can_adapter_ops_t s_supcap_can_ops = {
    supcap_rx_filter_count,
    supcap_rx_filter_at,
    supcap_on_rx,
    supcap_poll_tx,
    supcap_commit_tx,
    supcap_tick,
};
} // namespace

const can_adapter_ops_t* SupCap_CanOps() {
  return &s_supcap_can_ops;
}

int SupCap_AttachCan(can_channel_t ch, SupCap* obj, uint32_t period_ms) {
  return can_router_attach_adapter(ch, obj, SupCap_CanOps(), period_ms);
}

int SupCap_DetachCan(can_channel_t ch, SupCap* obj) {
  return can_router_detach_adapter(ch, obj);
}
