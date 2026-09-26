#include <assert.h>
#include <stdint.h>
#include "../../Board/Glue/libc_arena.h"

int main(void)
{
    uintptr_t next = 0;
    const uintptr_t start = 0x24040000u;
    const uintptr_t end = start + 8192u;
    assert(SRNOS_LibcArenaResize(start, end, start, 8192, &next));
    assert(next == end);
    assert(!SRNOS_LibcArenaResize(start, end, next, 1, &next));
    assert(next == end);
    assert(SRNOS_LibcArenaResize(start, end, end, -8192, &next));
    assert(next == start);
    assert(!SRNOS_LibcArenaResize(start, end, start, -1, &next));
    assert(!SRNOS_LibcArenaResize(start, end, start, PTRDIFF_MIN, &next));
    assert(!SRNOS_LibcArenaResize(start, end, start, PTRDIFF_MAX, &next));
    assert(!SRNOS_LibcArenaResize(start, end, end + 1, 0, &next));
    assert(!SRNOS_LibcArenaResize(start, end, start - 1, 0, &next));
    assert(!SRNOS_LibcArenaResize(end, start, start, 0, &next));
    assert(SRNOS_LibcArenaResize(UINTPTR_MAX - 15, UINTPTR_MAX,
                                UINTPTR_MAX - 15, 15, &next));
    assert(next == UINTPTR_MAX);
    assert(!SRNOS_LibcArenaResize(UINTPTR_MAX - 15, UINTPTR_MAX,
                                 UINTPTR_MAX - 15, 16, &next));
}
