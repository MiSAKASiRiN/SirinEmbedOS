#ifndef SIRINOS_BOUNDS_HPP
#define SIRINOS_BOUNDS_HPP

#include <cstddef>

#include "SirinEmbedOS/Core/C/Bounds.h"

namespace SRNOS::Bounds {

/** @brief Return whether @p index identifies an element in @p count. */
[[nodiscard]] constexpr bool indexValid(std::size_t index,
                                        std::size_t count) noexcept
{
    return index < count;
}

/** @brief Validate a subrange without an overflow-prone offset addition. */
[[nodiscard]] constexpr bool rangeValid(std::size_t offset,
                                        std::size_t length,
                                        std::size_t capacity) noexcept
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

} // namespace SRNOS::Bounds

#endif
