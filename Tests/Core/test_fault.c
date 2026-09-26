#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "SirinEmbedOS/Core/C/Fault.h"

static jmp_buf s_termination_jump;
static uint32_t s_reports;
static uint32_t s_hooks;
static uint32_t s_terminations;

void SRNOS_Platform_FaultCapture(SRNOS_Fault_Context *context)
{
    context->pc = 0x12345678u;
    context->lr = 0x87654321u;
    context->ipsr = 6u;
    context->cfsr = 0x01000000u;
    context->frame_type = SRNOS_FAULT_FRAME_BASIC;
    context->raw_sp = 0x24001000u;
    context->frame_address = 0x24001000u;
    context->msp = 0x20010000u;
    context->psp = 0x24001000u;
    context->exc_return = 0xFFFFFFFDu;
    context->r0 = 1u;
    context->r1 = 2u;
    context->r2 = 3u;
    context->r3 = 4u;
    context->r12 = 12u;
    context->xpsr = 0x61000000u;
    context->task_name[0] = 't';
    context->task_name[1] = 'e';
    context->task_name[2] = 's';
    context->task_name[3] = 't';
    context->task_stack_low = 0x24000000u;
    context->task_stack_high = 0x24002000u;
    context->task_stack_current = 0x24001000u;
    context->task_stack_high_water_bytes = 256u;
    context->stack_snapshot_address = 0x24001000u;
    context->stack_snapshot_count = 2u;
    context->stack_snapshot[0] = 0x11111111u;
    context->stack_snapshot[1] = 0x22222222u;
}

void SRNOS_Platform_FaultReport(const SRNOS_Fault_Record *record)
{
    assert(record != NULL);
    s_reports++;
}

void SRNOS_Fault_OnReportDone(void)
{
    s_hooks++;
}

_Noreturn void SRNOS_Platform_PanicTerminate(void)
{
    s_terminations++;
    longjmp(s_termination_jump, 1);
}

int main(void)
{
    SRNOS_Fault_ClearLastRecord();
    SRNOS_Fault_Initialize();

    if (setjmp(s_termination_jump) == 0) {
        SRNOS_Fault_Panic(SRNOS_FAULT_REASON_ASSERTION, "value != 0", "fault.c",
                       "test", 42u);
    }

    assert(s_reports == 1u && s_hooks == 1u && s_terminations == 1u);
    SRNOS_Fault_Record record;
    assert(SRNOS_Fault_LastRecord(&record));
    assert(record.reason == SRNOS_FAULT_REASON_ASSERTION);
    assert(record.line == 42u);
    assert(record.pc == 0x12345678u);
    assert(record.lr == 0x87654321u);
    assert(record.version == SRNOS_FAULT_RECORD_VERSION);
    assert(record.frame_type == SRNOS_FAULT_FRAME_BASIC);
    assert(record.r0 == 1u && record.r12 == 12u);
    assert(record.raw_sp == 0x24001000u);
    assert(strcmp(record.task_name, "test") == 0);
    assert(record.task_stack_high_water_bytes == 256u);
    assert(record.stack_snapshot_count == 2u);
    assert(record.stack_snapshot[0] == 0x11111111u);
    assert(record.stack_snapshot[1] == 0x22222222u);

    /* A second Panic before reinitialization must terminate recursively. */
    if (setjmp(s_termination_jump) == 0) {
        SRNOS_Fault_Panic(SRNOS_FAULT_REASON_EXPLICIT_PANIC, NULL, NULL, NULL, 0u);
    }
    assert(s_reports == 1u && s_hooks == 1u && s_terminations == 2u);

    SRNOS_Fault_Initialize();
    const uint32_t manual_line = (uint32_t)__LINE__ + 2u;
    if (setjmp(s_termination_jump) == 0) {
        SRNOS_TRIGGER_PANIC();
    }
    assert(s_reports == 2u && s_hooks == 2u && s_terminations == 3u);
    assert(SRNOS_Fault_LastRecord(&record));
    assert(record.reason == SRNOS_FAULT_REASON_EXPLICIT_PANIC);
    assert(record.line == manual_line);
    assert(record.file != 0u);
    assert(record.expression != 0u);
    assert(strcmp((const char *)record.expression, "Manual panic trigger") == 0);

    SRNOS_Fault_ClearLastRecord();
    assert(!SRNOS_Fault_LastRecord(&record));
    return 0;
}
