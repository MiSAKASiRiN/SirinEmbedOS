#ifndef SRNOS_MATH_SIGNAL_HPP
#define SRNOS_MATH_SIGNAL_HPP

#include <algorithm>
#include <cmath>

#include "SirinEmbedOS/Algorithms/Math/angle.hpp"

namespace SRNOS::Math {

class FirstOrderLowPass {
public:
    void reset(float value = 0.0f) noexcept
    {
        value_ = value;
        initialized_ = true;
    }

    float step(float input, float cutoff_hz, float dt_s) noexcept
    {
        if (!std::isfinite(input) || !std::isfinite(cutoff_hz) ||
            !std::isfinite(dt_s) || dt_s <= 0.0f) {
            return value_;
        }
        if (!initialized_ || cutoff_hz <= 0.0f) {
            reset(input);
            return value_;
        }
        const float tau = 1.0f / (kTwoPi * cutoff_hz);
        const float alpha = dt_s / (tau + dt_s);
        value_ += alpha * (input - value_);
        return value_;
    }

    float value() const noexcept { return value_; }

private:
    float value_ {0.0f};
    bool initialized_ {false};
};

class SlewRateLimiter {
public:
    void reset(float value = 0.0f) noexcept
    {
        value_ = value;
        initialized_ = true;
    }

    float step(float target,
               float rise_per_s,
               float fall_per_s,
               float dt_s) noexcept
    {
        if (!std::isfinite(target) || !std::isfinite(dt_s) || dt_s <= 0.0f) {
            return value_;
        }
        if (!initialized_) {
            reset(target);
            return value_;
        }
        float delta = target - value_;
        if (rise_per_s > 0.0f && delta > rise_per_s * dt_s) {
            delta = rise_per_s * dt_s;
        }
        if (fall_per_s > 0.0f && delta < -fall_per_s * dt_s) {
            delta = -fall_per_s * dt_s;
        }
        value_ += delta;
        return value_;
    }

    float value() const noexcept { return value_; }

private:
    float value_ {0.0f};
    bool initialized_ {false};
};

class Ramp {
public:
    void reset(float value = 0.0f) noexcept { value_ = value; }

    float stepToward(float target, float rate_per_s, float dt_s) noexcept
    {
        if (!std::isfinite(target) || !std::isfinite(rate_per_s) ||
            !std::isfinite(dt_s) || dt_s <= 0.0f || rate_per_s <= 0.0f) {
            return value_;
        }
        const float maximum_step = rate_per_s * dt_s;
        value_ += std::clamp(target - value_, -maximum_step, maximum_step);
        return value_;
    }

    float value() const noexcept { return value_; }

private:
    float value_ {0.0f};
};

class Deadband {
public:
    explicit Deadband(float width = 0.0f) noexcept : width_(std::fabs(width)) {}

    void setWidth(float width) noexcept { width_ = std::fabs(width); }

    float apply(float value) const noexcept
    {
        return std::fabs(value) <= width_ ? 0.0f : value;
    }

private:
    float width_ {0.0f};
};

} // namespace SRNOS::Math

#endif
