#include <cassert>
#include <cmath>
#include <limits>

#include "SirinEmbedOS/Devices/IMU/quaternion_ekf.hpp"

int main()
{
    SRNOS::IMU::QuaternionEkf ekf;
    assert(ekf.configure({}) == SRNOS::IMU::QuaternionEkfStatus::Ok);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto attitude = ekf.update(0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 9.80665f,
                                         0.001f);
        assert(attitude.status == SRNOS::IMU::QuaternionEkfStatus::Ok);
        assert(std::isfinite(attitude.quaternion[0]));
    }
    assert(std::fabs(ekf.result().quaternion[0] - 1.0f) < 1.0e-3f);
    assert(ekf.update(0, 0, 0, 0, 0, 9.8f, 0.0f).status ==
           SRNOS::IMU::QuaternionEkfStatus::InvalidDt);
    assert(ekf.update(0, 0, 0,
                      std::numeric_limits<float>::quiet_NaN(), 0, 9.8f,
                      0.001f).status == SRNOS::IMU::QuaternionEkfStatus::InvalidInput);
    return 0;
}
