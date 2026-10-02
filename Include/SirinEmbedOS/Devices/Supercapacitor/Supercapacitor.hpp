// === Supercapacitor.hpp ===
// OMX supercapacitor CAN adapter.
// TX (MCU -> SupCap): STD ID 0x114, >=15 Hz recommended.
// RX (SupCap -> MCU): STD ID 0x191, default 500 Hz.

#ifndef SRNOS_SUPERCAPACITOR_HPP
#define SRNOS_SUPERCAPACITOR_HPP

#pragma once
#include <cstdint>
#include <cstddef>
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Devices/CANFrame.h"

namespace SRNOS::Devices {


#ifndef SRNOS_SUPCAP_TX_STD_ID
#define SRNOS_SUPCAP_TX_STD_ID 0x114u
#endif

#ifndef SRNOS_SUPCAP_RX_STD_ID
#define SRNOS_SUPCAP_RX_STD_ID 0x191u
#endif

enum {
  SRNOS_SUPCAP_CTRL_ENABLE_DCDC     = 0x01u,
  SRNOS_SUPCAP_CTRL_RESET_ONCE      = 0x02u,
  SRNOS_SUPCAP_CTRL_ENABLE_WIRELESS = 0x04u,
  SRNOS_SUPCAP_CTRL_GAME_STATUS     = 0x08u,
};

enum {
  SRNOS_SUPCAP_ENERGY_GE_125 = 0x01u,
  SRNOS_SUPCAP_ENERGY_GE_100 = 0x02u,
  SRNOS_SUPCAP_ENERGY_GE_50  = 0x04u,
  SRNOS_SUPCAP_ENERGY_GE_30  = 0x08u,
  SRNOS_SUPCAP_ENERGY_GE_15  = 0x10u,
  SRNOS_SUPCAP_ENERGY_GE_5   = 0x20u,
  SRNOS_SUPCAP_ENERGY_GE_1   = 0x40u,
};

enum {
  SRNOS_SUPCAP_ERR_UNDERVOLTAGE = 0x01u,
  SRNOS_SUPCAP_ERR_OVERVOLTAGE  = 0x02u,
  SRNOS_SUPCAP_ERR_BUCKBOOST    = 0x04u,
  SRNOS_SUPCAP_ERR_SHORTCIRCUIT = 0x08u,
  SRNOS_SUPCAP_ERR_HIGHTEMP     = 0x10u,
  SRNOS_SUPCAP_ERR_NOINPUT      = 0x20u,
  SRNOS_SUPCAP_ERR_CAPACITOR    = 0x40u,
  SRNOS_SUPCAP_ERR_DCDC_NEN     = 0x80u,
};

enum {
  SRNOS_SUPCAP_WILS_READY    = 0x01u,
  SRNOS_SUPCAP_WILS_CHARGING = 0x02u,
  SRNOS_SUPCAP_WILS_FAULT    = 0x04u,
};

struct SupCapTxCommand {
  uint8_t  control = 0;
  uint16_t SRNOS_RefereePowerLimit = 0;
  uint16_t SRNOS_RefereeEnergyBuffer = 0;
  uint8_t  SRNOS_RefereeChassisEnergyFlags = 0;
};

struct SupCapState {
  uint8_t  error_code = 0;
  bool     err_under_voltage = false;
  bool     err_over_voltage = false;
  bool     err_buck_boost = false;
  bool     err_short_circuit = false;
  bool     err_high_temp = false;
  bool     err_no_input = false;
  bool     err_capacitor = false;
  bool     err_dcdc_not_enabled = false;

  uint16_t power_raw = 0;          // Byte[1:2], little-endian
  float    power_W = 0.0f;         // power_raw / 100.0f when valid
  bool     power_valid = false;
  bool     power_out_of_range = false;
  bool     power_is_wireless = false;

  uint8_t  chassis_energy_raw = 0; // Byte[3]
  float    chassis_energy_pct = 0.0f;

  uint8_t  wils_status = 0;        // Byte[4]
  bool     wireless_ready = false;
  bool     wireless_charging = false;
  bool     wireless_fault = false;

  uint16_t chassis_power_limit_raw = 0; // Byte[5:6], little-endian
  float    chassis_power_limit_W = 0.0f; // raw / 50.0f when valid
  bool     chassis_power_limit_valid = false;
  bool     chassis_power_limit_default = false;

  uint8_t  cap_energy_raw = 0;     // Byte[7]
  float    cap_energy_pct = 0.0f;

  uint32_t last_rx_ms = 0;
  bool     online = false;
  uint32_t rx_count = 0;
};

class SupCap {
public:
  explicit SupCap(uint16_t tx_id = SRNOS_SUPCAP_TX_STD_ID, uint16_t rx_id = SRNOS_SUPCAP_RX_STD_ID);

  uint16_t txId() const { return tx_id_; }
  uint16_t rxId() const { return rx_id_; }
  void set_tx_id(uint16_t id) { tx_id_ = id; }
  void set_rx_id(uint16_t id) { rx_id_ = id; }

  void set_enable_supcap(bool en);
  void set_enable_wireless(bool en);
  void set_game_status(bool in_game);
  void request_reset_once();
  void set_control_raw(uint8_t control);

  void set_referee_power_limit(uint16_t limit_raw);
  void set_referee_energy_buffer(uint16_t buffer_raw);
  void set_referee_chassis_energy_flags(uint8_t flags);
  void set_tx_command(const SupCapTxCommand& cmd);

  bool exportTxRaw8(uint16_t* sid, uint8_t out[8]) const;
  bool exportResetTxRaw8(uint16_t* sid, uint8_t out[8], uint8_t* reset_token) const;
  void commitResetTx(uint8_t reset_token) const;

  void onRxFeedback(const CANFrame& f, uint32_t now_ms);
  void set_offline_timeout(uint32_t ms) { offline_ms_ = ms; }
  void tick(uint32_t now_ms);

  bool snapshot(SupCapState& out) const;
  SupCapState state() const noexcept {
    SupCapState s{};
    (void)snapshot(s);
    return s;
  }

private:
  void update_control_bit(uint8_t bit, bool en);
  bool tx_snapshot(SupCapTxCommand& out) const;

  uint16_t tx_id_;
  uint16_t rx_id_;

  mutable volatile uint32_t tx_seq_{0};
  SupCapTxCommand tx_cmd_{};
  mutable uint8_t reset_once_pending_{0};

  mutable volatile uint32_t state_seq_{0};
  SupCapState state_{};
  uint32_t offline_ms_ = 100;
};

const SRNOS_CANAdapterOps* SupCap_CanOps();
int SupCap_AttachCan(SRNOS_CANChannel ch, SupCap* obj, uint32_t period_ms);
int SupCap_DetachCan(SRNOS_CANChannel ch, SupCap* obj);

} // namespace SRNOS::Devices

#endif // SRNOS_SUPERCAPACITOR_HPP
