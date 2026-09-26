#ifndef SIRINOS_C_ENDIAN_H
#define SIRINOS_C_ENDIAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#define SRNOS_ENDIAN_DECLARE_LOAD(width_, type_, order_)                        \
static inline bool SRNOS_Endian_Load##order_##width_(                           \
    SRNOS_Memory_ByteSpan source, size_t offset, type_ *value)

#define SRNOS_ENDIAN_DECLARE_STORE(width_, type_, order_)                       \
static inline bool SRNOS_Endian_Store##order_##width_(                          \
    SRNOS_Memory_MutableByteSpan destination, size_t offset, type_ value)

/** @brief Load an unaligned-safe little-endian 16-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(16, uint16_t, LE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 2u, source.size) ||
        (source.data == NULL)) return false;
    *value = (uint16_t)((uint16_t)source.data[offset] |
                        ((uint16_t)source.data[offset + 1u] << 8u));
    return true;
}

/** @brief Load an unaligned-safe little-endian 32-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(32, uint32_t, LE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 4u, source.size) ||
        (source.data == NULL)) return false;
    *value = (uint32_t)source.data[offset] |
             ((uint32_t)source.data[offset + 1u] << 8u) |
             ((uint32_t)source.data[offset + 2u] << 16u) |
             ((uint32_t)source.data[offset + 3u] << 24u);
    return true;
}

/** @brief Load an unaligned-safe little-endian 64-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(64, uint64_t, LE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 8u, source.size) ||
        (source.data == NULL)) return false;
    uint64_t result = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        result |= (uint64_t)source.data[offset + index] << (index * 8u);
    }
    *value = result;
    return true;
}

/** @brief Load an unaligned-safe big-endian 16-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(16, uint16_t, BE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 2u, source.size) ||
        (source.data == NULL)) return false;
    *value = (uint16_t)(((uint16_t)source.data[offset] << 8u) |
                        (uint16_t)source.data[offset + 1u]);
    return true;
}

/** @brief Load an unaligned-safe big-endian 32-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(32, uint32_t, BE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 4u, source.size) ||
        (source.data == NULL)) return false;
    *value = ((uint32_t)source.data[offset] << 24u) |
             ((uint32_t)source.data[offset + 1u] << 16u) |
             ((uint32_t)source.data[offset + 2u] << 8u) |
             (uint32_t)source.data[offset + 3u];
    return true;
}

/** @brief Load an unaligned-safe big-endian 64-bit value. */
SRNOS_ENDIAN_DECLARE_LOAD(64, uint64_t, BE)
{
    if ((value == NULL) || !SRNOS_Bounds_RangeValid(offset, 8u, source.size) ||
        (source.data == NULL)) return false;
    uint64_t result = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        result = (result << 8u) | source.data[offset + index];
    }
    *value = result;
    return true;
}

/** @brief Store an unaligned-safe little-endian 16-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(16, uint16_t, LE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 2u, destination.size) ||
        (destination.data == NULL)) return false;
    destination.data[offset] = (uint8_t)value;
    destination.data[offset + 1u] = (uint8_t)(value >> 8u);
    return true;
}

/** @brief Store an unaligned-safe little-endian 32-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(32, uint32_t, LE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 4u, destination.size) ||
        (destination.data == NULL)) return false;
    for (size_t index = 0u; index < 4u; ++index) {
        destination.data[offset + index] = (uint8_t)(value >> (index * 8u));
    }
    return true;
}

/** @brief Store an unaligned-safe little-endian 64-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(64, uint64_t, LE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 8u, destination.size) ||
        (destination.data == NULL)) return false;
    for (size_t index = 0u; index < 8u; ++index) {
        destination.data[offset + index] = (uint8_t)(value >> (index * 8u));
    }
    return true;
}

/** @brief Store an unaligned-safe big-endian 16-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(16, uint16_t, BE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 2u, destination.size) ||
        (destination.data == NULL)) return false;
    destination.data[offset] = (uint8_t)(value >> 8u);
    destination.data[offset + 1u] = (uint8_t)value;
    return true;
}

/** @brief Store an unaligned-safe big-endian 32-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(32, uint32_t, BE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 4u, destination.size) ||
        (destination.data == NULL)) return false;
    for (size_t index = 0u; index < 4u; ++index) {
        destination.data[offset + index] =
            (uint8_t)(value >> ((3u - index) * 8u));
    }
    return true;
}

/** @brief Store an unaligned-safe big-endian 64-bit value. */
SRNOS_ENDIAN_DECLARE_STORE(64, uint64_t, BE)
{
    if (!SRNOS_Bounds_RangeValid(offset, 8u, destination.size) ||
        (destination.data == NULL)) return false;
    for (size_t index = 0u; index < 8u; ++index) {
        destination.data[offset + index] =
            (uint8_t)(value >> ((7u - index) * 8u));
    }
    return true;
}

#undef SRNOS_ENDIAN_DECLARE_LOAD
#undef SRNOS_ENDIAN_DECLARE_STORE

#endif
