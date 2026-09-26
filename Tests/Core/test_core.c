#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "SirinEmbedOS/Core/SirinOS.h"

int main(void)
{
    uint8_t bytes[17] = {0};
    assert(SRNOS_ARRAY_COUNT(bytes) == 17u);
    assert(SRNOS_Bounds_IndexValid(16u, 17u));
    assert(!SRNOS_Bounds_IndexValid(17u, 17u));
    assert(SRNOS_Bounds_RangeValid(SIZE_MAX, 0u, SIZE_MAX));
    assert(!SRNOS_Bounds_RangeValid(SIZE_MAX, 1u, SIZE_MAX));

    size_t result = 0u;
    assert(SRNOS_Numeric_CheckedAdd(4u, 5u, &result) && result == 9u);
    assert(!SRNOS_Numeric_CheckedAdd(SIZE_MAX, 1u, &result));
    assert(SRNOS_Numeric_CheckedMul(4u, 5u, &result) && result == 20u);
    assert(!SRNOS_Numeric_CheckedMul(SIZE_MAX, 2u, &result));
    assert(SRNOS_Numeric_ClampI16(4, -2, 3) == 3);
    assert(SRNOS_Numeric_ClampI16(-4, -2, 3) == -2);
    assert(SRNOS_Numeric_ClampI16(1, -2, 3) == 1);

    SRNOS_Memory_MutableByteSpan output = SRNOS_Memory_MutableBytes(&bytes[1], 16u);
    assert(SRNOS_Endian_StoreLE32(output, 0u, 0x12345678u));
    assert(SRNOS_Endian_StoreBE32(output, 4u, 0x89ABCDEFu));
    uint32_t value = 0u;
    assert(SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(&bytes[1], 16u), 0u, &value));
    assert(value == 0x12345678u);
    assert(SRNOS_Endian_LoadBE32(SRNOS_Memory_Bytes(&bytes[1], 16u), 4u, &value));
    assert(value == 0x89ABCDEFu);
    assert(!SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(bytes, 3u), 0u, &value));

    const uint8_t source[] = {1u, 2u, 3u, 4u};
    assert(SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(bytes, sizeof(bytes)),
                          SRNOS_Memory_Bytes(source, sizeof(source))));
    assert(memcmp(bytes, source, sizeof(source)) == 0);
    assert(!SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(&bytes[1], 4u),
                           SRNOS_Memory_Bytes(bytes, 4u)));
    assert(SRNOS_Memory_Move(SRNOS_Memory_MutableBytes(&bytes[1], sizeof(bytes) - 1u),
                          SRNOS_Memory_Bytes(bytes, 4u)));
    assert(bytes[1] == 1u && bytes[4] == 4u);
    assert(SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(NULL, 0u),
                          SRNOS_Memory_Bytes(NULL, 0u)));
    assert(!SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(bytes, 2u),
                           SRNOS_Memory_Bytes(source, sizeof(source))));
    return 0;
}
