#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_spi.h"
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
/* Debugger-only snapshot: stop outside sampling. Never scan all task stacks
 * inside a measurement window. Normal applications use the public getters. */
volatile struct {
    bool sampling;
    uint32_t sequence,free_heap,minimum_heap;
    SRNOS_INSSnapshot ins;
    bsp_spi_statistics_t mcp;
} srnos_ins_bringup;
static SRNOS_INSSnapshot sample;
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->bringup_indicators=true;c->diagnostics_enabled=false;
    c->load_parameters=false;c->host_link_storage_enabled=false;
}
void SRNOS_ApplicationMain(void) {
    for(;;) {
        SRNOS_INSGetSnapshot(&sample);
        srnos_ins_bringup.sampling=true;srnos_ins_bringup.ins=sample;
        srnos_ins_bringup.free_heap=xPortGetFreeHeapSize();srnos_ins_bringup.minimum_heap=xPortGetMinimumEverFreeHeapSize();
        bsp_spi_statistics_t mcp;DRV_SPI_GetStatistics(0,&mcp);srnos_ins_bringup.mcp=mcp;
        ++srnos_ins_bringup.sequence;srnos_ins_bringup.sampling=false;
        LOGI("INS phase=%u seq=%lu epoch=%lu src=%u flags=%lx q=%.3f/%.3f/%.3f/%.3f",
            sample.status.phase,(unsigned long)sample.attitude.sequence,(unsigned long)sample.status.epoch,
            sample.attitude.source_mask,(unsigned long)sample.status.flags,
            (double)sample.attitude.quaternion_wxyz[0],(double)sample.attitude.quaternion_wxyz[1],
            (double)sample.attitude.quaternion_wxyz[2],(double)sample.attitude.quaternion_wxyz[3]);
        LOGI("INS heat src=%d T=%.2f target=%.1f PWM=%.1f%% stable=%u protection=%u execution=%lu us copy=%lu us",
            sample.status.heater.source,(double)sample.status.heater.feedback_c,(double)sample.status.heater.target_c,
            (double)(sample.status.heater.duty*100),sample.status.heater.thermal_ready,sample.status.heater.overtemperature,
            (unsigned long)sample.status.max_execution_us,(unsigned long)sample.status.max_copy_us);
        osDelay(1000);
    }
}
