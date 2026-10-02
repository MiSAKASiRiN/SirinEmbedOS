#ifndef SRNOS_MATH_KALMAN_FILTER_HPP
#define SRNOS_MATH_KALMAN_FILTER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "SirinEmbedOS/Algorithms/Math/Matrix.h"

namespace SRNOS::Math {

enum class KalmanStatus {
    Ok,
    InvalidMeasurement,
    MatrixError,
};

template <std::size_t StateN, std::size_t ControlN, std::size_t MeasureN>
class KalmanFilter {
    static_assert(StateN > 0U, "Kalman filter requires state dimensions");
    static_assert(MeasureN > 0U, "Kalman filter requires measurement dimensions");

public:
    using StateVector = std::array<float, StateN>;
    using ControlVector = std::array<float, ControlN>;
    using MeasurementVector = std::array<float, MeasureN>;
    using StateMatrix = std::array<float, StateN * StateN>;
    using ControlMatrix = std::array<float, StateN * ControlN>;
    using MeasurementMatrix = std::array<float, MeasureN * StateN>;
    using MeasurementCovariance = std::array<float, MeasureN * MeasureN>;

    KalmanFilter() noexcept
    {
        setIdentity(transition_, StateN);
        setIdentity(covariance_, StateN);
        setIdentity(process_noise_, StateN);
        setIdentity(measurement_noise_, MeasureN);
    }

    KalmanFilter(const KalmanFilter&) = delete;
    KalmanFilter& operator=(const KalmanFilter&) = delete;
    KalmanFilter(KalmanFilter&&) = delete;
    KalmanFilter& operator=(KalmanFilter&&) = delete;

    void reset(const StateVector& state = StateVector {}) noexcept
    {
        state_ = state;
        predicted_state_ = state;
        setIdentity(covariance_, StateN);
        predicted_covariance_ = covariance_;
    }

    StateVector& state() noexcept { return state_; }
    const StateVector& state() const noexcept { return state_; }
    StateMatrix& transition() noexcept { return transition_; }
    ControlMatrix& controlModel() noexcept { return control_model_; }
    MeasurementMatrix& measurementModel() noexcept { return measurement_model_; }
    StateMatrix& covariance() noexcept { return covariance_; }
    StateMatrix& processNoise() noexcept { return process_noise_; }
    MeasurementCovariance& measurementNoise() noexcept { return measurement_noise_; }

    KalmanStatus predict(const ControlVector& control = ControlVector {}) noexcept
    {
        matrix(transition_matrix_, StateN, StateN, transition_.data());
        matrix(state_matrix_, StateN, 1U, state_.data());
        matrix(predicted_state_matrix_, StateN, 1U, predicted_state_.data());
        if (SRNOS_MatrixMultiplyF32(&transition_matrix_, &state_matrix_,
                             &predicted_state_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        if constexpr (ControlN > 0U) {
            matrix(control_model_matrix_, StateN, ControlN, control_model_.data());
            matrix(control_matrix_, ControlN, 1U, const_cast<float*>(control.data()));
            matrix(control_contribution_matrix_, StateN, 1U,
                   control_contribution_.data());
            if (SRNOS_MatrixMultiplyF32(&control_model_matrix_, &control_matrix_,
                                 &control_contribution_matrix_) != SRNOS_MATRIX_OK) {
                return KalmanStatus::MatrixError;
            }
            for (std::size_t i = 0U; i < StateN; ++i) {
                predicted_state_[i] += control_contribution_[i];
            }
        }

        matrix(covariance_matrix_, StateN, StateN, covariance_.data());
        matrix(transition_transpose_matrix_, StateN, StateN,
               transition_transpose_.data());
        matrix(temp_nn_matrix_, StateN, StateN, temp_nn_.data());
        matrix(predicted_covariance_matrix_, StateN, StateN,
               predicted_covariance_.data());
        if (SRNOS_MatrixTransposeF32(&transition_matrix_, &transition_transpose_matrix_) !=
                SRNOS_MATRIX_OK ||
            SRNOS_MatrixMultiplyF32(&transition_matrix_, &covariance_matrix_,
                             &temp_nn_matrix_) != SRNOS_MATRIX_OK ||
            SRNOS_MatrixMultiplyF32(&temp_nn_matrix_, &transition_transpose_matrix_,
                             &predicted_covariance_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        for (std::size_t i = 0U; i < predicted_covariance_.size(); ++i) {
            predicted_covariance_[i] += process_noise_[i];
        }
        state_ = predicted_state_;
        covariance_ = predicted_covariance_;
        return KalmanStatus::Ok;
    }

    KalmanStatus update(const MeasurementVector& measurement) noexcept
    {
        for (float value : measurement) {
            if (!std::isfinite(value)) {
                return KalmanStatus::InvalidMeasurement;
            }
        }
        matrix(measurement_model_matrix_, MeasureN, StateN,
               measurement_model_.data());
        matrix(measurement_transpose_matrix_, StateN, MeasureN,
               measurement_transpose_.data());
        matrix(state_matrix_, StateN, 1U, state_.data());
        matrix(projected_measurement_matrix_, MeasureN, 1U,
               projected_measurement_.data());
        if (SRNOS_MatrixMultiplyF32(&measurement_model_matrix_, &state_matrix_,
                             &projected_measurement_matrix_) != SRNOS_MATRIX_OK ||
            SRNOS_MatrixTransposeF32(&measurement_model_matrix_,
                              &measurement_transpose_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        for (std::size_t i = 0U; i < MeasureN; ++i) {
            innovation_[i] = measurement[i] - projected_measurement_[i];
        }

        matrix(covariance_matrix_, StateN, StateN, covariance_.data());
        matrix(temp_nm_matrix_, StateN, MeasureN, temp_nm_.data());
        matrix(temp_mm_matrix_, MeasureN, MeasureN, temp_mm_.data());
        if (SRNOS_MatrixMultiplyF32(&covariance_matrix_, &measurement_transpose_matrix_,
                             &temp_nm_matrix_) != SRNOS_MATRIX_OK ||
            SRNOS_MatrixMultiplyF32(&measurement_model_matrix_, &temp_nm_matrix_,
                             &temp_mm_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        for (std::size_t i = 0U; i < temp_mm_.size(); ++i) {
            innovation_covariance_[i] = temp_mm_[i] + measurement_noise_[i];
        }

        if (!invertInnovationCovariance()) {
            return KalmanStatus::MatrixError;
        }
        matrix(innovation_covariance_inverse_matrix_, MeasureN, MeasureN,
               innovation_covariance_inverse_.data());
        matrix(kalman_gain_matrix_, StateN, MeasureN, kalman_gain_.data());
        if (SRNOS_MatrixMultiplyF32(&temp_nm_matrix_,
                             &innovation_covariance_inverse_matrix_,
                             &kalman_gain_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }

        matrix(innovation_matrix_, MeasureN, 1U, innovation_.data());
        matrix(correction_matrix_, StateN, 1U, correction_.data());
        if (SRNOS_MatrixMultiplyF32(&kalman_gain_matrix_, &innovation_matrix_,
                             &correction_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        for (std::size_t i = 0U; i < StateN; ++i) {
            state_[i] += correction_[i];
        }

        matrix(kh_matrix_, StateN, StateN, kh_.data());
        matrix(identity_minus_kh_matrix_, StateN, StateN, identity_minus_kh_.data());
        matrix(updated_covariance_matrix_, StateN, StateN, updated_covariance_.data());
        if (SRNOS_MatrixMultiplyF32(&kalman_gain_matrix_, &measurement_model_matrix_,
                             &kh_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        setIdentity(identity_minus_kh_, StateN);
        for (std::size_t i = 0U; i < kh_.size(); ++i) {
            identity_minus_kh_[i] -= kh_[i];
        }
        if (SRNOS_MatrixMultiplyF32(&identity_minus_kh_matrix_, &covariance_matrix_,
                             &updated_covariance_matrix_) != SRNOS_MATRIX_OK) {
            return KalmanStatus::MatrixError;
        }
        covariance_ = updated_covariance_;
        return KalmanStatus::Ok;
    }

private:
    static void setIdentity(float* data, std::size_t dimension) noexcept
    {
        std::fill(data, data + dimension * dimension, 0.0f);
        for (std::size_t i = 0U; i < dimension; ++i) {
            data[i * dimension + i] = 1.0f;
        }
    }

    template <std::size_t Size>
    static void setIdentity(std::array<float, Size>& data,
                            std::size_t dimension) noexcept
    {
        setIdentity(data.data(), dimension);
    }

    static void matrix(SRNOS_MatrixF32& instance,
                       std::size_t rows,
                       std::size_t columns,
                       float* data) noexcept
    {
        SRNOS_MatrixInitF32(&instance,
                         static_cast<uint16_t>(rows),
                         static_cast<uint16_t>(columns),
                         data);
    }

    bool invertInnovationCovariance() noexcept
    {
        temp_mm_ = innovation_covariance_;
        setIdentity(innovation_covariance_inverse_, MeasureN);
        for (std::size_t pivot = 0U; pivot < MeasureN; ++pivot) {
            std::size_t best = pivot;
            for (std::size_t row = pivot + 1U; row < MeasureN; ++row) {
                if (std::fabs(temp_mm_[row * MeasureN + pivot]) >
                    std::fabs(temp_mm_[best * MeasureN + pivot])) {
                    best = row;
                }
            }
            if (std::fabs(temp_mm_[best * MeasureN + pivot]) < 1.0e-12f) {
                return false;
            }
            if (best != pivot) {
                for (std::size_t column = 0U; column < MeasureN; ++column) {
                    std::swap(temp_mm_[pivot * MeasureN + column],
                              temp_mm_[best * MeasureN + column]);
                    std::swap(innovation_covariance_inverse_[pivot * MeasureN + column],
                              innovation_covariance_inverse_[best * MeasureN + column]);
                }
            }
            const float divisor = temp_mm_[pivot * MeasureN + pivot];
            for (std::size_t column = 0U; column < MeasureN; ++column) {
                temp_mm_[pivot * MeasureN + column] /= divisor;
                innovation_covariance_inverse_[pivot * MeasureN + column] /= divisor;
            }
            for (std::size_t row = 0U; row < MeasureN; ++row) {
                if (row == pivot) {
                    continue;
                }
                const float factor = temp_mm_[row * MeasureN + pivot];
                for (std::size_t column = 0U; column < MeasureN; ++column) {
                    temp_mm_[row * MeasureN + column] -=
                        factor * temp_mm_[pivot * MeasureN + column];
                    innovation_covariance_inverse_[row * MeasureN + column] -=
                        factor * innovation_covariance_inverse_[pivot * MeasureN + column];
                }
            }
        }
        return true;
    }

    StateVector state_ {};
    StateVector predicted_state_ {};
    StateMatrix transition_ {};
    ControlMatrix control_model_ {};
    MeasurementMatrix measurement_model_ {};
    StateMatrix covariance_ {};
    StateMatrix predicted_covariance_ {};
    StateMatrix process_noise_ {};
    MeasurementCovariance measurement_noise_ {};

    StateMatrix transition_transpose_ {};
    StateMatrix temp_nn_ {};
    std::array<float, StateN * MeasureN> measurement_transpose_ {};
    std::array<float, StateN * MeasureN> temp_nm_ {};
    MeasurementVector projected_measurement_ {};
    MeasurementVector innovation_ {};
    MeasurementCovariance temp_mm_ {};
    MeasurementCovariance innovation_covariance_ {};
    MeasurementCovariance innovation_covariance_inverse_ {};
    std::array<float, StateN * MeasureN> kalman_gain_ {};
    StateVector correction_ {};
    StateMatrix kh_ {};
    StateMatrix identity_minus_kh_ {};
    StateMatrix updated_covariance_ {};
    StateVector control_contribution_ {};

    SRNOS_MatrixF32 transition_matrix_ {};
    SRNOS_MatrixF32 transition_transpose_matrix_ {};
    SRNOS_MatrixF32 state_matrix_ {};
    SRNOS_MatrixF32 predicted_state_matrix_ {};
    SRNOS_MatrixF32 control_model_matrix_ {};
    SRNOS_MatrixF32 control_matrix_ {};
    SRNOS_MatrixF32 control_contribution_matrix_ {};
    SRNOS_MatrixF32 covariance_matrix_ {};
    SRNOS_MatrixF32 predicted_covariance_matrix_ {};
    SRNOS_MatrixF32 temp_nn_matrix_ {};
    SRNOS_MatrixF32 measurement_model_matrix_ {};
    SRNOS_MatrixF32 measurement_transpose_matrix_ {};
    SRNOS_MatrixF32 projected_measurement_matrix_ {};
    SRNOS_MatrixF32 temp_nm_matrix_ {};
    SRNOS_MatrixF32 temp_mm_matrix_ {};
    SRNOS_MatrixF32 innovation_covariance_inverse_matrix_ {};
    SRNOS_MatrixF32 kalman_gain_matrix_ {};
    SRNOS_MatrixF32 innovation_matrix_ {};
    SRNOS_MatrixF32 correction_matrix_ {};
    SRNOS_MatrixF32 kh_matrix_ {};
    SRNOS_MatrixF32 identity_minus_kh_matrix_ {};
    SRNOS_MatrixF32 updated_covariance_matrix_ {};
};

} // namespace SRNOS::Math

#endif
