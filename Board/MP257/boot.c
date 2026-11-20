#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "stm32mp2xx.h"
#include "SirinEmbedOS/Kernel/Startup.h"
#include "thread_internal.h"
#include "thread_config.h"
#include "SirinEmbedOS/Services/Parameters/Parameters.h"
#include <string.h>
#include "IO/CAN/can_controller_internal.h"
#include "IO/CAN/can_startup.h"
#include "IO/CAN/FD/can_fd_internal.h"
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/can_board.h"
#include <stdio.h>
#include <picotls.h>
#include "IPC/peer.h"

static char trace[512] __attribute__((section(".remote_trace"), used));
/* Linux remoteproc v1, one RSC_TRACE entry. All device addresses are 32-bit. */
static const struct {
    uint32_t version, count, reserved[2], offset;
    uint32_t type;
    const void *address;
    uint32_t length, padding;
    char name[32];
} resources __attribute__((section(".resource_table"), used)) = {
    1, 1, {0, 0}, 20, 2, trace, sizeof(trace), 0, "SirinEmbedOS"
};
static uint8_t heap[256 * 1024] __attribute__((aligned(8)));
HeapRegion_t SRNOS_HeapRegions[] = {{heap, sizeof(heap)}, {NULL, 0}};
void SRNOS_KernelFailure(const char *reason, const char *file, unsigned line)
{
    __disable_irq();
    /* Faults can occur inside Picolibc or on an ISR stack. Keep this path
     * independent of TLS, formatting routines and recursive libc locks. */
    char digits[11];
    char *end = digits + sizeof(digits);
    *--end = '\0';
    do {
        *--end = (char)('0' + line % 10U);
        line /= 10U;
    } while (line);
    const char *parts[] = {"M33 halted: ", reason ? reason : "?", " at ", file, ":", end, "\n"};
    size_t used = 0;
    for (unsigned i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i)
        for (const char *p = parts[i]; *p && used + 1 < sizeof(trace); ++p)
            trace[used++] = *p;
    trace[used] = '\0';
    __DSB();
    for (;;) __WFI();
}
void vApplicationMallocFailedHook(void) { SRNOS_KernelFailure("heap", __FILE__, __LINE__); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    SRNOS_KernelFailure(name, __FILE__, __LINE__);
}
/* Constructors may use TLS before the task port installs its first block. */
extern char __srnos_boot_tls[];
void __real___libc_init_array(void);
void __wrap___libc_init_array(void)
{
    _init_tls(__srnos_boot_tls);
    _set_tls(__srnos_boot_tls);
    __real___libc_init_array();
}

/* Sample CYCCNT every tick so its 32-bit wrap can be extended. Task and
 * ISR readers update the same anchor under a saved interrupt mask. */
static uint64_t cycles;
static uint32_t last_cycle;
static uint64_t sample_cycles(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = DWT->CYCCNT;
    cycles += (uint32_t)(now - last_cycle);
    last_cycle = now;
    uint64_t value = cycles;
    __set_PRIMASK(mask);
    return value;
}
void vApplicationTickHook(void) { (void)sample_cycles(); }
uint32_t HAL_GetTick(void) { return SRNOS_TimeMs(); }
uint64_t SRNOS_TimeGetUptimeUs(void) { return sample_cycles() / (SystemCoreClock / 1000000U); }

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *depth = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb = &timer_tcb;
    *stack = timer_stack;
    *depth = configTIMER_TASK_STACK_DEPTH;
}

static SRNOS_SystemConfig config;
static SRNOS_SystemStatus status = {
    .identity_load_status=SRNOS_STORAGE_DISABLED,
    .parameter_load_status=SRNOS_PARAMETERS_STATUS_DISABLED
};

void SRNOS_SystemConfigDefaults(SRNOS_SystemConfig *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i)
        c->can[i] = (SRNOS_CANControllerConfig){.mode=SRNOS_CAN_MODE_CLASSIC, .nominal_bitrate=1000000};
    c->application_stack_bytes = 4096;
    c->application_priority = osPriorityNormal;
}

SRNOS_SystemResult SRNOS_SystemValidate(const SRNOS_SystemConfig *c)
{
    if (!c || c->thread_count > SRNOS_SYSTEM_MAX_THREADS || c->application_stack_bytes < 1024 ||
        c->application_stack_bytes > 32768 || (c->application_stack_bytes % 8) ||
        c->application_priority < 1 || c->application_priority >= 40) return SRNOS_SYSTEM_INVALID;
    if (c->ethernet.enabled || c->network.enabled || c->ins.enabled || c->sensors.lsm_enabled ||
        c->sensors.lis_enabled || c->sensors.spa_enabled || c->sensors.heater_enabled ||
        c->serial_count || c->logging_enabled || c->usb_enabled || c->sirin_link_enabled ||
        c->bringup_indicators || c->system_monitor_enabled || c->cycle_statistics_enabled ||
        c->load_parameters || c->storage.enabled || c->blackbox.enabled || c->bind_channels ||
        c->sirin_link_setup || c->sirin_link_storage_enabled || c->referee_port || c->log_port ||
        c->remote.dr16_port || c->remote.wbus_port || c->remote.vt03_port || c->remote.vt02_port ||
        c->remote.sirinrmt_transports || c->remote.video_link_enabled ||
        c->messages.uart_port || c->messages.udp_port) return SRNOS_SYSTEM_INVALID;
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i)
        if (!SRNOS_CANControllerConfigValid(i + 1, &c->can[i])) return SRNOS_SYSTEM_INVALID;
    for (size_t i = 0; i < c->thread_count; ++i) {
        if (!SRNOS_KernelThreadConfigValid(&c->threads[i])) return SRNOS_SYSTEM_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(c->threads[i].name, c->threads[j].name)) return SRNOS_SYSTEM_CONFLICT;
    }
    return SRNOS_SYSTEM_OK;
}

__attribute__((weak)) void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) { (void)c; }
bool SRNOS_SystemConfigurationFrozen(void) { return SRNOS_CAN_ConfigurationFrozen(); }
const SRNOS_SystemConfig *SRNOS_SystemGetConfig(void)
{
    return SRNOS_SystemConfigurationFrozen() ? &config : NULL;
}
void SRNOS_SystemGetStatus(SRNOS_SystemStatus *out)
{
    if (!out) return;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = status;
    __set_PRIMASK(mask);
}

static void ipc_worker(void *arg)
{
    (void)arg;
    TickType_t deadline = xTaskGetTickCount();
    unsigned last_status = SRNOS_TimeMs() - 100u;
    for (;;) {
        unsigned now = SRNOS_TimeMs();
        SRNOS_IpcPoll(now);
        SRNOS_IpcPublishMotor(now);
        if (now - last_status >= 100u) {
            SRNOS_IpcStatus ipc = {.uptime_ms = now};
            for (unsigned i = 0; i < 3; ++i) {
                SRNOS_CANRouterStats stats = {0};
                SRNOS_CANRouterGetStats(i + 1, &stats);
                ipc.can_rx[i] = stats.rx_pops;
                ipc.can_tx_done[i] = stats.tx_complete_events;
                if (SRNOS_FDCANIsReady(i + 1)) ipc.can_ready_mask |= 1u << i;
            }
            SRNOS_IpcPublishStatus(&ipc);
            last_status = now;
        }
        if (xTaskDelayUntil(&deadline, pdMS_TO_TICKS(1)) == pdFALSE) {
            /* A missed deadline must not run the control callback twice in one tick. */
            vTaskDelay(pdMS_TO_TICKS(1));
            deadline = xTaskGetTickCount();
        }
    }
}

static void report(void *arg)
{
    (void)arg;
    for (;;) {
        SRNOS_CANRouterStats stats[3] = {0};
        for (unsigned i = 0; i < 3; ++i) SRNOS_CANRouterGetStats(i + 1, &stats[i]);
        snprintf(trace, sizeof(trace),
                 "M33 tick=%lu us=%llu core=%lu Hz CAN ready=%u/%u/%u RX=%lu/%lu/%lu TXdone=%lu/%lu/%lu\n",
                 (unsigned long)SRNOS_TimeMs(), (unsigned long long)SRNOS_TimeUs(), (unsigned long)SystemCoreClock,
                 SRNOS_FDCANIsReady(1), SRNOS_FDCANIsReady(2), SRNOS_FDCANIsReady(3),
                 (unsigned long)stats[0].rx_pops, (unsigned long)stats[1].rx_pops,
                 (unsigned long)stats[2].rx_pops, (unsigned long)stats[0].tx_complete_events,
                 (unsigned long)stats[1].tx_complete_events, (unsigned long)stats[2].tx_complete_events);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void application(void *arg)
{
    (void)arg;
    configASSERT(SRNOS_FDCANInitAll() && SRNOS_FDCANStartAll());
    static const char *const rx[] = {"SRNOS_CAN1_RX", "SRNOS_CAN2_RX", "SRNOS_CAN3_RX"};
    static const char *const tx[] = {"SRNOS_CAN1_TX", "SRNOS_CAN2_TX", "SRNOS_CAN3_TX"};
    for (uintptr_t i = 0; i < 3; ++i) {
        osThreadAttr_t attr = {.name=rx[i], .stack_size=4096, .priority=osPriorityRealtime};
        configASSERT(SRNOS_KernelThreadNew(SRNOS_NativeCANRxRouterWorker, (void *)(i+1), &attr));
        attr.name = tx[i];
        configASSERT(SRNOS_KernelThreadNew(SRNOS_NativeCANTxRouterWorker, (void *)(i+1), &attr));
    }
    SRNOS_IpcInit();
    const osThreadAttr_t ipc = {.name="SRNOS_IPC", .stack_size=3072, .priority=osPriorityAboveNormal};
    configASSERT(SRNOS_KernelThreadNew(ipc_worker, NULL, &ipc));
    const osThreadAttr_t monitor = {.name="SRNOS_Trace", .stack_size=2048, .priority=osPriorityLow};
    configASSERT(SRNOS_KernelThreadNew(report, NULL, &monitor));
    status.native_can_ready = true;
    status.phase = SRNOS_SYSTEM_RUNNING;
    SRNOS_KernelThreadReleaseApplications();
    SRNOS_ApplicationMain();
}

int main(void)
{
    SystemCoreClockUpdate();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    last_cycle = DWT->CYCCNT;
    configASSERT((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) && SystemCoreClock >= 1000000U && SystemCoreClock % 1000000U == 0);
    SRNOS_BoardShutdownEnable();
    static SRNOS_SystemConfig draft;
    SRNOS_SystemConfigDefaults(&draft);
    SRNOS_ApplicationConfigure(&draft);
    configASSERT(SRNOS_SystemValidate(&draft) == SRNOS_SYSTEM_OK);
    config = draft;
    configASSERT(SRNOS_CANControllerPrepare(config.can));
    SRNOS_CAN_FreezeConfiguration();
    status.phase = SRNOS_SYSTEM_CONFIG_FROZEN;
    configASSERT(osKernelInitialize() == osOK);
    configASSERT(SRNOS_KernelThreadPrepare(false));
    configASSERT(SRNOS_CANFDCorePrepareResources());
    for (size_t i = 0; i < config.thread_count; ++i) {
        SRNOS_Thread handle;
        configASSERT(SRNOS_KernelThreadCreateConfigured(&config.threads[i], &handle) == SRNOS_OK);
    }
    const osThreadAttr_t attr = {.name="SRNOS_App", .stack_size=config.application_stack_bytes,
                                 .priority=(osPriority_t)config.application_priority};
    configASSERT(SRNOS_KernelThreadNew(application, NULL, &attr));
    status.phase = SRNOS_SYSTEM_THREADS_CREATED;
    osKernelStart();
    SRNOS_KernelFailure("scheduler returned", __FILE__, __LINE__);
}
