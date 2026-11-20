#ifndef SRNOS_MOTOR_DM_HPP
#define SRNOS_MOTOR_DM_HPP

#include <cstdint>
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"

namespace SRNOS::Devices {

enum class DMModel : uint8_t { DM4310, DM4340V11, DM6006, DM8009P };
enum class DMMode : uint8_t { MIT, PosVel, Velocity, ForcePosition };

struct DMLimits {
    float pmax_rad, vmax_rad_s, tmax_nm; // Must match the drive's three MIT registers.
};

struct DMConfig {
    DMModel model;
    uint16_t can_id, feedback_sid;
    DMMode mode; // Mode already configured on the drive.
    DMLimits limits;
    uint32_t offline_ms = 100;
    float feedback_period_rad = 0; // Zero uses the full reported interval 2*PMAX.
};

struct DMFeedback {
    uint16_t position_raw{}, velocity_raw{}, torque_raw{}; // Unsigned wire codes.
    float position_rad{}, velocity_rad_s{}, torque_nm{};
    double continuous_position_rad{};
    uint8_t drive_status{}, mos_temp_c{}, rotor_temp_c{};
    uint32_t last_rx_ms{};
    bool online{};
};

namespace Detail { struct DMCanAdapter; }

class DMMotor {
public:
    DMMotor() = default;
    DMMotor(const DMMotor&) = delete;
    DMMotor& operator=(const DMMotor&) = delete;

    // ApplicationConfigure only. Config is copied. Keep this object alive while attached.
    // Zero on success, negative on invalid configuration, duplicate or router error.
    int registerCan(SRNOS_CANChannel channel, const DMConfig& config, uint32_t period_ms);
    int unregisterCan(); // Configuration stage only.

    // Task context or IRQ priority 5..15. False leaves the previous command intact.
    bool setMIT(float position_rad, float velocity_rad_s, float kp, float kd, float torque_nm);
    bool setPosVel(float position_rad, float max_velocity_rad_s);
    bool setVelocity(float velocity_rad_s);
    bool setForcePosition(float position_rad, float max_velocity_rad_s, float current_fraction);
    bool requestEnable();
    bool requestDisable();
    DMFeedback feedback() const;

private:
    friend struct Detail::DMCanAdapter;
    bool publish(const SRNOS_CANFrame& frame);
    void receive(const SRNOS_CANFrame& frame, uint32_t now_ms);
    DMConfig config_{};
    SRNOS_CANChannel channel_{};
    uint16_t command_sid_{};
    SRNOS_CANFrame command_{};
    uint8_t pending_{};
    int64_t wraps_{};
    DMFeedback feedback_{};
};

} // namespace SRNOS::Devices
#endif
