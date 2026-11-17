#ifndef SRNOS_MATH_OLS_HPP
#define SRNOS_MATH_OLS_HPP

#include <array>
#include <cmath>
#include <cstddef>

namespace SRNOS::Math {

template <std::size_t Window>
class OlsWindow {
    static_assert(Window >= 2U, "OLS window must contain at least two samples");

public:
    void reset() noexcept
    {
        intervals_.fill(0.0f);
        values_.fill(0.0f);
        count_ = 0U;
        next_ = 0U;
    }

    bool push(float value, float dt_s) noexcept
    {
        if (!std::isfinite(value) || !std::isfinite(dt_s) || dt_s <= 0.0f) {
            return false;
        }
        // Store only the interval ending at this sample. No uptime accumulates.
        intervals_[next_] = dt_s;
        values_[next_] = value;
        next_ = (next_ + 1U) % Window;
        if (count_ < Window) {
            ++count_;
        }
        return true;
    }

    std::size_t size() const noexcept { return count_; }
    constexpr std::size_t capacity() const noexcept { return Window; }
    bool full() const noexcept { return count_ == Window; }

    float mean() const noexcept
    {
        if (count_ == 0U) {
            return 0.0f;
        }
        float sum = 0.0f;
        for (std::size_t i = 0U; i < count_; ++i) {
            sum += values_[physicalIndex(i)];
        }
        return sum / static_cast<float>(count_);
    }

    float slope() const noexcept
    {
        if (count_ < 2U) {
            return 0.0f;
        }
        float sum_t = 0.0f;
        float sum_y = 0.0f;
        float relative_time = 0.0f;
        for (std::size_t i = 0U; i < count_; ++i) {
            const std::size_t index = physicalIndex(i);
            if (i != 0U) {
                relative_time += intervals_[index];
            }
            sum_t += relative_time;
            sum_y += values_[index];
        }
        const float mean_t = sum_t / static_cast<float>(count_);
        const float mean_y = sum_y / static_cast<float>(count_);
        float covariance = 0.0f;
        float variance = 0.0f;
        relative_time = 0.0f;
        for (std::size_t i = 0U; i < count_; ++i) {
            const std::size_t index = physicalIndex(i);
            if (i != 0U) {
                relative_time += intervals_[index];
            }
            const float centered_t = relative_time - mean_t;
            covariance += centered_t * (values_[index] - mean_y);
            variance += centered_t * centered_t;
        }
        return variance > 1.0e-12f ? covariance / variance : 0.0f;
    }

private:
    std::size_t physicalIndex(std::size_t chronological_index) const noexcept
    {
        const std::size_t oldest = count_ == Window ? next_ : 0U;
        return (oldest + chronological_index) % Window;
    }

    std::array<float, Window> intervals_ {};
    std::array<float, Window> values_ {};
    std::size_t count_ {0U};
    std::size_t next_ {0U};
};

} // namespace SRNOS::Math

#endif
