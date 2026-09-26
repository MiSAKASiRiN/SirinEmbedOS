#ifndef SIRINOS_STM32H7_FAULT_H
#define SIRINOS_STM32H7_FAULT_H

#include <stdint.h>

#include "SirinEmbedOS/Core/C/Fault.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Decode a Cortex-M exception frame and enter the Panic pipeline. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void SRNOS_STM32H7_HandleException(uint32_t *mainStackPointer,
                                uint32_t *processStackPointer,
                                uint32_t exceptionReturn,
                                SRNOS_Fault_Reason reason);

/** @brief Register FreeRTOS task stack metadata for fault-time diagnostics. */
void SRNOS_STM32H7_TraceTaskCreate(void *task,
                                const char *name,
                                void *stackLow,
                                void *stackHighInclusive);
/** @brief Remove fault-time task metadata before a task is destroyed. */
void SRNOS_STM32H7_TraceTaskDelete(void *task);

#define SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(code_)                              \
    do {                                                                     \
        __asm volatile (                                                     \
            "mrs r0, msp\n"                                                 \
            "mrs r1, psp\n"                                                 \
            "mov r2, lr\n"                                                  \
            "movs r3, #" #code_ "\n"                                       \
            "b SRNOS_STM32H7_HandleException\n");                              \
        __builtin_unreachable();                                             \
    } while (0)

#define SRNOS_STM32H7_HANDLE_HARDFAULT()                                        \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(16)
#define SRNOS_STM32H7_HANDLE_MEMMANAGE()                                        \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(17)
#define SRNOS_STM32H7_HANDLE_BUSFAULT()                                         \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(18)
#define SRNOS_STM32H7_HANDLE_USAGEFAULT()                                       \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(19)
#define SRNOS_STM32H7_HANDLE_NMI()                                              \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(20)
#define SRNOS_STM32H7_HANDLE_DEBUGMON()                                         \
    SRNOS_STM32H7_HANDLE_EXCEPTION_CODE(21)

#ifdef __cplusplus
}
#endif

#endif
