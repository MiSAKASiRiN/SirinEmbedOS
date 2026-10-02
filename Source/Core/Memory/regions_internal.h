#ifndef SRNOS_MEMORY_REGIONS_INTERNAL_H
#define SRNOS_MEMORY_REGIONS_INTERNAL_H

#include <stdint.h>

/* Linker guarantees 8-byte aligned boundaries and Flash source. Full-width
 * volatile stores initialize each ITCM ECC word without reading old contents.
 * Do not replace these loops with byte stores or a libc memset/memcpy call. */
static inline void zero_tcm_region(uintptr_t begin, uintptr_t end)
{
    while (begin < end) {
        *(volatile uint64_t *)begin = 0;
        begin += sizeof(uint64_t);
    }
}

static inline void copy_tcm_region(uintptr_t begin, uintptr_t source, uintptr_t end)
{
    while (begin < end) {
        *(volatile uint64_t *)begin = *(const uint64_t *)source;
        begin += sizeof(uint64_t);
        source += sizeof(uint64_t);
    }
}

#endif
