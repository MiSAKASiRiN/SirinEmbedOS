#ifndef SRNOS_ARRAY_HPP
#define SRNOS_ARRAY_HPP

#include <cstddef>

namespace SRNOS::Array {

/** @brief Return the number of elements in a native array; pointers do not match. */
template <typename T, std::size_t N>
[[nodiscard]] constexpr std::size_t count(const T (&)[N]) noexcept
{
    return N;
}

} // namespace SRNOS::Array

#endif
