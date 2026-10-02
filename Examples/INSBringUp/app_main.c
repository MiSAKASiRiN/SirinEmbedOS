#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include "SirinEmbedOS/IO/CAN/External/ExternalCAN.h"
#include "SirinEmbedOS/IO/SPI/SPI.h"
/* Debugger-only snapshot: stop outside sampling. Never scan all task stacks
 * inside a measurement window. Normal applications use the public getters. */
volatile struct {
    bool sampling;
    uint32_t sequence,free_heap,minimum_heap;
    SRNOS_INSSnapshot ins;
    SRNOS_SPIStatistics mcp;
} srnos_ins_bringup;
static SRNOS_INSSnapshot sample;
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->bringup_indicators=true;c->system_monitor_enabled=false;
    c->load_parameters=false;c->host_link_storage_enabled=false;
}
static SRNOS_StepResult application_step(void *arg,const SRNOS_ThreadWake *wake)
{
 (void)arg;(void)wake;

        SRNOS_INSGetSnapshot(&sample);
        srnos_ins_bringup.sampling=true;srnos_ins_bringup.ins=sample;
        SRNOS_KernelResources resources;SRNOS_KernelGetResources(&resources);
        srnos_ins_bringup.free_heap=resources.kernel_free_bytes;srnos_ins_bringup.minimum_heap=resources.kernel_minimum_free_bytes;
        SRNOS_SPIStatistics mcp;SRNOS_ExternalCANGetSPIStatistics(&mcp);srnos_ins_bringup.mcp=mcp;
        ++srnos_ins_bringup.sequence;srnos_ins_bringup.sampling=false;
        SRNOS_LOGI("INS phase=%u seq=%lu epoch=%lu src=%u flags=%lx q=%.3f/%.3f/%.3f/%.3f",
            sample.status.phase,(unsigned long)sample.attitude.sequence,(unsigned long)sample.status.epoch,
            sample.attitude.source_mask,(unsigned long)sample.status.flags,
            (double)sample.attitude.quaternion_wxyz[0],(double)sample.attitude.quaternion_wxyz[1],
            (double)sample.attitude.quaternion_wxyz[2],(double)sample.attitude.quaternion_wxyz[3]);
        SRNOS_LOGI("INS heat src=%d T=%.2f target=%.1f PWM=%.1f%% stable=%u protection=%u execution=%lu us copy=%lu us",
            sample.status.heater.source,(double)sample.status.heater.feedback_c,(double)sample.status.heater.target_c,
            (double)(sample.status.heater.duty*100),sample.status.heater.thermal_ready,sample.status.heater.overtemperature,
            (unsigned long)sample.status.max_execution_us,(unsigned long)sample.status.max_copy_us);


 return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result srnos_example_create_result;
static SRNOS_Thread application_task;
void SRNOS_ApplicationMain(void)
{
 const SRNOS_ThreadConfig task={.name="INSView",.step=application_step,.stack_bytes=4096,.priority=24,.mode=SRNOS_TASK_PERIODIC,.period_ms=1000};
 srnos_example_create_result=SRNOS_ThreadCreate(&task,&application_task);
}
