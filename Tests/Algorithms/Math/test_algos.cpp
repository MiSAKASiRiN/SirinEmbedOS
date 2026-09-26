#include <cassert>
#include <cmath>
#include <type_traits>

#include "SirinEmbedOS/Algorithms/Math/angle.hpp"
#include "SirinEmbedOS/Algorithms/Math/kalman_filter.hpp"
#include "SirinEmbedOS/Algorithms/Math/ols.hpp"
#include "SirinEmbedOS/Algorithms/Math/signal.hpp"

namespace {

bool near(float left, float right, float tolerance = 1.0e-4f)
{
    return std::fabs(left - right) <= tolerance;
}

} // namespace

int main()
{
    float left_data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    float right_data[] = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    float product_data[4] = {};
    float transpose_data[6] = {};
    arm_matrix_instance_f32 left{}, right{}, product{}, transpose{};
    arm_mat_init_f32(&left, 2, 3, left_data);
    arm_mat_init_f32(&right, 3, 2, right_data);
    arm_mat_init_f32(&product, 2, 2, product_data);
    arm_mat_init_f32(&transpose, 3, 2, transpose_data);
    assert(left.numRows == 2 && left.numCols == 3 && left.pData == left_data);
    assert(arm_mat_mult_f32(&left, &right, &product) == ARM_MATH_SUCCESS);
    assert(near(product_data[0], 4.0f) && near(product_data[1], 5.0f));
    assert(near(product_data[2], 10.0f) && near(product_data[3], 11.0f));
    assert(arm_mat_trans_f32(&left, &transpose) == ARM_MATH_SUCCESS);
    assert(near(transpose_data[1], 4.0f) && near(transpose_data[4], 3.0f));
    assert(arm_mat_mult_f32(&left, &left, &product) == ARM_MATH_SIZE_MISMATCH);
    assert(arm_mat_trans_f32(&left, &product) == ARM_MATH_SIZE_MISMATCH);

    assert(near(algo::wrapDegrees(180.0f), -180.0f));
    assert(near(algo::wrapDegrees(-181.0f), 179.0f));
    assert(near(algo::shortestAngularDistance(179.0f, -179.0f, 360.0f), 2.0f));
    assert(near(algo::wrapRadians(algo::kPi), -algo::kPi));

    algo::FirstOrderLowPass filter;
    filter.reset(0.0f);
    const float filtered = filter.step(1.0f, 2.0f, 0.1f);
    assert(filtered > 0.0f && filtered < 1.0f);

    algo::SlewRateLimiter slew;
    slew.reset(0.0f);
    assert(near(slew.step(10.0f, 2.0f, 4.0f, 0.1f), 0.2f));
    assert(near(slew.step(-10.0f, 2.0f, 4.0f, 0.1f), -0.2f));

    algo::OlsWindow<5> ols;
    for (int index = 0; index < 8; ++index) {
        assert(ols.push(2.0f * static_cast<float>(index) +
                            (index == 3 ? 0.01f : 0.0f),
                        0.1f));
        assert(ols.size() <= ols.capacity());
    }
    assert(near(ols.slope(), 20.0f, 0.05f));
    assert(!ols.push(0.0f, 0.0f));

    algo::KalmanFilter<1, 0, 1> kalman;
    kalman.measurementModel()[0] = 1.0f;
    kalman.processNoise()[0] = 0.01f;
    kalman.measurementNoise()[0] = 0.1f;
    assert(kalman.predict() == algo::KalmanStatus::Ok);
    assert(kalman.update({1.0f}) == algo::KalmanStatus::Ok);
    assert(kalman.state()[0] > 0.8f && kalman.state()[0] < 1.0f);
    assert(kalman.update({NAN}) == algo::KalmanStatus::InvalidMeasurement);
    static_assert(!std::is_polymorphic_v<decltype(kalman)>);
    return 0;
}
