#ifndef SIRINOS_C_PLATFORM_H
#define SIRINOS_C_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize SirinOS platform state and enable configured fault traps. */
void SRNOS_Platform_Initialize(void);
/** @brief Enable MemManage, BusFault, UsageFault and divide-by-zero traps.
 * Unaligned trapping is opt-in (SRNOS_TRAP_UNALIGNED) to match the runtime ABI. */
void SRNOS_Platform_EnableFaultTraps(void);

/** Reserved before scheduler start, once; callbacks must work with interrupts
 * disabled, must not allocate or call RTOS, and must return within bounded time.
 * The owner must also reserve the physical transport in system configuration. */
typedef bool (*SRNOS_EmergencyWriteByte)(void *user, uint8_t byte);
typedef void (*SRNOS_EmergencyFlush)(void *user);
bool SRNOS_Platform_ReserveEmergencyOutput(SRNOS_EmergencyWriteByte write_byte,
                                           SRNOS_EmergencyFlush flush, void *user);
/** @brief Write one byte with a bounded polling loop. */
bool SRNOS_Platform_Emergency_WriteByte(uint8_t byte);
/** @brief Write up to @p size bytes and return the count accepted. */
size_t SRNOS_Platform_Emergency_Write(const void *data, size_t size);
/** @brief Write a NUL-terminated string and return the count accepted. */
size_t SRNOS_Platform_Emergency_WriteString(const char *string);
/** @brief Write an 8-bit hexadecimal value. */
void SRNOS_Platform_Emergency_WriteHexU8(uint8_t value);
/** @brief Write a 32-bit hexadecimal value. */
void SRNOS_Platform_Emergency_WriteHexU32(uint32_t value);
/** @brief Wait for transmit completion with a bounded polling loop. */
void SRNOS_Platform_Emergency_Flush(void);
/** @brief Emit a diagnostic word dump; intended only for trusted aligned addresses. */
void SRNOS_Platform_Emergency_DumpWords(const char *tag,
                                     const void *address,
                                     uint32_t words);

#ifdef __cplusplus
}
#endif

#endif
