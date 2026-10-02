#ifndef SRNOS_LIBC_ARENA_H
#define SRNOS_LIBC_ARENA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Integer-address arithmetic shared by sbrk and its host boundary tests. */
static inline bool SRNOS_LibcArenaResize(uintptr_t begin, uintptr_t end,
                                       uintptr_t current, ptrdiff_t increment,
                                       uintptr_t *next)
{
    if (begin > end || current < begin || current > end || next == NULL) return false;
    if (increment >= 0) {
        if ((uintptr_t)increment > end - current) return false;
        *next = current + (uintptr_t)increment;
    } else {
        const uintptr_t magnitude = (uintptr_t)(-(increment + 1)) + 1u;
        if (magnitude > current - begin) return false;
        *next = current - magnitude;
    }
    return true;
}
#endif
