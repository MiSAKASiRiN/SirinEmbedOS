#ifndef OMX_LIB_CONTROLS_PID_HPP
#define OMX_LIB_CONTROLS_PID_HPP

#include "SirinEmbedOS/Algorithms/Math/signal.hpp"

namespace omx::control {

enum class PidForm {
    Positional,
    Incremental,
};

enum class DerivativeMode {
    OnMeasurement,
    OnError,
};

enum class AntiWindupMode {
    Clamp,
    Conditional,
    BackCalculation,
};

struct PidGains {
    float kp {0.0f};
    float ki {0.0f};
    float kd {0.0f};
};

struct PidConfig {
    PidForm form {PidForm::Positional};
    PidGains gains {};

    float output_min {-1.0f};
    float output_max {1.0f};
    float integral_min {-1.0f};
    float integral_max {1.0f};

    DerivativeMode derivative_mode {DerivativeMode::OnMeasurement};
    AntiWindupMode anti_windup {AntiWindupMode::Clamp};
    float back_calculation_gain {0.0f};

    float error_deadband {0.0f};
    float integral_zone {0.0f};
    float derivative_cutoff_hz {0.0f};
    float output_cutoff_hz {0.0f};
    float slew_up_per_s {0.0f};
    float slew_down_per_s {0.0f};
    float error_period {0.0f};

    float min_dt_s {1.0e-6f};
    float max_dt_s {0.5f};
    float fail_output {0.0f};
};

struct PidInput {
    float setpoint {0.0f};
    float measurement {0.0f};
    float feedforward {0.0f};
    float dt_s {0.0f};
};

enum class PidStatus {
    Ok,
    NotConfigured,
    InvalidConfig,
    InvalidInput,
    InvalidDt,
};

struct PidResult {
    PidStatus status {PidStatus::NotConfigured};
    float output {0.0f};
    float raw_output {0.0f};
    float error {0.0f};
    float p {0.0f};
    float i {0.0f};
    float d {0.0f};
    float feedforward {0.0f};
    float dt_s {0.0f};
    bool saturated {false};
    bool slew_limited {false};
};

class Pid {
public:
    PidStatus configure(const PidConfig&) noexcept;
    PidStatus setGains(const PidGains&) noexcept;
    void reset(float measurement = 0.0f,
               float tracked_output = 0.0f) noexcept;
    void track(float setpoint,
               float measurement,
               float actual_output) noexcept;
    PidResult step(const PidInput&) noexcept;
    const PidResult& lastResult() const noexcept { return last_result_; }

private:
    static bool validGains(const PidGains&) noexcept;
    static bool validConfig(const PidConfig&) noexcept;
    float errorFor(float setpoint, float measurement) const noexcept;
    float difference(float current, float previous) const noexcept;
    PidResult fail(PidStatus status, float dt_s = 0.0f) noexcept;

    PidConfig config_ {};
    PidResult last_result_ {};
    bool configured_ {false};
    bool initialized_ {false};
    float previous_measurement_ {0.0f};
    float previous_error_ {0.0f};
    float previous_p_ {0.0f};
    float previous_d_ {0.0f};
    float integral_ {0.0f};
    float controller_output_ {0.0f};
    float tracked_output_ {0.0f};
    algo::FirstOrderLowPass derivative_filter_ {};
    algo::FirstOrderLowPass output_filter_ {};
    algo::SlewRateLimiter slew_limiter_ {};
};

} // namespace omx::control

#endif
