#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Services/Telemetry.hpp"
namespace {
SRNOS::Telemetry::Value<uint32_t> cycles{"application.cycles","count",1000};
SRNOS_Thread task;
uint32_t completed;
SRNOS_StepResult step(void *,const SRNOS_ThreadWake *)
{
    return cycles.Publish(++completed,SRNOS_TimeUs())==SRNOS_TELEMETRY_OK ? SRNOS_STEP_CONTINUE : SRNOS_STEP_FAILED;
}
}
extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{ config->usb_enabled=true;config->host_link_enabled=true; }
extern "C" void SRNOS_ApplicationMain(void)
{
    if(cycles.Status()!=SRNOS_TELEMETRY_OK)return;
    SRNOS_ThreadConfig config{};config.name="TelemetryDemo";config.step=step;
    config.stack_bytes=2048;config.priority=8;config.mode=SRNOS_TASK_PERIODIC;config.period_ms=1;
    (void)SRNOS_ThreadCreate(&config,&task);
}
