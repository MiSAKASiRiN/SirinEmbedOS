#ifndef SIRINOS_C_ARRAY_H
#define SIRINOS_C_ARRAY_H

#include <stddef.h>

/**
 * @brief Return the number of elements in a native C array.
 * @note GCC and Clang reject pointer arguments at compile time.
 */
/* GCC/Clang reject pointer arguments by forming a negative-width bit field. */
#if defined(__GNUC__) || defined(__clang__)
#define SRNOS_INTERNAL_ARRAY_POINTER_CHECK(array_)                              \
    (0u * sizeof(char[1 -                                                    \
        2 * !!__builtin_types_compatible_p(__typeof__(array_),                \
                                           __typeof__(&(array_)[0]))]))
#define SRNOS_ARRAY_COUNT(array_)                                               \
    ((sizeof(array_) / sizeof((array_)[0])) +                                \
     SRNOS_INTERNAL_ARRAY_POINTER_CHECK(array_))
#else
#define SRNOS_ARRAY_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))
#endif

#endif
