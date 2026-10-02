#ifndef SRNOS_C_NUMERIC_H
#define SRNOS_C_NUMERIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Add two sizes and return false on overflow or a null result. */
static inline bool SRNOS_Numeric_CheckedAdd(size_t left,
                                         size_t right,
                                         size_t *result)
{
    if ((result == NULL) || (right > (SIZE_MAX - left))) {
        return false;
    }
    *result = left + right;
    return true;
}

/** @brief Multiply two sizes and return false on overflow or a null result. */
static inline bool SRNOS_Numeric_CheckedMul(size_t left,
                                         size_t right,
                                         size_t *result)
{
    if ((result == NULL) || ((left != 0u) && (right > (SIZE_MAX / left)))) {
        return false;
    }
    *result = left * right;
    return true;
}

/** @brief Return the smaller single-precision value. */
static inline float SRNOS_Numeric_MinF32(float left, float right)
{
    return (left < right) ? left : right;
}

/** @brief Return the larger single-precision value. */
static inline float SRNOS_Numeric_MaxF32(float left, float right)
{
    return (left > right) ? left : right;
}

/** @brief Clamp a single-precision value to the inclusive bounds. */
static inline float SRNOS_Numeric_ClampF32(float value, float lower, float upper)
{
    return (value < lower) ? lower : ((value > upper) ? upper : value);
}

/** @brief Clamp a signed 16-bit integer to the inclusive bounds. */
static inline int16_t SRNOS_Numeric_ClampI16(int16_t value,
                                         int16_t lower,
                                         int16_t upper)
{
    return (value < lower) ? lower : ((value > upper) ? upper : value);
}

#endif
