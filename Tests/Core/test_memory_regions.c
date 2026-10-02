#include "Core/Memory/regions_internal.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    /* Poisoned RAM models reset with NOLOAD data left from a prior boot.
     * Guards represent adjacent code/retained regions that must not be cleared. */
    uint64_t memory[10], flash[8];
    for (size_t i=0; i<10; ++i) memory[i]=UINT64_C(0xfeedface12345678);
    for (size_t i=0; i<8; ++i) flash[i]=UINT64_C(0x1122334455667700)+i;
    zero_tcm_region((uintptr_t)&memory[1], (uintptr_t)&memory[9]);
    assert(memory[0]==UINT64_C(0xfeedface12345678));
    assert(memory[9]==UINT64_C(0xfeedface12345678));
    for (size_t i=1; i<9; ++i) assert(memory[i]==0);
    copy_tcm_region((uintptr_t)&memory[1], (uintptr_t)flash, (uintptr_t)&memory[9]);
    for (size_t i=0; i<8; ++i) assert(memory[i+1]==flash[i]);
    zero_tcm_region((uintptr_t)&memory[1], (uintptr_t)&memory[1]);
    copy_tcm_region((uintptr_t)&memory[1], (uintptr_t)flash, (uintptr_t)&memory[1]);
    for (size_t i=0; i<8; ++i) assert(memory[i+1]==flash[i]);
    assert(memory[0]==UINT64_C(0xfeedface12345678));
    assert(memory[9]==UINT64_C(0xfeedface12345678));
    /* A second boot must clear previously initialized state too. */
    zero_tcm_region((uintptr_t)&memory[1], (uintptr_t)&memory[9]);
    for (size_t i=1; i<9; ++i) assert(memory[i]==0);
    return 0;
}
