#ifndef SRNOS_MATH_ANGLE_HPP
#define SRNOS_MATH_ANGLE_HPP

#include <cmath>

namespace SRNOS::Math {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float wrapPeriodic(float value, float period) noexcept
{
    if (!std::isfinite(value) || !std::isfinite(period) || period <= 0.0f) {
        return value;
    }
    const float half = 0.5f * period;
    float wrapped = std::fmod(value + half, period);
    if (wrapped < 0.0f) {
        wrapped += period;
    }
    return wrapped - half;
}

inline float wrapRadians(float radians) noexcept
{
    return wrapPeriodic(radians, kTwoPi);
}

inline float wrapDegrees(float degrees) noexcept
{
    return wrapPeriodic(degrees, 360.0f);
}

inline float shortestAngularDistance(float from,
                                     float to,
                                     float period = kTwoPi) noexcept
{
    return wrapPeriodic(to - from, period);
}

} // namespace SRNOS::Math

#endif
