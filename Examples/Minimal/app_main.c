#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/Thread.h"

/* Observe this in the debugger; no actuator or communication is started here. */
volatile uint32_t sirinos_minimal_heartbeat;

static SRNOS_StepResult application_step(void *arg,const SRNOS_ThreadWake *wake)
{
 (void)arg;(void)wake;

        ++sirinos_minimal_heartbeat;


 return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result srnos_example_create_result;
static SRNOS_Thread application_task;
void SRNOS_ApplicationMain(void)
{
 const SRNOS_ThreadConfig task={.name="Heartbeat",.step=application_step,.stack_bytes=4096,.priority=24,.mode=SRNOS_TASK_PERIODIC,.period_ms=1000};
 srnos_example_create_result=SRNOS_ThreadCreate(&task,&application_task);
}
