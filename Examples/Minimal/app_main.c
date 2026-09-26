#include "SirinEmbedOS/Kernel/Startup.h"
#include "cmsis_os2.h"

/* Observe this in the debugger; no actuator or communication is started here. */
volatile uint32_t sirinos_minimal_heartbeat;

void SRNOS_ApplicationMain(void)
{
    for (;;) {
        ++sirinos_minimal_heartbeat;
        osDelay(1000U); /* Kernel tick is fixed at 1 kHz. */
    }
}
