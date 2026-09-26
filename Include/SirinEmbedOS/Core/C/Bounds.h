#ifndef SIRINOS_C_BOUNDS_H
#define SIRINOS_C_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>

/** @brief Return whether @p index names an element in a @p count-sized range. */
static inline bool SRNOS_Bounds_IndexValid(size_t index, size_t count)
{
    return index < count;
}

/**
 * @brief Validate a subrange without evaluating an overflow-prone addition.
 * @return true when `[offset, offset + length)` fits in @p capacity.
 */
static inline bool SRNOS_Bounds_RangeValid(size_t offset,
                                       size_t length,
                                       size_t capacity)
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

#endif
