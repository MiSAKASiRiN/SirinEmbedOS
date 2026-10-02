#include "SirinEmbedOS/Algorithms/Control/pid.hpp"

#include <algorithm>
#include <cmath>

#include "SirinEmbedOS/Algorithms/Math/angle.hpp"

namespace SRNOS::Control {
namespace {

bool finite(float value) noexcept
{
    return std::isfinite(value);
}

bool differs(float left, float right) noexcept
{
    const float scale = std::max(1.0f, std::max(std::fabs(left), std::fabs(right)));
    return std::fabs(left - right) > 1.0e-6f * scale;
}

} // namespace

bool Pid::validGains(const PidGains& gains) noexcept
{
    return finite(gains.kp) && finite(gains.ki) && finite(gains.kd);
}

bool Pid::validConfig(const PidConfig& config) noexcept
{
    return validGains(config.gains) &&
           finite(config.output_min) && finite(config.output_max) &&
           config.output_min < config.output_max &&
           finite(config.integral_min) && finite(config.integral_max) &&
           config.integral_min <= config.integral_max &&
           finite(config.back_calculation_gain) &&
           config.back_calculation_gain >= 0.0f &&
           finite(config.error_deadband) && config.error_deadband >= 0.0f &&
           finite(config.integral_zone) &&
           finite(config.derivative_cutoff_hz) &&
           config.derivative_cutoff_hz >= 0.0f &&
           finite(config.output_cutoff_hz) && config.output_cutoff_hz >= 0.0f &&
           finite(config.slew_up_per_s) && config.slew_up_per_s >= 0.0f &&
           finite(config.slew_down_per_s) && config.slew_down_per_s >= 0.0f &&
           finite(config.error_period) && config.error_period >= 0.0f &&
           finite(config.min_dt_s) && finite(config.max_dt_s) &&
           config.min_dt_s > 0.0f && config.max_dt_s >= config.min_dt_s &&
           finite(config.fail_output);
}

PidStatus Pid::configure(const PidConfig& config) noexcept
{
    if (!validConfig(config)) {
        configured_ = false;
        last_result_ = {};
        last_result_.status = PidStatus::InvalidConfig;
        return PidStatus::InvalidConfig;
    }
    config_ = config;
    configured_ = true;
    reset(0.0f, std::clamp(0.0f,
                           config.output_min,
                           config.output_max));
    return PidStatus::Ok;
}

PidStatus Pid::setGains(const PidGains& gains) noexcept
{
    if (!configured_) {
        return PidStatus::NotConfigured;
    }
    if (!validGains(gains)) {
        return PidStatus::InvalidConfig;
    }
    config_.gains = gains;
    return PidStatus::Ok;
}

void Pid::reset(float measurement, float tracked_output) noexcept
{
    if (!finite(measurement)) {
        measurement = 0.0f;
    }
    if (!finite(tracked_output)) {
        tracked_output = configured_ ? config_.fail_output : 0.0f;
    }
    if (configured_) {
        tracked_output = std::clamp(tracked_output,
                                    config_.output_min,
                                    config_.output_max);
    }
    initialized_ = false;
    previous_measurement_ = measurement;
    previous_error_ = 0.0f;
    previous_p_ = 0.0f;
    previous_d_ = 0.0f;
    integral_ = 0.0f;
    controller_output_ = tracked_output;
    tracked_output_ = tracked_output;
    derivative_filter_.reset(0.0f);
    output_filter_.reset(tracked_output);
    slew_limiter_.reset(tracked_output);
    last_result_ = {};
    last_result_.status = configured_ ? PidStatus::Ok : PidStatus::NotConfigured;
    last_result_.output = tracked_output;
    last_result_.raw_output = tracked_output;
}

float Pid::errorFor(float setpoint, float measurement) const noexcept
{
    const float linear_error = setpoint - measurement;
    const float error = config_.error_period > 0.0f
                            ? SRNOS::Math::wrapPeriodic(linear_error, config_.error_period)
                            : linear_error;
    return std::fabs(error) <= config_.error_deadband ? 0.0f : error;
}

float Pid::difference(float current, float previous) const noexcept
{
    const float delta = current - previous;
    return config_.error_period > 0.0f
               ? SRNOS::Math::wrapPeriodic(delta, config_.error_period)
               : delta;
}

void Pid::track(float setpoint, float measurement, float actual_output) noexcept
{
    if (!configured_ || !finite(setpoint) || !finite(measurement) ||
        !finite(actual_output)) {
        return;
    }
    const float actual = std::clamp(actual_output,
                                    config_.output_min,
                                    config_.output_max);
    const float error = errorFor(setpoint, measurement);
    const float p = config_.gains.kp * error;
    integral_ = std::clamp(actual - p,
                           config_.integral_min,
                           config_.integral_max);
    controller_output_ = actual;
    tracked_output_ = actual;
    previous_measurement_ = measurement;
    previous_error_ = error;
    previous_p_ = p;
    previous_d_ = 0.0f;
    derivative_filter_.reset(0.0f);
    output_filter_.reset(actual);
    slew_limiter_.reset(actual);
    initialized_ = true;
    last_result_ = {};
    last_result_.status = PidStatus::Ok;
    last_result_.output = actual;
    last_result_.raw_output = actual;
    last_result_.error = error;
    last_result_.p = p;
    last_result_.i = integral_;
}

PidResult Pid::fail(PidStatus status, float dt_s) noexcept
{
    last_result_ = {};
    last_result_.status = status;
    last_result_.output = configured_
                              ? std::clamp(config_.fail_output,
                                           config_.output_min,
                                           config_.output_max)
                              : 0.0f;
    last_result_.raw_output = last_result_.output;
    last_result_.dt_s = dt_s;
    return last_result_;
}

PidResult Pid::step(const PidInput& input) noexcept
{
    if (!configured_) {
        return fail(PidStatus::NotConfigured, input.dt_s);
    }
    if (!finite(input.setpoint) || !finite(input.measurement) ||
        !finite(input.feedforward)) {
        return fail(PidStatus::InvalidInput, input.dt_s);
    }
    if (!finite(input.dt_s) || input.dt_s < config_.min_dt_s ||
        input.dt_s > config_.max_dt_s) {
        return fail(PidStatus::InvalidDt, input.dt_s);
    }

    const float error = errorFor(input.setpoint, input.measurement);
    const float p = config_.gains.kp * error;
    if (!initialized_) {
        previous_measurement_ = input.measurement;
        previous_error_ = error;
        previous_p_ = 0.0f;
        previous_d_ = 0.0f;
        integral_ = 0.0f;
        controller_output_ = tracked_output_ - input.feedforward;
        derivative_filter_.reset(0.0f);
        output_filter_.reset(tracked_output_);
        slew_limiter_.reset(tracked_output_);
        initialized_ = true;
    }

    float derivative = 0.0f;
    if (config_.derivative_mode == DerivativeMode::OnMeasurement) {
        derivative = -difference(input.measurement, previous_measurement_) /
                     input.dt_s;
    } else {
        derivative = difference(error, previous_error_) / input.dt_s;
    }
    if (config_.derivative_cutoff_hz > 0.0f) {
        derivative = derivative_filter_.step(derivative,
                                             config_.derivative_cutoff_hz,
                                             input.dt_s);
    } else {
        derivative_filter_.reset(derivative);
    }
    const float d = config_.gains.kd * derivative;

    const bool integration_enabled = config_.integral_zone <= 0.0f ||
                                     std::fabs(error) <= config_.integral_zone;
    const float integral_before = integral_;
    const float integral_delta = integration_enabled
                                     ? config_.gains.ki * error * input.dt_s
                                     : 0.0f;
    integral_ = std::clamp(integral_ + integral_delta,
                           config_.integral_min,
                           config_.integral_max);

    float controller_candidate = 0.0f;
    if (config_.form == PidForm::Positional) {
        controller_candidate = p + integral_ + d;
    } else {
        controller_candidate = controller_output_ +
                               (p - previous_p_) +
                               (integral_ - integral_before) +
                               (d - previous_d_);
    }
    float raw_output = controller_candidate + input.feedforward;

    const bool pushes_high = raw_output > config_.output_max && error > 0.0f;
    const bool pushes_low = raw_output < config_.output_min && error < 0.0f;
    if (config_.anti_windup == AntiWindupMode::Conditional &&
        (pushes_high || pushes_low)) {
        integral_ = integral_before;
        if (config_.form == PidForm::Positional) {
            controller_candidate = p + integral_ + d;
        } else {
            controller_candidate = controller_output_ +
                                   (p - previous_p_) +
                                   (d - previous_d_);
        }
        raw_output = controller_candidate + input.feedforward;
    }

    const float bounded_output = std::clamp(raw_output,
                                            config_.output_min,
                                            config_.output_max);
    float filtered_output = bounded_output;
    if (config_.output_cutoff_hz > 0.0f) {
        filtered_output = output_filter_.step(bounded_output,
                                              config_.output_cutoff_hz,
                                              input.dt_s);
    } else {
        output_filter_.reset(bounded_output);
    }
    const float slew_output = slew_limiter_.step(filtered_output,
                                                 config_.slew_up_per_s,
                                                 config_.slew_down_per_s,
                                                 input.dt_s);
    const bool slew_limited = differs(slew_output, filtered_output);
    const float actual_output = std::clamp(slew_output,
                                           config_.output_min,
                                           config_.output_max);

    if (config_.anti_windup == AntiWindupMode::BackCalculation) {
        integral_ = std::clamp(
            integral_ + config_.back_calculation_gain *
                            (actual_output - raw_output) * input.dt_s,
            config_.integral_min,
            config_.integral_max);
    }
    if (config_.form == PidForm::Incremental) {
        controller_output_ = controller_candidate;
        if (config_.anti_windup != AntiWindupMode::BackCalculation) {
            controller_output_ = std::clamp(controller_output_,
                                            config_.output_min - input.feedforward,
                                            config_.output_max - input.feedforward);
        } else {
            controller_output_ += config_.back_calculation_gain *
                                  (actual_output - raw_output) * input.dt_s;
        }
    } else {
        controller_output_ = p + integral_ + d;
    }

    previous_measurement_ = input.measurement;
    previous_error_ = error;
    previous_p_ = p;
    previous_d_ = d;
    tracked_output_ = actual_output;

    last_result_ = {};
    last_result_.status = PidStatus::Ok;
    last_result_.output = actual_output;
    last_result_.raw_output = raw_output;
    last_result_.error = error;
    last_result_.p = p;
    last_result_.i = integral_;
    last_result_.d = d;
    last_result_.feedforward = input.feedforward;
    last_result_.dt_s = input.dt_s;
    last_result_.saturated = differs(raw_output, bounded_output) ||
                             differs(slew_output, actual_output);
    last_result_.slew_limited = slew_limited;
    return last_result_;
}

} // namespace SRNOS::Control
