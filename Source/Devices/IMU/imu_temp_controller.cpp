#include "SirinEmbedOS/Devices/IMU/imu_temp_controller.hpp"

#include <algorithm>
#include <cmath>

namespace omx::imu {
namespace {

bool finite(float value) noexcept { return std::isfinite(value); }

} // namespace

bool ImuTemperatureController::configure(
    const TempControllerConfig& config) noexcept
{
    const bool valid = finite(config.target_c) &&
                       finite(config.maximum_output) &&
                       config.maximum_output > 0.0f &&
                       finite(config.maximum_integral) &&
                       config.maximum_integral >= 0.0f &&
                       finite(config.stable_delta_c) &&
                       config.stable_delta_c > 0.0f &&
                       config.stable_time_ms > 0U &&
                       finite(config.over_temperature_margin_c) &&
                       config.over_temperature_margin_c > 0.0f &&
                       finite(config.min_dt_s) && finite(config.max_dt_s) &&
                       config.min_dt_s > 0.0f &&
                       config.max_dt_s >= config.min_dt_s;
    if (!valid) {
        configured_ = false;
        result_.status = TempControllerStatus::NotConfigured;
        return false;
    }
    control::PidConfig pid_config {};
    pid_config.form = control::PidForm::Incremental;
    pid_config.gains = config.gains;
    pid_config.output_min = 0.0f;
    pid_config.output_max = config.maximum_output;
    pid_config.integral_min = 0.0f;
    pid_config.integral_max = config.maximum_integral;
    pid_config.derivative_mode = control::DerivativeMode::OnMeasurement;
    pid_config.anti_windup = control::AntiWindupMode::BackCalculation;
    pid_config.back_calculation_gain = 1.0f;
    pid_config.min_dt_s = config.min_dt_s;
    pid_config.max_dt_s = config.max_dt_s;
    pid_config.fail_output = 0.0f;
    if (pid_.configure(pid_config) != control::PidStatus::Ok) {
        configured_ = false;
        result_.status = TempControllerStatus::NotConfigured;
        return false;
    }
    config_ = config;
    configured_ = true;
    reset();
    return true;
}

void ImuTemperatureController::reset(float current_c) noexcept
{
    if (!finite(current_c)) {
        current_c = 25.0f;
    }
    pid_.reset(current_c, 0.0f);
    result_ = {};
    result_.status = configured_ ? TempControllerStatus::Ok
                                 : TempControllerStatus::NotConfigured;
    result_.state = TempControllerState::Cold;
    result_.current_c = current_c;
    result_.target_c = config_.target_c;
    result_.enabled = configured_;
    in_range_timer_active_ = false;
    in_range_start_ms_ = 0U;
}

void ImuTemperatureController::updateThermalState(float current_c,
                                                  std::uint32_t now_ms) noexcept
{
    const float error = std::fabs(current_c - config_.target_c);
    if (error > config_.stable_delta_c) {
        result_.state = TempControllerState::Warmup;
        in_range_timer_active_ = false;
        return;
    }
    if (!in_range_timer_active_) {
        in_range_timer_active_ = true;
        in_range_start_ms_ = now_ms;
        result_.state = TempControllerState::Warmup;
        return;
    }
    result_.state = (now_ms - in_range_start_ms_) >= config_.stable_time_ms
                        ? TempControllerState::Stable
                        : TempControllerState::Warmup;
}

void ImuTemperatureController::trackZero(float current_c, float dt_s) noexcept
{
    pid_.track(config_.target_c, current_c, 0.0f);
    result_.pid = pid_.lastResult();
    result_.pid.dt_s = dt_s;
    result_.output = 0.0f;
    result_.duty = 0.0f;
}

TempControllerResult ImuTemperatureController::step(float current_c,
                                                     float dt_s,
                                                     std::uint32_t now_ms) noexcept
{
    if (!configured_) {
        result_.status = TempControllerStatus::NotConfigured;
        return result_;
    }
    if (!finite(current_c)) {
        const control::PidResult failed_pid =
            pid_.step({config_.target_c, current_c, 0.0f, dt_s});
        latchFault();
        result_.pid = failed_pid;
        result_.status = TempControllerStatus::InvalidInput;
        return result_;
    }
    if (!finite(dt_s) || dt_s < config_.min_dt_s || dt_s > config_.max_dt_s) {
        const control::PidResult failed_pid =
            pid_.step({config_.target_c, current_c, 0.0f, dt_s});
        latchFault();
        result_.pid = failed_pid;
        result_.status = TempControllerStatus::InvalidDt;
        return result_;
    }
    result_.current_c = current_c;
    result_.target_c = config_.target_c;
    updateThermalState(current_c, now_ms);

    if (result_.fault_latched) {
        trackZero(current_c, dt_s);
        result_.status = TempControllerStatus::FaultLatched;
        return result_;
    }
    if (!result_.enabled) {
        trackZero(current_c, dt_s);
        result_.status = TempControllerStatus::Disabled;
        return result_;
    }
    if (current_c > config_.target_c + config_.over_temperature_margin_c) {
        latchFault();
        trackZero(current_c, dt_s);
        result_.status = TempControllerStatus::FaultLatched;
        return result_;
    }
    if (current_c > config_.target_c) {
        trackZero(current_c, dt_s);
        result_.status = TempControllerStatus::Ok;
        return result_;
    }

    const float absolute_error = std::fabs(config_.target_c - current_c);
    if (result_.state != TempControllerState::Stable &&
        absolute_error > config_.stable_delta_c) {
        pid_.track(config_.target_c, current_c, config_.maximum_output);
        result_.pid = pid_.lastResult();
        result_.pid.dt_s = dt_s;
        result_.output = config_.maximum_output;
        result_.duty = 1.0f;
        result_.status = TempControllerStatus::Ok;
        return result_;
    }

    result_.pid = pid_.step({config_.target_c, current_c, 0.0f, dt_s});
    if (result_.pid.status != control::PidStatus::Ok) {
        const control::PidResult failed_pid = result_.pid;
        latchFault();
        result_.pid = failed_pid;
        result_.status = failed_pid.status == control::PidStatus::InvalidDt
                             ? TempControllerStatus::InvalidDt
                             : TempControllerStatus::InvalidInput;
        return result_;
    }
    result_.output = std::clamp(result_.pid.output,
                                0.0f,
                                config_.maximum_output);
    result_.duty = result_.output / config_.maximum_output;
    result_.status = TempControllerStatus::Ok;
    return result_;
}

void ImuTemperatureController::setTarget(float target_c) noexcept
{
    if (!configured_ || !finite(target_c)) {
        return;
    }
    config_.target_c = target_c;
    result_.target_c = target_c;
    result_.state = TempControllerState::Warmup;
    in_range_timer_active_ = false;
    pid_.reset(result_.current_c, 0.0f);
}

void ImuTemperatureController::setEnabled(bool enabled) noexcept
{
    result_.enabled = configured_ && enabled;
    if (!result_.enabled) {
        trackZero(result_.current_c);
        result_.status = TempControllerStatus::Disabled;
    }
}

void ImuTemperatureController::latchFault() noexcept
{
    result_.fault_latched = true;
    result_.enabled = false;
    pid_.reset(result_.current_c, 0.0f);
    result_.output = 0.0f;
    result_.duty = 0.0f;
    result_.status = TempControllerStatus::FaultLatched;
}

void ImuTemperatureController::clearFault() noexcept
{
    result_.fault_latched = false;
    pid_.reset(result_.current_c, 0.0f);
    result_.status = result_.enabled ? TempControllerStatus::Ok
                                    : TempControllerStatus::Disabled;
}

} // namespace omx::imu
