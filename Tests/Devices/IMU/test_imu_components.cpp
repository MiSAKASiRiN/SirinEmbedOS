#include <cassert>
#include <cmath>
#include <limits>

#include "SirinEmbedOS/Devices/IMU/imu_temp_controller.hpp"
#include "SirinEmbedOS/Devices/IMU/quaternion_ekf.hpp"

int main()
{
    omx::imu::TempControllerConfig heater_config {};
    heater_config.target_c = 55.0f;
    heater_config.gains = {100.0f, 50000.0f, 0.01f};
    heater_config.maximum_output = 500.0f;
    heater_config.maximum_integral = 300.0f;
    heater_config.stable_delta_c = 5.0f;
    heater_config.stable_time_ms = 1000U;

    omx::control::Pid heater_pid;
    omx::imu::ImuTemperatureController heater {heater_pid};
    assert(heater.configure(heater_config));
    auto result = heater.step(25.0f, 0.001f, 1U);
    assert(result.status == omx::imu::TempControllerStatus::Ok);
    assert(result.state == omx::imu::TempControllerState::Warmup);
    assert(result.output == 500.0f && result.duty == 1.0f);

    result = heater.step(54.0f, 0.001f, 10U);
    assert(result.output >= 0.0f && result.output <= 500.0f);
    result = heater.step(54.0f, 0.001f, 1010U);
    assert(result.state == omx::imu::TempControllerState::Stable);

    result = heater.step(56.0f, 0.001f, 1011U);
    assert(result.output == 0.0f);
    heater.setEnabled(false);
    result = heater.step(50.0f, 0.001f, 1012U);
    assert(result.status == omx::imu::TempControllerStatus::Disabled);
    assert(result.output == 0.0f);
    heater.setEnabled(true);
    result = heater.step(70.0f, 0.001f, 1013U);
    assert(result.fault_latched && result.output == 0.0f);
    heater.clearFault();
    heater.setEnabled(true);
    result = heater.step(54.0f, 0.0f, 1014U);
    assert(result.status == omx::imu::TempControllerStatus::InvalidDt);
    assert(result.fault_latched && result.output == 0.0f);

    omx::imu::QuaternionEkf ekf;
    assert(ekf.configure({}) == omx::imu::QuaternionEkfStatus::Ok);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto attitude = ekf.update(0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 9.80665f,
                                         0.001f);
        assert(attitude.status == omx::imu::QuaternionEkfStatus::Ok);
        assert(std::isfinite(attitude.quaternion[0]));
    }
    assert(std::fabs(ekf.result().quaternion[0] - 1.0f) < 1.0e-3f);
    assert(ekf.update(0, 0, 0, 0, 0, 9.8f, 0.0f).status ==
           omx::imu::QuaternionEkfStatus::InvalidDt);
    assert(ekf.update(0, 0, 0,
                      std::numeric_limits<float>::quiet_NaN(), 0, 9.8f,
                      0.001f).status == omx::imu::QuaternionEkfStatus::InvalidInput);
    return 0;
}
