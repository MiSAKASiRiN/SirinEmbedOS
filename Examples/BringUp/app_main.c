#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include <string.h>
#include "SirinEmbedOS/Services/SystemMonitor.h"

/* Debugger observation only, not an SDK ABI or a concurrent reader API. Stop
 * the core and inspect when sampling == false. No allocation for snapshots. */
#define BRINGUP_MAX_TASKS 64u
typedef struct {
    char name[16];
    uint32_t priority;
    uint32_t state;
    uint32_t minimum_free_stack_bytes;
} SRNOS_BringUpTask;

typedef struct {
    bool sampling;
    uint32_t samples;
    uint32_t tick_ms;
    uint32_t kernel_tick;
    uint32_t kernel_tick_hz;
    SRNOS_SystemStatus system;
    bool configuration_frozen;
    uint32_t startup_heap_budget_bytes;
    SRNOS_KernelResources heap;
    uint32_t task_count;
    uint32_t captured_task_count;
    bool task_snapshot_complete;
    SRNOS_BringUpTask tasks[BRINGUP_MAX_TASKS];
    bool imu_snapshot_available;
    SRNOS_INSSnapshot imu;
    uint32_t internal_imu_age_ms; /* UINT32_MAX when no snapshot is available. */
    bool internal_imu_recent;    /* online && valid && age <= 100 ms; not calibrated proof. */
    SRNOS_LogStats logging;
} SRNOS_BringUpSnapshot;

volatile SRNOS_BringUpSnapshot srnos_bringup;
static SRNOS_BringUpSnapshot snapshot;


void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    /* No business parameter restore in this diagnostic application. Fixed
     * hardware services retain their defaults except the disabled heater. */
    config->load_parameters = false;
    config->sensors.heater_enabled = false;
    config->bringup_indicators = true;
}

static void capture(void)
{
    srnos_bringup.sampling = true;
    snapshot.sampling = true;
    snapshot.kernel_tick = SRNOS_TimeMs();
    snapshot.kernel_tick_hz = 1000u;
    SRNOS_SystemGetStatus(&snapshot.system);
    snapshot.configuration_frozen = SRNOS_SystemConfigurationFrozen();
    snapshot.startup_heap_budget_bytes = SRNOS_SystemHeapBudget(SRNOS_SystemGetConfig());
    SRNOS_KernelGetResources(&snapshot.heap);
    SRNOS_SystemMonitorSnapshot monitor;SRNOS_SystemMonitorGetSnapshot(&monitor);
    snapshot.task_count=monitor.task_count;snapshot.captured_task_count=0;
    memset(snapshot.tasks,0,sizeof(snapshot.tasks));
    for(uint8_t slot=1;slot<=SRNOS_MONITOR_MAX_TASKS;slot++) {
        SRNOS_MonitorTask task;SRNOS_SystemMonitorGetTask(slot,&task);
        if(!(task.flags&SRNOS_MONITOR_TASK_ACTIVE))continue;
        SRNOS_BringUpTask *out=&snapshot.tasks[snapshot.captured_task_count++];
        memcpy(out->name,task.name,16);out->priority=task.priority;out->state=task.state;
        out->minimum_free_stack_bytes=UINT32_MAX; /* Intentionally unmeasured. */
    }
    snapshot.task_snapshot_complete=snapshot.captured_task_count==snapshot.task_count;

    memset(&snapshot.imu, 0, sizeof(snapshot.imu));
    snapshot.imu_snapshot_available = SRNOS_INSGetSnapshot(&snapshot.imu);
    snapshot.tick_ms = SRNOS_TimeMs();
    const uint64_t now=SRNOS_TimeGetUptimeUs();
    snapshot.internal_imu_age_ms=snapshot.imu.attitude.attitude_us && now>=snapshot.imu.attitude.attitude_us?
        (uint32_t)((now-snapshot.imu.attitude.attitude_us)/1000):UINT32_MAX;
    snapshot.internal_imu_recent=(snapshot.imu.attitude.flags&SRNOS_INS_VALID)!=0;
    SRNOS_LogGetStats(&snapshot.logging);
    ++snapshot.samples;
    srnos_bringup = snapshot;
    srnos_bringup.sampling = false;
}

static SRNOS_StepResult application_step(void *arg,const SRNOS_ThreadWake *wake)
{
 (void)arg;(void)wake;

        capture();
        SRNOS_LOGI_SRC("BringUp", "sample=%lu phase=%u SRNOS_CANInit=%u usb_start=%u usb_configured=%lu",
                 (unsigned long)snapshot.samples, (unsigned)snapshot.system.phase,
                 (unsigned)snapshot.system.native_can_ready, (unsigned)snapshot.system.usb_ready,
                 (unsigned long)snapshot.logging.usb_configured);
        SRNOS_LOGI_SRC("BringUp", "heap_free=%lu min=%lu pool_used=%lu tasks=%lu/%lu imu_recent=%u age_ms=%lu",
                 (unsigned long)snapshot.heap.kernel_free_bytes,
                 (unsigned long)snapshot.heap.kernel_minimum_free_bytes,
                 (unsigned long)snapshot.heap.used_bytes,
                 (unsigned long)snapshot.captured_task_count, (unsigned long)snapshot.task_count,
                 (unsigned)snapshot.internal_imu_recent,
                 (unsigned long)snapshot.internal_imu_age_ms);


 return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result srnos_example_create_result;
static SRNOS_Thread application_task;
void SRNOS_ApplicationMain(void)
{
 const SRNOS_ThreadConfig task={.name="BringUpView",.step=application_step,.stack_bytes=4096,.priority=24,.mode=SRNOS_TASK_PERIODIC,.period_ms=5000};
 srnos_example_create_result=SRNOS_ThreadCreate(&task,&application_task);
}
