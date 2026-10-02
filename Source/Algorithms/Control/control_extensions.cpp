#include "SirinEmbedOS/Algorithms/Control/control_extensions.hpp"

#include <algorithm>
#include <cmath>

namespace SRNOS::Control {
namespace {

constexpr std::array<float, 49> kKpRules {{
    3, 3, 2, 2, 1, 0, 0,  3, 3, 2, 1, 1, 0, 1,
    2, 2, 2, 1, 0, 1, 1,  2, 2, 1, 0, 1, 2, 2,
    1, 1, 0, 1, 1, 2, 2,  1, 0, 1, 2, 2, 2, 3,
    0, 0, 2, 2, 2, 3, 3,
}};
constexpr std::array<float, 49> kKiRules {{
    3, 3, 2, 2, 1, 0, 0,  3, 3, 2, 1, 1, 0, 0,
    3, 2, 2, 1, 0, 1, 1,  2, 2, 1, 0, 1, 2, 2,
    1, 1, 0, 1, 1, 2, 3,  0, 0, 1, 1, 2, 3, 3,
    0, 0, 1, 2, 2, 3, 3,
}};
constexpr std::array<float, 49> kKdRules {{
    1, 1, 3, 3, 3, 2, 1,  1, 1, 3, 2, 2, 1, 0,
    0, 1, 2, 2, 1, 1, 0,  0, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0,  3, 1, 1, 1, 1, 1, 3,
    3, 2, 2, 2, 1, 1, 3,
}};

bool finite(float value) noexcept { return std::isfinite(value); }

float normalizedIndex(float value, float step) noexcept
{
    return std::clamp(value / step + 3.0f, 0.0f, 6.0f);
}

} // namespace

bool FuzzyGainScheduler::configure(const FuzzySchedulerConfig& config) noexcept
{
    if (!finite(config.base_gains.kp) || !finite(config.base_gains.ki) ||
        !finite(config.base_gains.kd) || !finite(config.kp_ratio) ||
        !finite(config.ki_ratio) || !finite(config.kd_ratio) ||
        !finite(config.error_step) || config.error_step <= 0.0f ||
        !finite(config.error_rate_step) || config.error_rate_step <= 0.0f) {
        configured_ = false;
        return false;
    }
    config_ = config;
    configured_ = true;
    reset();
    return true;
}

void FuzzyGainScheduler::reset(float error) noexcept
{
    previous_error_ = finite(error) ? error : 0.0f;
    initialized_ = false;
}

float FuzzyGainScheduler::interpolate(const std::array<float, 49>& table,
                                      float error_index,
                                      float rate_index) noexcept
{
    const auto e0 = static_cast<std::size_t>(std::floor(error_index));
    const auto r0 = static_cast<std::size_t>(std::floor(rate_index));
    const std::size_t e1 = std::min<std::size_t>(e0 + 1U, 6U);
    const std::size_t r1 = std::min<std::size_t>(r0 + 1U, 6U);
    const float ew = error_index - static_cast<float>(e0);
    const float rw = rate_index - static_cast<float>(r0);
    const float left = table[e0 * 7U + r0] * (1.0f - rw) +
                       table[e0 * 7U + r1] * rw;
    const float right = table[e1 * 7U + r0] * (1.0f - rw) +
                        table[e1 * 7U + r1] * rw;
    return left * (1.0f - ew) + right * ew;
}

PidGains FuzzyGainScheduler::step(float error, float dt_s) noexcept
{
    if (!configured_ || !finite(error) || !finite(dt_s) || dt_s <= 0.0f) {
        return config_.base_gains;
    }
    const float rate = initialized_ ? (error - previous_error_) / dt_s : 0.0f;
    initialized_ = true;
    previous_error_ = error;
    const float error_index = normalizedIndex(error, config_.error_step);
    const float rate_index = normalizedIndex(rate, config_.error_rate_step);
    return {
        config_.base_gains.kp + config_.kp_ratio *
                                    interpolate(kKpRules, error_index, rate_index),
        config_.base_gains.ki + config_.ki_ratio *
                                    interpolate(kKiRules, error_index, rate_index),
        config_.base_gains.kd + config_.kd_ratio *
                                    interpolate(kKdRules, error_index, rate_index),
    };
}

PidStatus FuzzyPid::configure(const PidConfig& pid_config,
                              const FuzzySchedulerConfig& fuzzy_config) noexcept
{
    if (!scheduler_.configure(fuzzy_config)) {
        return PidStatus::InvalidConfig;
    }
    return pid_.configure(pid_config);
}

void FuzzyPid::reset(float measurement, float tracked_output) noexcept
{
    scheduler_.reset();
    pid_.reset(measurement, tracked_output);
}

void FuzzyPid::track(float setpoint, float measurement, float actual_output) noexcept
{
    scheduler_.reset(setpoint - measurement);
    pid_.track(setpoint, measurement, actual_output);
}

PidResult FuzzyPid::step(const PidInput& input) noexcept
{
    const PidGains gains = scheduler_.step(input.setpoint - input.measurement,
                                           input.dt_s);
    const PidStatus status = pid_.setGains(gains);
    if (status != PidStatus::Ok) {
        return pid_.step(input);
    }
    return pid_.step(input);
}

bool Feedforward::configure(const FeedforwardConfig& config) noexcept
{
    const bool valid = finite(config.coefficients[0]) &&
                       finite(config.coefficients[1]) &&
                       finite(config.coefficients[2]) &&
                       finite(config.output_min) && finite(config.output_max) &&
                       config.output_min < config.output_max &&
                       finite(config.derivative_cutoff_hz) &&
                       config.derivative_cutoff_hz >= 0.0f;
    configured_ = valid;
    if (valid) {
        config_ = config;
        reset();
    }
    return valid;
}

void Feedforward::reset(float reference) noexcept
{
    reference_history_.reset();
    derivative_history_.reset();
    derivative_filter_.reset(0.0f);
    if (finite(reference)) {
        reference_history_.push(reference, 1.0f);
    }
}

float Feedforward::step(float reference, float dt_s) noexcept
{
    if (!configured_ || !finite(reference) || !finite(dt_s) || dt_s <= 0.0f ||
        !reference_history_.push(reference, dt_s)) {
        return 0.0f;
    }
    float derivative = reference_history_.slope();
    if (config_.derivative_cutoff_hz > 0.0f) {
        derivative = derivative_filter_.step(derivative,
                                             config_.derivative_cutoff_hz,
                                             dt_s);
    }
    derivative_history_.push(derivative, dt_s);
    const float second_derivative = derivative_history_.slope();
    const float output = config_.coefficients[0] * reference +
                         config_.coefficients[1] * derivative +
                         config_.coefficients[2] * second_derivative;
    return std::clamp(output, config_.output_min, config_.output_max);
}

bool LinearDisturbanceObserver::configure(
    const LinearDisturbanceObserverConfig& config) noexcept
{
    const bool valid = finite(config.coefficients[0]) &&
                       finite(config.coefficients[1]) &&
                       finite(config.coefficients[2]) &&
                       finite(config.disturbance_min) &&
                       finite(config.disturbance_max) &&
                       config.disturbance_min < config.disturbance_max &&
                       finite(config.deadband) && config.deadband >= 0.0f &&
                       finite(config.cutoff_hz) && config.cutoff_hz >= 0.0f;
    configured_ = valid;
    if (valid) {
        config_ = config;
        reset();
    }
    return valid;
}

void LinearDisturbanceObserver::reset(float measurement) noexcept
{
    measurement_history_.reset();
    derivative_history_.reset();
    output_filter_.reset(0.0f);
    if (finite(measurement)) {
        measurement_history_.push(measurement, 1.0f);
    }
}

float LinearDisturbanceObserver::step(float measurement,
                                      float plant_input,
                                      float dt_s) noexcept
{
    if (!configured_ || !finite(measurement) || !finite(plant_input) ||
        !finite(dt_s) || dt_s <= 0.0f ||
        !measurement_history_.push(measurement, dt_s)) {
        return 0.0f;
    }
    const float derivative = measurement_history_.slope();
    derivative_history_.push(derivative, dt_s);
    const float second_derivative = derivative_history_.slope();
    float disturbance = config_.coefficients[0] * measurement +
                        config_.coefficients[1] * derivative +
                        config_.coefficients[2] * second_derivative -
                        plant_input;
    if (std::fabs(disturbance) <= config_.deadband) {
        disturbance = 0.0f;
    }
    disturbance = std::clamp(disturbance,
                             config_.disturbance_min,
                             config_.disturbance_max);
    return config_.cutoff_hz > 0.0f
               ? output_filter_.step(disturbance, config_.cutoff_hz, dt_s)
               : disturbance;
}

bool TrackingDifferentiator::configure(
    const TrackingDifferentiatorConfig& config) noexcept
{
    configured_ = finite(config.response) && config.response > 0.0f &&
                  finite(config.acceleration_limit) &&
                  config.acceleration_limit > 0.0f;
    if (configured_) {
        config_ = config;
        reset();
    }
    return configured_;
}

void TrackingDifferentiator::reset(float value) noexcept
{
    result_ = {};
    result_.value = finite(value) ? value : 0.0f;
}

TrackingDifferentiatorResult TrackingDifferentiator::step(float input,
                                                           float dt_s) noexcept
{
    if (!configured_ || !finite(input) || !finite(dt_s) || dt_s <= 0.0f) {
        return result_;
    }
    const float requested_acceleration = config_.response * config_.response *
                                             (input - result_.value) -
                                         2.0f * config_.response *
                                             result_.derivative;
    result_.acceleration = std::clamp(requested_acceleration,
                                     -config_.acceleration_limit,
                                     config_.acceleration_limit);
    result_.derivative += result_.acceleration * dt_s;
    result_.value += result_.derivative * dt_s;
    return result_;
}

} // namespace SRNOS::Control
