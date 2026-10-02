#include "SirinEmbedOS/Core/C/CRC32.h"

static const uint32_t table[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};
uint32_t SRNOS_CRC32Update(uint32_t state, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (bytes == NULL && length != 0u) return 0u;
    for (size_t i = 0; i < length; ++i) {
        state ^= bytes[i];
        state = (state >> 4u) ^ table[state & 15u];
        state = (state >> 4u) ^ table[state & 15u];
    }
    return state;
}
uint32_t SRNOS_CRC32(const void *data, size_t length)
{
    if (data == NULL && length != 0u) return 0u;
    return SRNOS_CRC32Update(SRNOS_CRC32_INIT, data, length) ^ SRNOS_CRC32_FINAL_XOR;
}
