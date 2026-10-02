#include "SirinEmbedOS/Devices/IMU/quaternion_ekf.hpp"

#include <algorithm>
#include <cmath>

#include "SirinEmbedOS/Algorithms/Math/angle.hpp"

namespace SRNOS::IMU {
namespace {

constexpr std::size_t kStateDimension = 7U;

constexpr std::size_t at(std::size_t row, std::size_t column) noexcept
{
    return row * kStateDimension + column;
}

bool finite(float value) noexcept { return std::isfinite(value); }

} // namespace

QuaternionEkfStatus QuaternionEkf::configure(
    const QuaternionEkfConfig& config) noexcept
{
    const bool valid = finite(config.quaternion_process_noise) &&
                       config.quaternion_process_noise >= 0.0f &&
                       finite(config.gyro_bias_process_noise) &&
                       config.gyro_bias_process_noise >= 0.0f &&
                       finite(config.accelerometer_measurement_noise) &&
                       config.accelerometer_measurement_noise > 0.0f &&
                       finite(config.accelerometer_cutoff_hz) &&
                       config.accelerometer_cutoff_hz >= 0.0f &&
                       finite(config.min_dt_s) && finite(config.max_dt_s) &&
                       config.min_dt_s > 0.0f &&
                       config.max_dt_s >= config.min_dt_s;
    if (!valid) {
        configured_ = false;
        result_.status = QuaternionEkfStatus::InvalidConfig;
        return result_.status;
    }
    config_ = config;
    configured_ = true;
    reset();
    return QuaternionEkfStatus::Ok;
}

bool QuaternionEkf::normalizeQuaternion(std::array<float, 4>& quaternion) noexcept
{
    const float norm = std::sqrt(quaternion[0] * quaternion[0] +
                                 quaternion[1] * quaternion[1] +
                                 quaternion[2] * quaternion[2] +
                                 quaternion[3] * quaternion[3]);
    if (!finite(norm) || norm <= 1.0e-8f) {
        return false;
    }
    for (float& value : quaternion) {
        value /= norm;
    }
    return true;
}

void QuaternionEkf::reset(const std::array<float, 4>& quaternion) noexcept
{
    std::array<float, 4> normalized = quaternion;
    if (!normalizeQuaternion(normalized)) {
        normalized = {{1.0f, 0.0f, 0.0f, 0.0f}};
    }
    SRNOS::Math::KalmanFilter<7, 0, 3>::StateVector state {};
    std::copy(normalized.begin(), normalized.end(), state.begin());
    filter_.reset(state);
    filter_.transition().fill(0.0f);
    for (std::size_t index = 0U; index < kStateDimension; ++index) {
        filter_.transition()[at(index, index)] = 1.0f;
    }
    for (auto& low_pass : accelerometer_filters_) {
        low_pass.reset(0.0f);
    }
    result_ = {};
    result_.quaternion = normalized;
    result_.status = configured_ ? QuaternionEkfStatus::Ok
                                 : QuaternionEkfStatus::InvalidConfig;
    yaw_initialized_ = false;
    previous_yaw_deg_ = 0.0f;
    yaw_turns_ = 0;
}

QuaternionEkfResult QuaternionEkf::update(float gx,
                                          float gy,
                                          float gz,
                                          float ax,
                                          float ay,
                                          float az,
                                          float dt_s) noexcept
{
    if (!configured_) {
        result_.status = QuaternionEkfStatus::InvalidConfig;
        return result_;
    }
    if (!finite(gx) || !finite(gy) || !finite(gz) ||
        !finite(ax) || !finite(ay) || !finite(az)) {
        result_.status = QuaternionEkfStatus::InvalidInput;
        return result_;
    }
    if (!finite(dt_s) || dt_s < config_.min_dt_s || dt_s > config_.max_dt_s) {
        result_.status = QuaternionEkfStatus::InvalidDt;
        return result_;
    }

    const auto prior = filter_.state();
    const float q0 = prior[0];
    const float q1 = prior[1];
    const float q2 = prior[2];
    const float q3 = prior[3];
    const float half_dt = 0.5f * dt_s;
    auto& transition = filter_.transition();
    transition.fill(0.0f);
    for (std::size_t index = 0U; index < kStateDimension; ++index) {
        transition[at(index, index)] = 1.0f;
    }
    transition[at(0, 1)] = -gx * half_dt;
    transition[at(0, 2)] = -gy * half_dt;
    transition[at(0, 3)] = -gz * half_dt;
    transition[at(1, 0)] = gx * half_dt;
    transition[at(1, 2)] = gz * half_dt;
    transition[at(1, 3)] = -gy * half_dt;
    transition[at(2, 0)] = gy * half_dt;
    transition[at(2, 1)] = -gz * half_dt;
    transition[at(2, 3)] = gx * half_dt;
    transition[at(3, 0)] = gz * half_dt;
    transition[at(3, 1)] = gy * half_dt;
    transition[at(3, 2)] = -gx * half_dt;

    transition[at(0, 4)] = q1 * half_dt;
    transition[at(0, 5)] = q2 * half_dt;
    transition[at(0, 6)] = q3 * half_dt;
    transition[at(1, 4)] = -q0 * half_dt;
    transition[at(1, 5)] = q3 * half_dt;
    transition[at(1, 6)] = -q2 * half_dt;
    transition[at(2, 4)] = -q3 * half_dt;
    transition[at(2, 5)] = -q0 * half_dt;
    transition[at(2, 6)] = q1 * half_dt;
    transition[at(3, 4)] = q2 * half_dt;
    transition[at(3, 5)] = -q1 * half_dt;
    transition[at(3, 6)] = -q0 * half_dt;

    auto& process_noise = filter_.processNoise();
    process_noise.fill(0.0f);
    for (std::size_t index = 0U; index < 4U; ++index) {
        process_noise[at(index, index)] =
            config_.quaternion_process_noise * dt_s;
    }
    for (std::size_t index = 4U; index < 7U; ++index) {
        process_noise[at(index, index)] =
            config_.gyro_bias_process_noise * dt_s;
    }
    if (filter_.predict() != SRNOS::Math::KalmanStatus::Ok) {
        updateResult(QuaternionEkfStatus::MatrixError);
        return result_;
    }

    std::array<float, 4> predicted_quaternion {{
        filter_.state()[0], filter_.state()[1],
        filter_.state()[2], filter_.state()[3],
    }};
    if (!normalizeQuaternion(predicted_quaternion)) {
        result_.status = QuaternionEkfStatus::MatrixError;
        return result_;
    }
    std::copy(predicted_quaternion.begin(), predicted_quaternion.end(),
              filter_.state().begin());

    std::array<float, 3> acceleration {{ax, ay, az}};
    if (config_.accelerometer_cutoff_hz > 0.0f) {
        for (std::size_t index = 0U; index < acceleration.size(); ++index) {
            acceleration[index] = accelerometer_filters_[index].step(
                acceleration[index], config_.accelerometer_cutoff_hz, dt_s);
        }
    }
    const float acceleration_norm = std::sqrt(
        acceleration[0] * acceleration[0] +
        acceleration[1] * acceleration[1] +
        acceleration[2] * acceleration[2]);
    if (!finite(acceleration_norm) || acceleration_norm <= 1.0e-6f) {
        updateResult(QuaternionEkfStatus::PredictOnly);
        return result_;
    }
    for (float& value : acceleration) {
        value /= acceleration_norm;
    }

    const float a0 = predicted_quaternion[0];
    const float a1 = predicted_quaternion[1];
    const float a2 = predicted_quaternion[2];
    const float a3 = predicted_quaternion[3];
    const std::array<float, 3> predicted_gravity {{
        2.0f * (a1 * a3 - a0 * a2),
        2.0f * (a0 * a1 + a2 * a3),
        a0 * a0 - a1 * a1 - a2 * a2 + a3 * a3,
    }};
    auto& measurement_model = filter_.measurementModel();
    measurement_model.fill(0.0f);
    measurement_model[0] = -2.0f * a2;
    measurement_model[1] = 2.0f * a3;
    measurement_model[2] = -2.0f * a0;
    measurement_model[3] = 2.0f * a1;
    measurement_model[7] = 2.0f * a1;
    measurement_model[8] = 2.0f * a0;
    measurement_model[9] = 2.0f * a3;
    measurement_model[10] = 2.0f * a2;
    measurement_model[14] = 2.0f * a0;
    measurement_model[15] = -2.0f * a1;
    measurement_model[16] = -2.0f * a2;
    measurement_model[17] = 2.0f * a3;

    auto& measurement_noise = filter_.measurementNoise();
    measurement_noise.fill(0.0f);
    measurement_noise[0] = config_.accelerometer_measurement_noise;
    measurement_noise[4] = config_.accelerometer_measurement_noise;
    measurement_noise[8] = config_.accelerometer_measurement_noise;

    std::array<float, 3> linear_projection {};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < kStateDimension; ++column) {
            linear_projection[row] += measurement_model[row * kStateDimension + column] *
                                      filter_.state()[column];
        }
    }
    SRNOS::Math::KalmanFilter<7, 0, 3>::MeasurementVector pseudo_measurement {};
    for (std::size_t index = 0U; index < 3U; ++index) {
        pseudo_measurement[index] = acceleration[index] - predicted_gravity[index] +
                                    linear_projection[index];
    }
    if (filter_.update(pseudo_measurement) != SRNOS::Math::KalmanStatus::Ok) {
        updateResult(QuaternionEkfStatus::MatrixError);
        return result_;
    }
    std::array<float, 4> corrected_quaternion {{
        filter_.state()[0], filter_.state()[1],
        filter_.state()[2], filter_.state()[3],
    }};
    if (!normalizeQuaternion(corrected_quaternion)) {
        result_.status = QuaternionEkfStatus::MatrixError;
        return result_;
    }
    std::copy(corrected_quaternion.begin(), corrected_quaternion.end(),
              filter_.state().begin());
    updateResult(QuaternionEkfStatus::Ok);
    return result_;
}

void QuaternionEkf::updateResult(QuaternionEkfStatus status) noexcept
{
    const auto& state = filter_.state();
    result_.quaternion = {{state[0], state[1], state[2], state[3]}};
    result_.gyro_bias = {{state[4], state[5], state[6]}};
    const float q0 = state[0];
    const float q1 = state[1];
    const float q2 = state[2];
    const float q3 = state[3];
    constexpr float radians_to_degrees = 180.0f / SRNOS::Math::kPi;
    result_.roll_deg = std::atan2(2.0f * (q0 * q1 + q2 * q3),
                                  1.0f - 2.0f * (q1 * q1 + q2 * q2)) *
                       radians_to_degrees;
    result_.pitch_deg = std::asin(std::clamp(
                            2.0f * (q0 * q2 - q3 * q1), -1.0f, 1.0f)) *
                        radians_to_degrees;
    result_.yaw_deg = std::atan2(2.0f * (q0 * q3 + q1 * q2),
                                 1.0f - 2.0f * (q2 * q2 + q3 * q3)) *
                      radians_to_degrees;
    if (yaw_initialized_) {
        const float delta = result_.yaw_deg - previous_yaw_deg_;
        if (delta > 180.0f) {
            --yaw_turns_;
        } else if (delta < -180.0f) {
            ++yaw_turns_;
        }
    }
    yaw_initialized_ = true;
    previous_yaw_deg_ = result_.yaw_deg;
    result_.yaw_total_deg = result_.yaw_deg + 360.0f * static_cast<float>(yaw_turns_);
    result_.status = status;
    ++result_.update_count;
}

} // namespace SRNOS::IMU
