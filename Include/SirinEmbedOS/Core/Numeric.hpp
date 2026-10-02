#ifndef SRNOS_NUMERIC_HPP
#define SRNOS_NUMERIC_HPP

#include <cstddef>
#include <limits>

namespace SRNOS::Numeric {

/** @brief Add two sizes and return false on overflow. */
[[nodiscard]] constexpr bool checkedAdd(std::size_t left,
                                        std::size_t right,
                                        std::size_t &result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max() - left)) return false;
    result = left + right;
    return true;
}

/** @brief Multiply two sizes and return false on overflow. */
[[nodiscard]] constexpr bool checkedMul(std::size_t left,
                                        std::size_t right,
                                        std::size_t &result) noexcept
{
    if ((left != 0u) &&
        (right > (std::numeric_limits<std::size_t>::max() / left))) return false;
    result = left * right;
    return true;
}

/** @brief Return the smaller value with single evaluation of each argument. */
template <typename T>
[[nodiscard]] constexpr T min(const T &left, const T &right) noexcept
{
    return (right < left) ? right : left;
}

/** @brief Return the larger value with single evaluation of each argument. */
template <typename T>
[[nodiscard]] constexpr T max(const T &left, const T &right) noexcept
{
    return (left < right) ? right : left;
}

/** @brief Clamp a value to inclusive bounds with single argument evaluation. */
template <typename T>
[[nodiscard]] constexpr T clamp(const T &value,
                                const T &lower,
                                const T &upper) noexcept
{
    return (value < lower) ? lower : ((upper < value) ? upper : value);
}

} // namespace SRNOS::Numeric

#endif
