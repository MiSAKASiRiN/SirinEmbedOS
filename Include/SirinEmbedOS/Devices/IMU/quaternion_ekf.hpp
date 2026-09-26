#ifndef OMX_LIB_IMU_QUATERNION_EKF_HPP
#define OMX_LIB_IMU_QUATERNION_EKF_HPP

#include <array>
#include <cstdint>

#include "SirinEmbedOS/Algorithms/Math/kalman_filter.hpp"
#include "SirinEmbedOS/Algorithms/Math/signal.hpp"

namespace omx::imu {

struct QuaternionEkfConfig {
    float quaternion_process_noise {10.0f};
    float gyro_bias_process_noise {0.001f};
    float accelerometer_measurement_noise {10000000.0f};
    float accelerometer_cutoff_hz {0.0f};
    float min_dt_s {1.0e-6f};
    float max_dt_s {0.1f};
};

enum class QuaternionEkfStatus {
    Ok,
    InvalidConfig,
    InvalidInput,
    InvalidDt,
    PredictOnly,
    MatrixError,
};

struct QuaternionEkfResult {
    QuaternionEkfStatus status {QuaternionEkfStatus::InvalidConfig};
    std::array<float, 4> quaternion {{1.0f, 0.0f, 0.0f, 0.0f}};
    std::array<float, 3> gyro_bias {};
    float roll_deg {0.0f};
    float pitch_deg {0.0f};
    float yaw_deg {0.0f};
    float yaw_total_deg {0.0f};
    std::uint64_t update_count {0U};
};

class QuaternionEkf {
public:
    QuaternionEkfStatus configure(const QuaternionEkfConfig&) noexcept;
    void reset(const std::array<float, 4>& quaternion =
                   {{1.0f, 0.0f, 0.0f, 0.0f}}) noexcept;
    QuaternionEkfResult update(float gx,
                               float gy,
                               float gz,
                               float ax,
                               float ay,
                               float az,
                               float dt_s) noexcept;
    const QuaternionEkfResult& result() const noexcept { return result_; }

private:
    static bool normalizeQuaternion(std::array<float, 4>&) noexcept;
    void updateResult(QuaternionEkfStatus) noexcept;

    QuaternionEkfConfig config_ {};
    QuaternionEkfResult result_ {};
    algo::KalmanFilter<7, 0, 3> filter_ {};
    std::array<algo::FirstOrderLowPass, 3> accelerometer_filters_ {};
    bool configured_ {false};
    bool yaw_initialized_ {false};
    float previous_yaw_deg_ {0.0f};
    std::int32_t yaw_turns_ {0};
};

} // namespace omx::imu

#endif
