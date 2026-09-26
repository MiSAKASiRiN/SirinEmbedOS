#include "SirinEmbedOS/Diagnostics/Runtime/Startup.h"
#include "SirinEmbedOS/Diagnostics/Runtime/tskptt_hskp.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"

static bool s_prepared;
static bool s_enabled;
static bool s_started;

bool SRNOS_DiagnosticsPrepare(bool enable_sampling)
{
    if (s_prepared || osKernelGetState() != osKernelInactive) return false;
    DWT_Init(0u);
    s_enabled = enable_sampling;
    s_prepared = true;
    return DWT_IsReady() != 0u;
}
bool SRNOS_DiagnosticsStart(void)
{
    if (!s_prepared || s_started || osKernelGetState() != osKernelReady) return false;
    if (s_enabled) {
        const osThreadAttr_t attr = {
            .name = "Diagnostics", .stack_size = 4096u, .priority = osPriorityLow
        };
        if (osThreadNew(TSKPTT_HSKP_Task, NULL, &attr) == NULL) return false;
    }
    s_started = true;
    return true;
}
