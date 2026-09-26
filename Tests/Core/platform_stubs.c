#include <stdlib.h>

#include "SirinEmbedOS/Core/C/Fault.h"

void SRNOS_Platform_FaultCapture(SRNOS_Fault_Context *context)
{
    (void)context;
}

void SRNOS_Platform_FaultReport(const SRNOS_Fault_Record *record)
{
    (void)record;
}

_Noreturn void SRNOS_Platform_PanicTerminate(void)
{
    abort();
}
