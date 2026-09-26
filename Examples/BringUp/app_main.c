#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Devices/IMU/Worker/tskptt_imu.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include <string.h>

/* Debugger observation only, not an SDK ABI or a concurrent reader API. Stop
 * the core and inspect when sampling == false. No allocation for snapshots. */
#define BRINGUP_MAX_TASKS 64u
typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    uint32_t priority;
    eTaskState state;
    uint32_t minimum_free_stack_bytes;
} SRNOS_BringUpTask;

typedef struct {
    bool sampling;
    uint32_t samples;
    uint32_t hal_tick_ms;
    uint32_t kernel_tick;
    uint32_t kernel_tick_hz;
    SRNOS_SystemStatus system;
    bool configuration_frozen;
    uint32_t startup_heap_budget_bytes;
    HeapStats_t heap; /* FreeRTOS heap_5 only, excludes libc and MSP. */
    uint32_t task_count;
    uint32_t captured_task_count;
    bool task_snapshot_complete;
    SRNOS_BringUpTask tasks[BRINGUP_MAX_TASKS];
    bool imu_snapshot_available;
    imu_task_snapshot_t imu;
    uint32_t internal_imu_age_ms; /* UINT32_MAX when no snapshot is available. */
    bool internal_imu_recent;    /* online && valid && age <= 100 ms; not calibrated proof. */
    srn_log_stats_t logging;
} SRNOS_BringUpSnapshot;

volatile SRNOS_BringUpSnapshot srnos_bringup;
static SRNOS_BringUpSnapshot snapshot;
static TaskStatus_t task_status[BRINGUP_MAX_TASKS];

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    /* No business parameter restore in this diagnostic application. Fixed
     * hardware services (including IMU heating/calibration) keep SDK defaults. */
    config->load_parameters = false;
}

static void capture(void)
{
    srnos_bringup.sampling = true;
    snapshot.sampling = true;
    snapshot.kernel_tick = osKernelGetTickCount();
    snapshot.kernel_tick_hz = osKernelGetTickFreq();
    SRNOS_SystemGetStatus(&snapshot.system);
    snapshot.configuration_frozen = SRNOS_SystemConfigurationFrozen();
    snapshot.startup_heap_budget_bytes = SRNOS_SystemHeapBudget(SRNOS_SystemGetConfig());
    vPortGetHeapStats(&snapshot.heap);

    /* Scans stack fill patterns with the scheduler suspended. This deliberately
     * intrusive diagnostic must not be used as a real-time load benchmark.
     * Keep task names alive until copied, including tasks deleted at runtime. */
    vTaskSuspendAll();
    snapshot.task_count = (uint32_t)uxTaskGetNumberOfTasks();
    UBaseType_t count = uxTaskGetSystemState(task_status, BRINGUP_MAX_TASKS, NULL);
    snapshot.captured_task_count = (uint32_t)count;
    snapshot.task_snapshot_complete = count != 0 && count == snapshot.task_count;
    memset(snapshot.tasks, 0, sizeof(snapshot.tasks));
    for (UBaseType_t i = 0; i < count; ++i) {
        strncpy(snapshot.tasks[i].name, task_status[i].pcTaskName,
                sizeof(snapshot.tasks[i].name) - 1u);
        snapshot.tasks[i].priority = (uint32_t)task_status[i].uxCurrentPriority;
        snapshot.tasks[i].state = task_status[i].eCurrentState;
        snapshot.tasks[i].minimum_free_stack_bytes =
            (uint32_t)task_status[i].usStackHighWaterMark * sizeof(StackType_t);
    }
    (void)xTaskResumeAll();

    memset(&snapshot.imu, 0, sizeof(snapshot.imu));
    snapshot.imu_snapshot_available = IMU_GetSnapshot(&snapshot.imu);
    /* The IMU uses HAL milliseconds, not the scheduler's tick epoch. */
    snapshot.hal_tick_ms = HAL_GetTick();
    snapshot.internal_imu_age_ms = snapshot.imu_snapshot_available ?
        snapshot.hal_tick_ms - snapshot.imu.internal.last_update_ms : UINT32_MAX;
    snapshot.internal_imu_recent = snapshot.imu_snapshot_available &&
        snapshot.imu.internal.online && snapshot.imu.internal.valid &&
        snapshot.internal_imu_age_ms <= 100u;
    SRN_Log_GetStats(&snapshot.logging);
    ++snapshot.samples;
    srnos_bringup = snapshot;
    srnos_bringup.sampling = false;
}

void SRNOS_ApplicationMain(void)
{
    for (;;) {
        capture();
        LOGI_SRC("BringUp", "sample=%lu phase=%u can_init=%u usb_start=%u usb_configured=%lu",
                 (unsigned long)snapshot.samples, (unsigned)snapshot.system.phase,
                 (unsigned)snapshot.system.native_can_ready, (unsigned)snapshot.system.usb_ready,
                 (unsigned long)snapshot.logging.usb_configured);
        LOGI_SRC("BringUp", "heap_free=%lu min=%lu largest=%lu tasks=%lu/%lu imu_recent=%u age_ms=%lu",
                 (unsigned long)snapshot.heap.xAvailableHeapSpaceInBytes,
                 (unsigned long)snapshot.heap.xMinimumEverFreeBytesRemaining,
                 (unsigned long)snapshot.heap.xSizeOfLargestFreeBlockInBytes,
                 (unsigned long)snapshot.captured_task_count, (unsigned long)snapshot.task_count,
                 (unsigned)snapshot.internal_imu_recent,
                 (unsigned long)snapshot.internal_imu_age_ms);
        osDelay(5u * osKernelGetTickFreq());
    }
}
