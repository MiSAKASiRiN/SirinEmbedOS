#ifndef SRNOS_PLATFORM_HPP
#define SRNOS_PLATFORM_HPP

#include <cstddef>
#include <cstdint>

#include "SirinEmbedOS/Core/C/Platform.h"

namespace SRNOS::Platform {

/** @brief Initialize platform state and fault traps. */
inline void initialize() noexcept { SRNOS_Platform_Initialize(); }
/** @brief Enable Cortex-M configurable faults and arithmetic traps. */
inline void enableFaultTraps() noexcept { SRNOS_Platform_EnableFaultTraps(); }

namespace Emergency {

/** @brief Register an already-reserved polling transport before scheduling. */
[[nodiscard]] inline bool reserve(SRNOS_EmergencyWriteByte writeByte,
                                  SRNOS_EmergencyFlush flush,
                                  void *user = nullptr) noexcept
{
    return SRNOS_Platform_ReserveEmergencyOutput(writeByte, flush, user);
}

/** @brief Write a bounded byte range and return the accepted count. */
[[nodiscard]] inline std::size_t write(const void *data,
                                       std::size_t size) noexcept
{
    return SRNOS_Platform_Emergency_Write(data, size);
}

/** @brief Write a NUL-terminated string and return the accepted count. */
[[nodiscard]] inline std::size_t write(const char *string) noexcept
{
    return SRNOS_Platform_Emergency_WriteString(string);
}

/** @brief Wait for bounded transmit completion. */
inline void flush() noexcept { SRNOS_Platform_Emergency_Flush(); }

} // namespace Emergency
} // namespace SRNOS::Platform

#endif
