#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/Core/C/Platform.h"
#include "STM32H7Fault.h"
#include "SirinEmbedOS/Kernel/System.h"

#ifdef SIRINEMBEDOS_CONFIG_HEADER
#include SIRINEMBEDOS_CONFIG_HEADER
#endif

#include "stm32h7xx.h"
#include "FreeRTOS.h"
#include "task.h"

/* ST Arm Clang/Picolibc uses an unaligned-access multilib. Trapping these
 * legal Normal-memory accesses requires rebuilding all code/runtime strictly
 * aligned; leave it opt-in for diagnostics, never enable it silently. */
#ifndef SRNOS_TRAP_UNALIGNED
#define SRNOS_TRAP_UNALIGNED 0
#endif
#ifndef SRNOS_PANIC_ACTION
#define SRNOS_PANIC_ACTION SRNOS_PANIC_ACTION_RESET
#endif
#ifndef SRNOS_STM32H7_MAX_TRACKED_TASKS
#define SRNOS_STM32H7_MAX_TRACKED_TASKS 64u
#endif
#ifndef SRNOS_STM32H7_MAX_TASK_STACK_BYTES
#define SRNOS_STM32H7_MAX_TASK_STACK_BYTES (64u * 1024u)
#endif

#if (SRNOS_PANIC_ACTION != SRNOS_PANIC_ACTION_RESET) && \
    (SRNOS_PANIC_ACTION != SRNOS_PANIC_ACTION_HALT)
#error "SRNOS_PANIC_ACTION must be SRNOS_PANIC_ACTION_RESET or SRNOS_PANIC_ACTION_HALT"
#endif

typedef struct {
    volatile uint32_t active;
    uintptr_t task;
    uintptr_t stack_low;
    uintptr_t stack_high;
    char name[SRNOS_FAULT_TASK_NAME_CAPACITY];
} SRNOS_STM32H7_TaskMetadata;

typedef struct {
    const uint32_t *core_frame;
    SRNOS_Fault_FrameType type;
} SRNOS_STM32H7_FrameDecode;

static SRNOS_STM32H7_TaskMetadata
    s_task_metadata[SRNOS_STM32H7_MAX_TRACKED_TASKS];
static SRNOS_Fault_Context s_exception_context;

/* A sink is an explicitly reserved, bounded polling transport. No UART is
 * configured, reset, or borrowed by Fault. Without a sink the retained record
 * is the only output, including faults before application configuration. */
static SRNOS_EmergencyWriteByte s_emergency_write;
static SRNOS_EmergencyFlush s_emergency_flush;
static void *s_emergency_user;
static bool s_emergency_reserved;

bool SRNOS_Platform_ReserveEmergencyOutput(SRNOS_EmergencyWriteByte write_byte,
                                           SRNOS_EmergencyFlush flush,
                                           void *user)
{
    if (__get_IPSR() != 0u || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ||
        SRNOS_SystemConfigurationFrozen() || s_emergency_reserved || write_byte == NULL) return false;
    s_emergency_user = user;
    s_emergency_flush = flush;
    s_emergency_write = write_byte;
    s_emergency_reserved = true;
    return true;
}

bool SRNOS_Platform_Emergency_WriteByte(uint8_t byte)
{
    return s_emergency_write != NULL && s_emergency_write(s_emergency_user, byte);
}

size_t SRNOS_Platform_Emergency_Write(const void *data, size_t size)
{
    if ((data == NULL) && (size != 0u)) return 0u;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t written = 0u;
    while ((written < size) && SRNOS_Platform_Emergency_WriteByte(bytes[written])) {
        ++written;
    }
    return written;
}

size_t SRNOS_Platform_Emergency_WriteString(const char *string)
{
    if (string == NULL) return 0u;
    size_t written = 0u;
    while ((string[written] != '\0') &&
           SRNOS_Platform_Emergency_WriteByte((uint8_t)string[written])) {
        ++written;
    }
    return written;
}

static uint8_t hexDigit(uint32_t value)
{
    value &= 0xFu;
    return (uint8_t)((value < 10u) ? ('0' + value) : ('A' + value - 10u));
}

void SRNOS_Platform_Emergency_WriteHexU8(uint8_t value)
{
    (void)SRNOS_Platform_Emergency_WriteByte(hexDigit(value >> 4u));
    (void)SRNOS_Platform_Emergency_WriteByte(hexDigit(value));
}

void SRNOS_Platform_Emergency_WriteHexU32(uint32_t value)
{
    for (int32_t index = 7; index >= 0; --index) {
        (void)SRNOS_Platform_Emergency_WriteByte(
            hexDigit(value >> ((uint32_t)index * 4u)));
    }
}

void SRNOS_Platform_Emergency_Flush(void)
{
    if (s_emergency_flush != NULL) s_emergency_flush(s_emergency_user);
}

void SRNOS_Platform_Emergency_DumpWords(const char *tag,
                                     const void *address,
                                     uint32_t words)
{
    (void)SRNOS_Platform_Emergency_WriteString(tag);
    (void)SRNOS_Platform_Emergency_WriteString(" @0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)(uintptr_t)address);
    (void)SRNOS_Platform_Emergency_WriteString(":");
    const uint32_t *data = (const uint32_t *)address;
    for (uint32_t index = 0u; index < words; ++index) {
        (void)SRNOS_Platform_Emergency_WriteString(" ");
        SRNOS_Platform_Emergency_WriteHexU32(data[index]);
    }
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");
}

void SRNOS_Platform_EnableFaultTraps(void)
{
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk |
                  SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
#if SRNOS_TRAP_UNALIGNED
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
#else
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
#endif
    __DSB();
    __ISB();
}

void SRNOS_Platform_Initialize(void)
{
    SRNOS_Fault_Initialize();
    SRNOS_Platform_EnableFaultTraps();
}

static uintptr_t currentTask(void)
{
#if defined(INCLUDE_xTaskGetSchedulerState) && (INCLUDE_xTaskGetSchedulerState == 1)
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        return (uintptr_t)xTaskGetCurrentTaskHandle();
    }
#endif
    return 0u;
}

static size_t boundedStringLength(const char *string, size_t capacity)
{
    if (string == NULL) return 0u;
    size_t length = 0u;
    while ((length < capacity) && (string[length] != '\0')) ++length;
    return length;
}

void SRNOS_STM32H7_TraceTaskCreate(void *task,
                                const char *name,
                                void *stackLow,
                                void *stackHighInclusive)
{
    if ((task == NULL) || (stackLow == NULL) ||
        (stackHighInclusive == NULL)) return;

    SRNOS_STM32H7_TaskMetadata *slot = NULL;
    for (size_t index = 0u; index < SRNOS_STM32H7_MAX_TRACKED_TASKS; ++index) {
        if ((s_task_metadata[index].active != 0u) &&
            (s_task_metadata[index].task == (uintptr_t)task)) {
            slot = &s_task_metadata[index];
            break;
        }
        if ((slot == NULL) && (s_task_metadata[index].active == 0u)) {
            slot = &s_task_metadata[index];
        }
    }
    if (slot == NULL) return;

    const uintptr_t highInclusive = (uintptr_t)stackHighInclusive;
    if (highInclusive > (UINTPTR_MAX - sizeof(StackType_t))) return;

    slot->active = 0u;
    slot->task = (uintptr_t)task;
    slot->stack_low = (uintptr_t)stackLow;
    slot->stack_high = highInclusive + sizeof(StackType_t);
    (void)SRNOS_Memory_Fill(
        SRNOS_Memory_MutableBytes(slot->name, sizeof(slot->name)), 0u);
    static const char unknownName[] = "?";
    if (name == NULL) name = unknownName;
    const size_t nameLength =
        boundedStringLength(name, sizeof(slot->name) - 1u);
    (void)SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(slot->name, nameLength),
        SRNOS_Memory_Bytes(name, nameLength));
    __DMB();
    slot->active = 1u;
}

void SRNOS_STM32H7_TraceTaskDelete(void *task)
{
    if (task == NULL) return;
    for (size_t index = 0u; index < SRNOS_STM32H7_MAX_TRACKED_TASKS; ++index) {
        if ((s_task_metadata[index].active != 0u) &&
            (s_task_metadata[index].task == (uintptr_t)task)) {
            s_task_metadata[index].active = 0u;
            __DMB();
            return;
        }
    }
}

static bool isReadableAddress(uintptr_t address)
{
    return ((address >= 0x08000000u) && (address < 0x0A000000u)) ||
           ((address >= 0x20000000u) && (address < 0x20020000u)) ||
           ((address >= 0x24000000u) && (address < 0x24050000u)) ||
           ((address >= 0x30000000u) && (address < 0x30008000u)) ||
           ((address >= 0x38000000u) && (address < 0x38004000u));
}

static bool isReadableRamRange(uintptr_t start, uintptr_t endInclusive)
{
    if (endInclusive < start) return false;
#define SRNOS_RANGE_IN(low_, high_)                                             \
    ((start >= (uintptr_t)(low_)) && (endInclusive < (uintptr_t)(high_)))
    const bool readable =
        SRNOS_RANGE_IN(0x20000000u, 0x20020000u) ||
        SRNOS_RANGE_IN(0x24000000u, 0x24050000u) ||
        SRNOS_RANGE_IN(0x30000000u, 0x30008000u) ||
        SRNOS_RANGE_IN(0x38000000u, 0x38004000u);
#undef SRNOS_RANGE_IN
    return readable;
}

static bool isReadableWords(const uint32_t *address, size_t words)
{
    const uintptr_t start = (uintptr_t)address;
    if ((words == 0u) || ((start & 3u) != 0u) ||
        (words > (SIZE_MAX / sizeof(uint32_t)))) return false;
    const size_t bytes = words * sizeof(uint32_t);
    if (start > (UINTPTR_MAX - (bytes - 1u))) return false;
    return isReadableAddress(start) &&
           isReadableAddress(start + bytes - 1u);
}

static bool coreFrameIsPlausible(const uint32_t *frame,
                                 bool requireReadablePc)
{
    if (!isReadableWords(frame, 8u)) return false;
    if ((frame[7] & (1u << 24u)) == 0u) return false;
    return !requireReadablePc ||
           isReadableAddress((uintptr_t)(frame[6] & ~1u));
}

static SRNOS_STM32H7_FrameDecode findCoreFrame(const uint32_t *stackPointer,
                                            uint32_t exceptionReturn,
                                            uint32_t fpccr)
{
    SRNOS_STM32H7_FrameDecode decoded = {
        .core_frame = NULL,
        .type = SRNOS_FAULT_FRAME_INVALID,
    };
    if (stackPointer == NULL) return decoded;
    if ((exceptionReturn & (1u << 4u)) != 0u) {
        if (coreFrameIsPlausible(stackPointer, false)) {
            decoded.core_frame = stackPointer;
            decoded.type = SRNOS_FAULT_FRAME_BASIC;
        }
        return decoded;
    }

    const uintptr_t rawAddress = (uintptr_t)stackPointer;
    const size_t fpFrameBytes = 18u * sizeof(uint32_t);
    if (rawAddress > (UINTPTR_MAX - fpFrameBytes)) return decoded;

    /* In an extended frame the FP area is below the eight-word core frame. */
    const uint32_t *extendedFrame =
        (const uint32_t *)(rawAddress + fpFrameBytes);
    if (!coreFrameIsPlausible(extendedFrame, false)) return decoded;

    decoded.core_frame = extendedFrame;
    decoded.type = ((fpccr & FPU_FPCCR_LSPACT_Msk) != 0u) ?
                   SRNOS_FAULT_FRAME_LAZY_FPU :
                   SRNOS_FAULT_FRAME_EXTENDED_FPU;
    return decoded;
}

static void captureTaskMetadata(SRNOS_Fault_Context *context)
{
    context->task = currentTask();
    context->task_stack_current = context->raw_sp;
    if (context->task == 0u) return;

    for (size_t index = 0u; index < SRNOS_STM32H7_MAX_TRACKED_TASKS; ++index) {
        const SRNOS_STM32H7_TaskMetadata *slot = &s_task_metadata[index];
        if ((slot->active == 0u) || (slot->task != context->task)) continue;

        (void)SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(context->task_name,
                                   sizeof(context->task_name)),
            SRNOS_Memory_Bytes(slot->name, sizeof(slot->name)));
        context->task_stack_low = slot->stack_low;
        context->task_stack_high = slot->stack_high;

        if ((slot->stack_high <= slot->stack_low) ||
            ((slot->stack_high - slot->stack_low) >
             SRNOS_STM32H7_MAX_TASK_STACK_BYTES) ||
            !isReadableRamRange(slot->stack_low, slot->stack_high - 1u)) {
            context->task_stack_low = 0u;
            context->task_stack_high = 0u;
            return;
        }

        if ((context->raw_sp >= slot->stack_low) &&
            (context->raw_sp < slot->stack_high)) {
            context->task_stack_current = context->raw_sp;
        } else if ((context->psp >= slot->stack_low) &&
                   (context->psp < slot->stack_high)) {
            /* A handler-mode fault can use MSP while the task remains on PSP. */
            context->task_stack_current = context->psp;
        } else {
            context->task_stack_current = 0u;
        }

        const volatile uint8_t *stack =
            (const volatile uint8_t *)slot->stack_low;
        const size_t stackSize = (size_t)(slot->stack_high - slot->stack_low);
        size_t unused = 0u;
        while ((unused < stackSize) && (stack[unused] == 0xA5u)) ++unused;
        context->task_stack_high_water_bytes = (uint32_t)unused;
        return;
    }
}

static void captureStackSnapshot(SRNOS_Fault_Context *context)
{
    if (context->raw_sp == 0u) return;
    const uintptr_t start = context->raw_sp & ~(uintptr_t)0x3u;
    context->stack_snapshot_address = start;
    const bool snapshotIsTaskStack =
        (context->task_stack_low != 0u) &&
        (start >= context->task_stack_low) &&
        (start <= (context->task_stack_high - sizeof(uint32_t)));

    for (size_t index = 0u; index < SRNOS_FAULT_STACK_SNAPSHOT_WORDS; ++index) {
        if (start > (UINTPTR_MAX - (index * sizeof(uint32_t)))) break;
        const uintptr_t address = start + (index * sizeof(uint32_t));
        if (snapshotIsTaskStack &&
            ((address < context->task_stack_low) ||
             (address > (context->task_stack_high - sizeof(uint32_t))))) {
            break;
        }
        const uint32_t *word = (const uint32_t *)address;
        if (!isReadableWords(word, 1u)) break;
        context->stack_snapshot[index] = *(const volatile uint32_t *)word;
        context->stack_snapshot_count++;
    }
}

static void capturePlatformState(SRNOS_Fault_Context *context,
                                 uintptr_t mainStackPointer,
                                 uintptr_t processStackPointer,
                                 uintptr_t rawStackPointer)
{
    context->msp = mainStackPointer;
    context->psp = processStackPointer;
    context->raw_sp = rawStackPointer;
    context->ipsr = __get_IPSR();
    context->control = __get_CONTROL();
    context->primask = __get_PRIMASK();
    context->basepri = __get_BASEPRI();
    context->faultmask = __get_FAULTMASK();
    context->cfsr = SCB->CFSR;
    context->hfsr = SCB->HFSR;
    context->dfsr = SCB->DFSR;
    context->afsr = SCB->AFSR;
    context->mmfar = SCB->MMFAR;
    context->bfar = SCB->BFAR;
    context->shcsr = SCB->SHCSR;
    context->ccr = SCB->CCR;
    context->icsr = SCB->ICSR;
    context->abfsr = SCB->ABFSR;
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1U)
    context->fpccr = FPU->FPCCR;
#endif
    captureTaskMetadata(context);
    captureStackSnapshot(context);
}

void SRNOS_Platform_FaultCapture(SRNOS_Fault_Context *context)
{
    if (context == NULL) return;
    uint32_t linkRegister;
    __asm volatile ("mov %0, lr" : "=r" (linkRegister));
    context->lr = linkRegister;
    context->frame_type = SRNOS_FAULT_FRAME_NONE;
    const uintptr_t msp = (uintptr_t)__get_MSP();
    const uintptr_t psp = (uintptr_t)__get_PSP();
    const uint32_t control = __get_CONTROL();
    const uintptr_t rawSp = ((__get_IPSR() == 0u) &&
                             ((control & (1u << 1u)) != 0u)) ? psp : msp;
    capturePlatformState(context, msp, psp, rawSp);
}

static const char *reasonName(uint32_t reason)
{
    switch ((SRNOS_Fault_Reason)reason) {
    case SRNOS_FAULT_REASON_ASSERTION: return "Assertion";
    case SRNOS_FAULT_REASON_EXPLICIT_PANIC: return "ExplicitPanic";
    case SRNOS_FAULT_REASON_HAL_ERROR: return "HalError";
    case SRNOS_FAULT_REASON_KERNEL_STACK_OVERFLOW: return "StackOverflow";
    case SRNOS_FAULT_REASON_HARDFAULT: return "HardFault";
    case SRNOS_FAULT_REASON_MEMMANAGE: return "MemManage";
    case SRNOS_FAULT_REASON_BUSFAULT: return "BusFault";
    case SRNOS_FAULT_REASON_USAGEFAULT: return "UsageFault";
    case SRNOS_FAULT_REASON_NMI: return "NMI";
    case SRNOS_FAULT_REASON_DEBUGMON: return "DebugMon";
    default: return "Unknown";
    }
}

static const char *frameTypeName(uint32_t type)
{
    switch ((SRNOS_Fault_FrameType)type) {
    case SRNOS_FAULT_FRAME_NONE: return "none";
    case SRNOS_FAULT_FRAME_BASIC: return "basic";
    case SRNOS_FAULT_FRAME_EXTENDED_FPU: return "extended-fpu";
    case SRNOS_FAULT_FRAME_LAZY_FPU: return "lazy-fpu";
    default: return "invalid";
    }
}

static void writeDecU32(uint32_t value)
{
    char digits[10];
    size_t count = 0u;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while ((value != 0u) && (count < sizeof(digits)));
    while (count != 0u) {
        (void)SRNOS_Platform_Emergency_WriteByte((uint8_t)digits[--count]);
    }
}

static void writeBoundedString(uintptr_t address, size_t capacity)
{
    if ((address == 0u) || !isReadableAddress(address)) {
        (void)SRNOS_Platform_Emergency_WriteString("<unavailable>");
        return;
    }
    const char *string = (const char *)address;
    for (size_t index = 0u; index < capacity; ++index) {
        const uintptr_t byteAddress = address + index;
        if ((byteAddress < address) || !isReadableAddress(byteAddress)) break;
        const uint8_t byte = *(const volatile uint8_t *)&string[index];
        if (byte == 0u) return;
        (void)SRNOS_Platform_Emergency_WriteByte(byte);
    }
}

static bool reportCause(uint32_t cfsr, uint32_t mask, const char *description)
{
    if ((cfsr & mask) == 0u) return false;
    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][CAUSE] ");
    (void)SRNOS_Platform_Emergency_WriteString(description);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    return true;
}

static void reportCfsrCauses(uint32_t cfsr)
{
    bool reported = false;
    reported |= reportCause(cfsr, SCB_CFSR_UNDEFINSTR_Msk,
                            "UsageFault: UNDEFINSTR");
    reported |= reportCause(cfsr, SCB_CFSR_INVSTATE_Msk,
                            "UsageFault: INVSTATE");
    reported |= reportCause(cfsr, SCB_CFSR_INVPC_Msk,
                            "UsageFault: INVPC");
    reported |= reportCause(cfsr, SCB_CFSR_NOCP_Msk,
                            "UsageFault: NOCP");
    reported |= reportCause(cfsr, SCB_CFSR_UNALIGNED_Msk,
                            "UsageFault: UNALIGNED");
    reported |= reportCause(cfsr, SCB_CFSR_DIVBYZERO_Msk,
                            "UsageFault: DIVBYZERO");
    reported |= reportCause(cfsr, SCB_CFSR_IACCVIOL_Msk,
                            "MemManage: IACCVIOL");
    reported |= reportCause(cfsr, SCB_CFSR_DACCVIOL_Msk,
                            "MemManage: DACCVIOL");
    reported |= reportCause(cfsr, SCB_CFSR_MUNSTKERR_Msk,
                            "MemManage: MUNSTKERR");
    reported |= reportCause(cfsr, SCB_CFSR_MSTKERR_Msk,
                            "MemManage: MSTKERR");
    reported |= reportCause(cfsr, SCB_CFSR_MLSPERR_Msk,
                            "MemManage: MLSPERR");
    reported |= reportCause(cfsr, SCB_CFSR_IBUSERR_Msk,
                            "BusFault: IBUSERR");
    reported |= reportCause(cfsr, SCB_CFSR_PRECISERR_Msk,
                            "BusFault: PRECISERR");
    reported |= reportCause(cfsr, SCB_CFSR_IMPRECISERR_Msk,
                            "BusFault: IMPRECISERR");
    reported |= reportCause(cfsr, SCB_CFSR_UNSTKERR_Msk,
                            "BusFault: UNSTKERR");
    reported |= reportCause(cfsr, SCB_CFSR_STKERR_Msk,
                            "BusFault: STKERR");
    reported |= reportCause(cfsr, SCB_CFSR_LSPERR_Msk,
                            "BusFault: LSPERR");
    if (!reported) {
        (void)SRNOS_Platform_Emergency_WriteString(
            "[SRNOS::Fault][CAUSE] no CFSR cause bit set\r\n");
    }
}

void SRNOS_Platform_FaultReport(const SRNOS_Fault_Record *record)
{
    __disable_irq();
    if (s_emergency_write == NULL) return;
    if (record == NULL) {
        (void)SRNOS_Platform_Emergency_WriteString(
            "\r\n[SRNOS::Fault] missing record\r\n");
        return;
    }

    (void)SRNOS_Platform_Emergency_WriteString(
        "\r\n[SRNOS::Fault] === SirinOS panic ===\r\nReason=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->reason);
    (void)SRNOS_Platform_Emergency_WriteString("(");
    (void)SRNOS_Platform_Emergency_WriteString(reasonName(record->reason));
    (void)SRNOS_Platform_Emergency_WriteString(") Sequence=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->sequence);
    (void)SRNOS_Platform_Emergency_WriteString(" Line=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->line);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    reportCfsrCauses(record->cfsr);

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][FRAME] type=");
    (void)SRNOS_Platform_Emergency_WriteString(frameTypeName(record->frame_type));
    (void)SRNOS_Platform_Emergency_WriteString(" EXC_RETURN=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->exc_return);
    (void)SRNOS_Platform_Emergency_WriteString(" FPCCR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->fpccr);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][CORE] R0=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->r0);
    (void)SRNOS_Platform_Emergency_WriteString(" R1=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->r1);
    (void)SRNOS_Platform_Emergency_WriteString(" R2=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->r2);
    (void)SRNOS_Platform_Emergency_WriteString(" R3=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->r3);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n[SRNOS::Fault][CORE] R12=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->r12);
    (void)SRNOS_Platform_Emergency_WriteString(" LR=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->lr);
    (void)SRNOS_Platform_Emergency_WriteString(" PC=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->pc);
    (void)SRNOS_Platform_Emergency_WriteString(" xPSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->xpsr);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][SP] raw=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->raw_sp);
    (void)SRNOS_Platform_Emergency_WriteString(" frame=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->frame_address);
    (void)SRNOS_Platform_Emergency_WriteString(" MSP=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->msp);
    (void)SRNOS_Platform_Emergency_WriteString(" PSP=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->psp);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][CPU] CONTROL=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->control);
    (void)SRNOS_Platform_Emergency_WriteString(" PRIMASK=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->primask);
    (void)SRNOS_Platform_Emergency_WriteString(" BASEPRI=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->basepri);
    (void)SRNOS_Platform_Emergency_WriteString(" FAULTMASK=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->faultmask);
    (void)SRNOS_Platform_Emergency_WriteString(" IPSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->ipsr);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][FAULT] CFSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->cfsr);
    (void)SRNOS_Platform_Emergency_WriteString(" HFSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->hfsr);
    (void)SRNOS_Platform_Emergency_WriteString(" DFSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->dfsr);
    (void)SRNOS_Platform_Emergency_WriteString(" AFSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->afsr);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n[SRNOS::Fault][ADDR] MMFAR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->mmfar);
    (void)SRNOS_Platform_Emergency_WriteString(" valid=");
    (void)SRNOS_Platform_Emergency_WriteString(
        ((record->cfsr & SCB_CFSR_MMARVALID_Msk) != 0u) ? "yes" : "no");
    (void)SRNOS_Platform_Emergency_WriteString(" BFAR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->bfar);
    (void)SRNOS_Platform_Emergency_WriteString(" valid=");
    (void)SRNOS_Platform_Emergency_WriteString(
        ((record->cfsr & SCB_CFSR_BFARVALID_Msk) != 0u) ? "yes" : "no");
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][SYS] SHCSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->shcsr);
    (void)SRNOS_Platform_Emergency_WriteString(" CCR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->ccr);
    (void)SRNOS_Platform_Emergency_WriteString(" ICSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->icsr);
    (void)SRNOS_Platform_Emergency_WriteString(" ABFSR=0x");
    SRNOS_Platform_Emergency_WriteHexU32(record->abfsr);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");

    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][TASK] handle=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->task);
    (void)SRNOS_Platform_Emergency_WriteString(" name=");
    if (record->task_name[0] != '\0') {
        (void)SRNOS_Platform_Emergency_WriteString(record->task_name);
    } else {
        (void)SRNOS_Platform_Emergency_WriteString("<unavailable>");
    }
    (void)SRNOS_Platform_Emergency_WriteString(" stack=[0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->task_stack_low);
    (void)SRNOS_Platform_Emergency_WriteString(",0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->task_stack_high);
    (void)SRNOS_Platform_Emergency_WriteString(") current=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->task_stack_current);
    (void)SRNOS_Platform_Emergency_WriteString(" high_water=");
    writeDecU32(record->task_stack_high_water_bytes);
    (void)SRNOS_Platform_Emergency_WriteString("B\r\n");

    if (record->file != 0u) {
        (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][SOURCE] file=");
        writeBoundedString(record->file, 160u);
        (void)SRNOS_Platform_Emergency_WriteString(" function=");
        writeBoundedString(record->function, 96u);
        (void)SRNOS_Platform_Emergency_WriteString(" line=");
        writeDecU32(record->line);
        (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    }
    if (record->expression != 0u) {
        (void)SRNOS_Platform_Emergency_WriteString(
            "[SRNOS::Fault][SOURCE] expression=");
        writeBoundedString(record->expression, 160u);
        (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    }

    uint32_t snapshotCount = record->stack_snapshot_count;
    if (snapshotCount > SRNOS_FAULT_STACK_SNAPSHOT_WORDS) {
        snapshotCount = SRNOS_FAULT_STACK_SNAPSHOT_WORDS;
    }
    (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][STACK] start=0x");
    SRNOS_Platform_Emergency_WriteHexU32((uint32_t)record->stack_snapshot_address);
    (void)SRNOS_Platform_Emergency_WriteString(" words=");
    writeDecU32(snapshotCount);
    (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    for (uint32_t index = 0u; index < snapshotCount; index += 4u) {
        (void)SRNOS_Platform_Emergency_WriteString("[SRNOS::Fault][STACK+");
        SRNOS_Platform_Emergency_WriteHexU8((uint8_t)(index * sizeof(uint32_t)));
        (void)SRNOS_Platform_Emergency_WriteString("]");
        const uint32_t lineCount =
            ((snapshotCount - index) < 4u) ? (snapshotCount - index) : 4u;
        for (uint32_t word = 0u; word < lineCount; ++word) {
            (void)SRNOS_Platform_Emergency_WriteString(" 0x");
            SRNOS_Platform_Emergency_WriteHexU32(
                record->stack_snapshot[index + word]);
        }
        (void)SRNOS_Platform_Emergency_WriteString("\r\n");
    }

#if (SRNOS_PANIC_ACTION == SRNOS_PANIC_ACTION_HALT)
    (void)SRNOS_Platform_Emergency_WriteString(
        "Halting after fault hook...\r\n");
#else
    (void)SRNOS_Platform_Emergency_WriteString(
        "Resetting after fault hook...\r\n");
#endif
    SRNOS_Platform_Emergency_Flush();
}

void SRNOS_Platform_PanicTerminate(void)
{
    __disable_irq();
    SRNOS_Platform_Emergency_Flush();
#if (SRNOS_PANIC_ACTION == SRNOS_PANIC_ACTION_HALT)
    for (;;) {
        __DSB();
        __NOP();
    }
#else
    NVIC_SystemReset();
    for (;;) __NOP();
#endif
}

void SRNOS_STM32H7_HandleException(uint32_t *mainStackPointer,
                                uint32_t *processStackPointer,
                                uint32_t exceptionReturn,
                                SRNOS_Fault_Reason reason)
{
    const uint32_t *rawStack =
        ((exceptionReturn & (1u << 2u)) != 0u) ?
        processStackPointer : mainStackPointer;
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1U)
    const uint32_t fpccr = FPU->FPCCR;
#else
    const uint32_t fpccr = 0u;
#endif
    const SRNOS_STM32H7_FrameDecode decoded =
        findCoreFrame(rawStack, exceptionReturn, fpccr);

    SRNOS_Fault_Context *context = &s_exception_context;
    (void)SRNOS_Memory_Fill(
        SRNOS_Memory_MutableBytes(context, sizeof(*context)), 0u);
    context->reason = reason;
    context->frame_type = decoded.type;
    context->frame_address = (uintptr_t)decoded.core_frame;
    context->exc_return = exceptionReturn;
    capturePlatformState(context,
                         (uintptr_t)mainStackPointer,
                         (uintptr_t)processStackPointer,
                         (uintptr_t)rawStack);

    if (decoded.core_frame != NULL) {
        context->r0 = decoded.core_frame[0];
        context->r1 = decoded.core_frame[1];
        context->r2 = decoded.core_frame[2];
        context->r3 = decoded.core_frame[3];
        context->r12 = decoded.core_frame[4];
        context->lr = (uintptr_t)decoded.core_frame[5];
        context->pc = (uintptr_t)decoded.core_frame[6];
        context->xpsr = decoded.core_frame[7];
    } else {
        context->lr = exceptionReturn;
    }
    SRNOS_Fault_PanicContext(context);
}
