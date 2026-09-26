#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#if (defined(__GNUC__) || defined(__clang__)) && defined(__ELF__)
#define SRNOS_FAULT_NOINIT __attribute__((section(".sirinos_noinit"), aligned(32), used))
#elif defined(__GNUC__) || defined(__clang__)
#define SRNOS_FAULT_NOINIT __attribute__((aligned(32), used))
#else
#define SRNOS_FAULT_NOINIT
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SRNOS_FAULT_WEAK __attribute__((weak))
#else
#define SRNOS_FAULT_WEAK
#endif

static SRNOS_FAULT_NOINIT volatile SRNOS_Fault_Record g_srnos_fault_record;
static SRNOS_Fault_Record g_srnos_fault_scratch;
static SRNOS_Fault_Context g_srnos_fault_context_scratch;
static volatile uint32_t g_srnos_fault_active;

static uint32_t recordChecksum(const SRNOS_Fault_Record *record)
{
    const uint8_t *bytes = (const uint8_t *)&record->version;
    const size_t length = offsetof(SRNOS_Fault_Record, checksum) -
                          offsetof(SRNOS_Fault_Record, version);
    uint32_t hash = 2166136261u;
    for (size_t index = 0u; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

static void copyRecord(SRNOS_Fault_Record *destination,
                       const volatile SRNOS_Fault_Record *source)
{
    uint8_t *out = (uint8_t *)destination;
    const volatile uint8_t *in = (const volatile uint8_t *)source;
    for (size_t index = 0u; index < sizeof(*destination); ++index) {
        out[index] = in[index];
    }
}

void SRNOS_Fault_Initialize(void)
{
    g_srnos_fault_active = 0u;
}

bool SRNOS_Fault_LastRecord(SRNOS_Fault_Record *record)
{
    if (record == NULL) return false;
    copyRecord(record, &g_srnos_fault_record);
    return (record->magic == SRNOS_FAULT_RECORD_MAGIC) &&
           (record->version == SRNOS_FAULT_RECORD_VERSION) &&
           (record->size == sizeof(*record)) &&
           (record->checksum == recordChecksum(record));
}

void SRNOS_Fault_ClearLastRecord(void)
{
    g_srnos_fault_record.magic = 0u;
    g_srnos_fault_record.checksum = 0u;
}

SRNOS_FAULT_WEAK void SRNOS_Fault_OnReportDone(void)
{
}

void SRNOS_Fault_Panic(SRNOS_Fault_Reason reason,
                    const char *expression,
                    const char *file,
                    const char *function,
                    uint32_t line)
{
    SRNOS_Fault_Context *context = &g_srnos_fault_context_scratch;
    (void)SRNOS_Memory_Fill(
        SRNOS_Memory_MutableBytes(context, sizeof(*context)), 0u);
    context->reason = reason;
    context->expression = expression;
    context->file = file;
    context->function = function;
    context->line = line;
    context->pc = (uintptr_t)__builtin_return_address(0);
    SRNOS_Platform_FaultCapture(context);
    SRNOS_Fault_PanicContext(context);
}

void SRNOS_Fault_TriggerPanic(const char *file,
                           const char *function,
                           uint32_t line)
{
    SRNOS_Fault_Context *context = &g_srnos_fault_context_scratch;
    (void)SRNOS_Memory_Fill(
        SRNOS_Memory_MutableBytes(context, sizeof(*context)), 0u);
    context->reason = SRNOS_FAULT_REASON_EXPLICIT_PANIC;
    context->expression = "Manual panic trigger";
    context->file = file;
    context->function = function;
    context->line = line;
    context->pc = (uintptr_t)__builtin_return_address(0);
    SRNOS_Platform_FaultCapture(context);
    SRNOS_Fault_PanicContext(context);
}

void SRNOS_Fault_PanicContext(const SRNOS_Fault_Context *context)
{
    if ((context == NULL) || (g_srnos_fault_active++ != 0u)) {
        SRNOS_Platform_PanicTerminate();
    }

    uint32_t sequence = 1u;
    if (SRNOS_Fault_LastRecord(&g_srnos_fault_scratch)) {
        sequence = g_srnos_fault_scratch.sequence + 1u;
    }

    SRNOS_Fault_Record *record = &g_srnos_fault_scratch;
    if (!SRNOS_Memory_Fill(
            SRNOS_Memory_MutableBytes(record, sizeof(*record)), 0u)) {
        SRNOS_Platform_PanicTerminate();
    }
    record->version = SRNOS_FAULT_RECORD_VERSION;
    record->size = sizeof(SRNOS_Fault_Record);
    record->sequence = sequence;
    record->reason = (uint32_t)context->reason;
    record->line = context->line;
    record->expression = (uintptr_t)context->expression;
    record->file = (uintptr_t)context->file;
    record->function = (uintptr_t)context->function;
    record->pc = context->pc;
    record->lr = context->lr;
    record->task = context->task;
    record->ipsr = context->ipsr;
    record->cfsr = context->cfsr;
    record->hfsr = context->hfsr;
    record->dfsr = context->dfsr;
    record->afsr = context->afsr;
    record->frame_type = (uint32_t)context->frame_type;
    record->raw_sp = context->raw_sp;
    record->frame_address = context->frame_address;
    record->msp = context->msp;
    record->psp = context->psp;
    record->exc_return = context->exc_return;
    record->r0 = context->r0;
    record->r1 = context->r1;
    record->r2 = context->r2;
    record->r3 = context->r3;
    record->r12 = context->r12;
    record->xpsr = context->xpsr;
    record->control = context->control;
    record->primask = context->primask;
    record->basepri = context->basepri;
    record->faultmask = context->faultmask;
    record->mmfar = context->mmfar;
    record->bfar = context->bfar;
    record->shcsr = context->shcsr;
    record->ccr = context->ccr;
    record->icsr = context->icsr;
    record->abfsr = context->abfsr;
    record->fpccr = context->fpccr;
    record->task_stack_low = context->task_stack_low;
    record->task_stack_high = context->task_stack_high;
    record->task_stack_current = context->task_stack_current;
    record->task_stack_high_water_bytes =
        context->task_stack_high_water_bytes;
    record->stack_snapshot_address = context->stack_snapshot_address;
    record->stack_snapshot_count =
        (context->stack_snapshot_count <= SRNOS_FAULT_STACK_SNAPSHOT_WORDS) ?
        context->stack_snapshot_count : SRNOS_FAULT_STACK_SNAPSHOT_WORDS;
    if (!SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(record->task_name,
                                   sizeof(record->task_name)),
            SRNOS_Memory_Bytes(context->task_name,
                            sizeof(context->task_name))) ||
        !SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(record->stack_snapshot,
                                   sizeof(record->stack_snapshot)),
            SRNOS_Memory_Bytes(context->stack_snapshot,
                            sizeof(context->stack_snapshot)))) {
        SRNOS_Platform_PanicTerminate();
    }
    record->checksum = recordChecksum(record);

    g_srnos_fault_record.magic = 0u;
    volatile uint8_t *out = (volatile uint8_t *)&g_srnos_fault_record;
    const uint8_t *in = (const uint8_t *)record;
    for (size_t index = 0u; index < sizeof(*record); ++index) {
        out[index] = in[index];
    }
    g_srnos_fault_record.magic = SRNOS_FAULT_RECORD_MAGIC;

    SRNOS_Platform_FaultReport((const SRNOS_Fault_Record *)&g_srnos_fault_record);
    SRNOS_Fault_OnReportDone();
    SRNOS_Platform_PanicTerminate();
}
