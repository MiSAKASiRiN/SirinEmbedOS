#include "Services/Parameters/parameters_internal.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    alignas(4) uint8_t wire[16] {};
    const uint32_t source = 0x44332211U;
    uint32_t decoded = 0U;

    SRNOS::Parameters::detail::CopyBytesUnalignedSafe(&wire[1],
                                                &source,
                                                sizeof(source));
    assert(wire[1] == 0x11U);
    assert(wire[2] == 0x22U);
    assert(wire[3] == 0x33U);
    assert(wire[4] == 0x44U);

    SRNOS::Parameters::detail::CopyBytesUnalignedSafe(&decoded,
                                                &wire[1],
                                                sizeof(decoded));
    assert(decoded == source);

    std::puts("SRNOS_Params unaligned binary test: PASS");
    return 0;
}
