#ifndef OMX_LIB_IMU_TEMP_CONTROLLER_HPP
#define OMX_LIB_IMU_TEMP_CONTROLLER_HPP

#include <cstdint>

#include "SirinEmbedOS/Algorithms/Control/pid.hpp"

namespace omx::imu {

enum class TempControllerState {
    Cold = 0,
    Warmup,
    Stable,
};

enum class TempControllerStatus {
    Ok,
    NotConfigured,
    Disabled,
    FaultLatched,
    InvalidInput,
    InvalidDt,
};

struct TempControllerConfig {
    float target_c {40.0f};
    control::PidGains gains {};
    float maximum_output {500.0f};
    float maximum_integral {300.0f};
    float stable_delta_c {2.0f};
    std::uint32_t stable_time_ms {5000U};
    float over_temperature_margin_c {10.0f};
    float min_dt_s {0.0002f};
    float max_dt_s {0.0200f};
};

struct TempControllerResult {
    TempControllerStatus status {TempControllerStatus::NotConfigured};
    TempControllerState state {TempControllerState::Cold};
    control::PidResult pid {};
    float current_c {25.0f};
    float target_c {40.0f};
    float output {0.0f};
    float duty {0.0f};
    bool enabled {false};
    bool fault_latched {false};
};

class ImuTemperatureController {
public:
    explicit ImuTemperatureController(control::Pid& pid) noexcept : pid_(pid) {}
    bool configure(const TempControllerConfig&) noexcept;
    void reset(float current_c = 25.0f) noexcept;
    TempControllerResult step(float current_c,
                              float dt_s,
                              std::uint32_t now_ms) noexcept;
    void setTarget(float target_c) noexcept;
    void setEnabled(bool enabled) noexcept;
    void latchFault() noexcept;
    void clearFault() noexcept;
    const TempControllerResult& result() const noexcept { return result_; }
    float maximumOutput() const noexcept { return config_.maximum_output; }

private:
    void updateThermalState(float current_c, std::uint32_t now_ms) noexcept;
    void trackZero(float current_c, float dt_s = 0.0f) noexcept;

    TempControllerConfig config_ {};
    TempControllerResult result_ {};
    control::Pid& pid_;
    bool configured_ {false};
    bool in_range_timer_active_ {false};
    std::uint32_t in_range_start_ms_ {0U};
};

} // namespace omx::imu

#endif
