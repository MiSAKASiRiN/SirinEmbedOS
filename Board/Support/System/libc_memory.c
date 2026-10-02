#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx.h"
#include "System/libc_arena.h"

extern unsigned char __srnos_libc_start[];
extern unsigned char __srnos_libc_end[];
static uintptr_t s_break;

/* The libc arena is independent of both heap_5 regions and MSP. malloc owns
 * the recursive libc lock; this primitive also serializes direct callers. */
void *_sbrk(ptrdiff_t increment)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uintptr_t current = s_break;
    const uintptr_t begin = (uintptr_t)__srnos_libc_start;
    const uintptr_t end = (uintptr_t)__srnos_libc_end;
    if (current == 0u) current = begin;
    uintptr_t next;
    if (!SRNOS_LibcArenaResize(begin, end, current, increment, &next)) goto exhausted;
    s_break = next;
    __DMB();
    __set_PRIMASK(mask);
    return (void *)current;
exhausted:
    __set_PRIMASK(mask);
    errno = ENOMEM;
    return (void *)-1;
}

void *sbrk(intptr_t increment)
{
    return _sbrk((ptrdiff_t)increment);
}
