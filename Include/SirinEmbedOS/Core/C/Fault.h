#ifndef SRNOS_C_FAULT_H
#define SRNOS_C_FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SRNOS_CONFIG_HEADER
#include SRNOS_CONFIG_HEADER
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_ASSERT_LEVEL
#define SRNOS_ASSERT_LEVEL 2
#endif

#define SRNOS_FAULT_RECORD_MAGIC   0x5349524Eu
#define SRNOS_FAULT_RECORD_VERSION 2u
#define SRNOS_FAULT_TASK_NAME_CAPACITY 16u
#define SRNOS_FAULT_STACK_SNAPSHOT_WORDS 32u

/** @brief Supported project-configured terminal actions after Panic reporting. */
#define SRNOS_PANIC_ACTION_RESET 1u
#define SRNOS_PANIC_ACTION_HALT  2u

/** @brief Stable reasons stored in the retained Panic record. */
typedef enum {
    SRNOS_FAULT_REASON_ASSERTION = 1,
    SRNOS_FAULT_REASON_EXPLICIT_PANIC = 2,
    SRNOS_FAULT_REASON_HAL_ERROR = 3,
    SRNOS_FAULT_REASON_KERNEL_STACK_OVERFLOW = 4,
    SRNOS_FAULT_REASON_HARDFAULT = 16,
    SRNOS_FAULT_REASON_MEMMANAGE = 17,
    SRNOS_FAULT_REASON_BUSFAULT = 18,
    SRNOS_FAULT_REASON_USAGEFAULT = 19,
    SRNOS_FAULT_REASON_NMI = 20,
    SRNOS_FAULT_REASON_DEBUGMON = 21,
} SRNOS_Fault_Reason;

/** @brief Cortex-M exception frame selected by the platform decoder. */
typedef enum {
    SRNOS_FAULT_FRAME_NONE = 0,
    SRNOS_FAULT_FRAME_BASIC = 1,
    SRNOS_FAULT_FRAME_EXTENDED_FPU = 2,
    SRNOS_FAULT_FRAME_LAZY_FPU = 3,
    SRNOS_FAULT_FRAME_INVALID = 4,
} SRNOS_Fault_FrameType;

/** @brief Source and processor context supplied to the Panic pipeline. */
typedef struct {
    SRNOS_Fault_Reason reason;
    const char *expression;
    const char *file;
    const char *function;
    uint32_t line;
    uintptr_t pc;
    uintptr_t lr;
    uintptr_t task;
    uint32_t ipsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    SRNOS_Fault_FrameType frame_type;
    uintptr_t raw_sp;
    uintptr_t frame_address;
    uintptr_t msp;
    uintptr_t psp;
    uint32_t exc_return;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t xpsr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
    uint32_t ccr;
    uint32_t icsr;
    uint32_t abfsr;
    uint32_t fpccr;
    char task_name[SRNOS_FAULT_TASK_NAME_CAPACITY];
    uintptr_t task_stack_low;
    uintptr_t task_stack_high;
    uintptr_t task_stack_current;
    uint32_t task_stack_high_water_bytes;
    uintptr_t stack_snapshot_address;
    uint32_t stack_snapshot_count;
    uint32_t stack_snapshot[SRNOS_FAULT_STACK_SNAPSHOT_WORDS];
} SRNOS_Fault_Context;

/** @brief Versioned reset-retained Panic record stored in NOLOAD memory. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint32_t reason;
    uint32_t line;
    uintptr_t expression;
    uintptr_t file;
    uintptr_t function;
    uintptr_t pc;
    uintptr_t lr;
    uintptr_t task;
    uint32_t ipsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t frame_type;
    uintptr_t raw_sp;
    uintptr_t frame_address;
    uintptr_t msp;
    uintptr_t psp;
    uint32_t exc_return;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t xpsr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
    uint32_t ccr;
    uint32_t icsr;
    uint32_t abfsr;
    uint32_t fpccr;
    char task_name[SRNOS_FAULT_TASK_NAME_CAPACITY];
    uintptr_t task_stack_low;
    uintptr_t task_stack_high;
    uintptr_t task_stack_current;
    uint32_t task_stack_high_water_bytes;
    uintptr_t stack_snapshot_address;
    uint32_t stack_snapshot_count;
    uint32_t stack_snapshot[SRNOS_FAULT_STACK_SNAPSHOT_WORDS];
    uint32_t checksum;
} SRNOS_Fault_Record;

/** @brief Initialize runtime recursion state without clearing the retained record. */
void SRNOS_Fault_Initialize(void);
/** @brief Validate and copy the retained record into @p record. */
bool SRNOS_Fault_LastRecord(SRNOS_Fault_Record *record);
/** @brief Explicitly invalidate the retained record. */
void SRNOS_Fault_ClearLastRecord(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
/** @brief Capture source information, persist a record, report, and terminate. */
void SRNOS_Fault_Panic(SRNOS_Fault_Reason reason,
                    const char *expression,
                    const char *file,
                    const char *function,
                    uint32_t line);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
/** @brief Deliberately trigger an explicit Panic, primarily for diagnostics. */
void SRNOS_Fault_TriggerPanic(const char *file,
                           const char *function,
                           uint32_t line);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
/** @brief Persist a supplied context, report, and terminate. */
void SRNOS_Fault_PanicContext(const SRNOS_Fault_Context *context);

/** @brief Platform backend: enrich a portable context with processor state. */
void SRNOS_Platform_FaultCapture(SRNOS_Fault_Context *context);
/** @brief Platform backend: emit a bounded emergency report. */
void SRNOS_Platform_FaultReport(const SRNOS_Fault_Record *record);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
/** @brief Platform backend: perform the configured halt/reset terminal action. */
void SRNOS_Platform_PanicTerminate(void);

/** @brief Optional bounded project hook run before the configured terminal action. */
void SRNOS_Fault_OnReportDone(void);

/** @brief Permanently enabled single-evaluation invariant check. */
#define SRNOS_ASSERT(expression_)                                               \
    do {                                                                     \
        if (!(expression_)) {                                                \
            SRNOS_Fault_Panic(SRNOS_FAULT_REASON_ASSERTION, #expression_,          \
                           __FILE__, __func__, (uint32_t)__LINE__);           \
        }                                                                    \
    } while (0)

#if SRNOS_ASSERT_LEVEL >= 2
/** @brief Debug-level invariant check controlled by SRNOS_ASSERT_LEVEL. */
#define SRNOS_DEBUG_ASSERT(expression_) SRNOS_ASSERT(expression_)
#else
#define SRNOS_DEBUG_ASSERT(expression_) do { (void)sizeof(expression_); } while (0)
#endif

/** @brief Unconditionally enter the Panic pipeline with source metadata. */
#define SRNOS_PANIC(reason_)                                                    \
    SRNOS_Fault_Panic((reason_), NULL, __FILE__, __func__, (uint32_t)__LINE__)

/** @brief Deliberately enter Panic while retaining the caller source location. */
#define SRNOS_TRIGGER_PANIC()                                                   \
    SRNOS_Fault_TriggerPanic(__FILE__, __func__, (uint32_t)__LINE__)

#ifdef __cplusplus
}
#endif

#endif
