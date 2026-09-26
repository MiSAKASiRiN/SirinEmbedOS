#ifndef SIRINOS_C_MEMORY_H
#define SIRINOS_C_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "SirinEmbedOS/Core/C/Bounds.h"

/** @brief Non-owning read-only byte range. */
typedef struct {
    const uint8_t *data;
    size_t size;
} SRNOS_Memory_ByteSpan;

/** @brief Non-owning writable byte range. */
typedef struct {
    uint8_t *data;
    size_t size;
} SRNOS_Memory_MutableByteSpan;

/** @brief Construct a read-only byte span; nonzero size requires non-null data. */
static inline SRNOS_Memory_ByteSpan SRNOS_Memory_Bytes(const void *data, size_t size)
{
    SRNOS_Memory_ByteSpan span = {(const uint8_t *)data, size};
    return span;
}

/** @brief Construct a writable byte span; nonzero size requires non-null data. */
static inline SRNOS_Memory_MutableByteSpan SRNOS_Memory_MutableBytes(void *data,
                                                               size_t size)
{
    SRNOS_Memory_MutableByteSpan span = {(uint8_t *)data, size};
    return span;
}

/**
 * @brief Copy bytes using byte-wide accesses.
 * @return false on invalid input, insufficient capacity, or overlapping ranges.
 */
static inline bool SRNOS_Memory_Copy(SRNOS_Memory_MutableByteSpan destination,
                                  SRNOS_Memory_ByteSpan source)
{
    if ((source.size > destination.size) ||
        ((source.size != 0u) &&
         ((destination.data == NULL) || (source.data == NULL)))) {
        return false;
    }
    if ((source.size != 0u) &&
        ((uintptr_t)destination.data != (uintptr_t)source.data)) {
        const uintptr_t out_address = (uintptr_t)destination.data;
        const uintptr_t in_address = (uintptr_t)source.data;
        const uintptr_t distance = (out_address > in_address) ?
            (out_address - in_address) : (in_address - out_address);
        if (distance < source.size) return false;
    }
    volatile uint8_t *out = destination.data;
    const volatile uint8_t *in = source.data;
    for (size_t index = 0u; index < source.size; ++index) {
        out[index] = in[index];
    }
    return true;
}

/**
 * @brief Move bytes using byte-wide accesses and defined overlap semantics.
 * @return false on invalid input or insufficient capacity.
 */
static inline bool SRNOS_Memory_Move(SRNOS_Memory_MutableByteSpan destination,
                                  SRNOS_Memory_ByteSpan source)
{
    if ((source.size > destination.size) ||
        ((source.size != 0u) &&
         ((destination.data == NULL) || (source.data == NULL)))) {
        return false;
    }
    volatile uint8_t *out = destination.data;
    const volatile uint8_t *in = source.data;
    const uintptr_t out_address = (uintptr_t)destination.data;
    const uintptr_t in_address = (uintptr_t)source.data;
    if ((out_address > in_address) &&
        ((out_address - in_address) < source.size)) {
        for (size_t index = source.size; index != 0u; --index) {
            out[index - 1u] = in[index - 1u];
        }
    } else {
        for (size_t index = 0u; index < source.size; ++index) {
            out[index] = in[index];
        }
    }
    return true;
}

/** @brief Fill a writable span and return false for invalid nonempty input. */
static inline bool SRNOS_Memory_Fill(SRNOS_Memory_MutableByteSpan destination,
                                  uint8_t value)
{
    if ((destination.size != 0u) && (destination.data == NULL)) {
        return false;
    }
    volatile uint8_t *out = destination.data;
    for (size_t index = 0u; index < destination.size; ++index) {
        out[index] = value;
    }
    return true;
}

#endif
