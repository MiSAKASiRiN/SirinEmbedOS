#ifndef SRNOS_ENDIAN_HPP
#define SRNOS_ENDIAN_HPP

#include <cstddef>
#include <cstdint>

#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/Memory.hpp"

namespace SRNOS::Endian {

#define SRNOS_CPP_ENDIAN_LOAD(width_, type_, order_)                            \
[[nodiscard]] inline bool load##order_##width_(                              \
    Memory::ByteSpan source, std::size_t offset, type_ &value) noexcept      \
{                                                                            \
    return SRNOS_Endian_Load##order_##width_(                                   \
        SRNOS_Memory_Bytes(source.data(), source.size()), offset, &value);       \
}

#define SRNOS_CPP_ENDIAN_STORE(width_, type_, order_)                           \
[[nodiscard]] inline bool store##order_##width_(                             \
    Memory::MutableByteSpan destination, std::size_t offset, type_ value)     \
    noexcept                                                                  \
{                                                                            \
    return SRNOS_Endian_Store##order_##width_(                                  \
        SRNOS_Memory_MutableBytes(destination.data(), destination.size()),       \
        offset, value);                                                       \
}

/** @brief Load an unaligned-safe little-endian 16-bit value. */
SRNOS_CPP_ENDIAN_LOAD(16, std::uint16_t, LE)
/** @brief Load an unaligned-safe little-endian 32-bit value. */
SRNOS_CPP_ENDIAN_LOAD(32, std::uint32_t, LE)
/** @brief Load an unaligned-safe little-endian 64-bit value. */
SRNOS_CPP_ENDIAN_LOAD(64, std::uint64_t, LE)
/** @brief Load an unaligned-safe big-endian 16-bit value. */
SRNOS_CPP_ENDIAN_LOAD(16, std::uint16_t, BE)
/** @brief Load an unaligned-safe big-endian 32-bit value. */
SRNOS_CPP_ENDIAN_LOAD(32, std::uint32_t, BE)
/** @brief Load an unaligned-safe big-endian 64-bit value. */
SRNOS_CPP_ENDIAN_LOAD(64, std::uint64_t, BE)
/** @brief Store an unaligned-safe little-endian 16-bit value. */
SRNOS_CPP_ENDIAN_STORE(16, std::uint16_t, LE)
/** @brief Store an unaligned-safe little-endian 32-bit value. */
SRNOS_CPP_ENDIAN_STORE(32, std::uint32_t, LE)
/** @brief Store an unaligned-safe little-endian 64-bit value. */
SRNOS_CPP_ENDIAN_STORE(64, std::uint64_t, LE)
/** @brief Store an unaligned-safe big-endian 16-bit value. */
SRNOS_CPP_ENDIAN_STORE(16, std::uint16_t, BE)
/** @brief Store an unaligned-safe big-endian 32-bit value. */
SRNOS_CPP_ENDIAN_STORE(32, std::uint32_t, BE)
/** @brief Store an unaligned-safe big-endian 64-bit value. */
SRNOS_CPP_ENDIAN_STORE(64, std::uint64_t, BE)

#undef SRNOS_CPP_ENDIAN_LOAD
#undef SRNOS_CPP_ENDIAN_STORE

} // namespace SRNOS::Endian

#endif
