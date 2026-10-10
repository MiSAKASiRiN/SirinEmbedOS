#include "SirinEmbedOS/Kernel/System.h"
#include "thread_config.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include "SirinEmbedOS/Config.h"
#include <string.h>
#include <math.h>

static bool frozen;
static SRNOS_SystemConfig saved;

void SRNOS_SystemConfigDefaults(SRNOS_SystemConfig *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i) {
        c->can[i].mode = SRNOS_CAN_MODE_CLASSIC;
        c->can[i].nominal_bitrate = 1000000u;
    }
    c->ins.enabled=true;
    c->ins.board_to_robot_wxyz[0]=1;
    c->sensors.lsm_enabled=c->sensors.lis_enabled=c->sensors.spa_enabled=true;
    c->sensors.heater_enabled=true;
    c->sensors.heater_target_c=60;
    c->sensors.bmi_period_ms=1;
    c->sensors.lsm_period_ms=1;
    c->sensors.environment_period_ms=10;
    c->remote.primary = SRNOS_RC_SRC_DR16;
    c->remote.secondary = SRNOS_RC_SRC_VT03;
    c->remote.idle_ms = 50;
    c->remote.offline_ms = 300;
    c->remote.stick_deadzone = 10;
    c->logging_enabled = true;
    c->usb_enabled = true;
    c->host_link_enabled = true;
    c->host_link_storage_enabled = true;
    c->system_monitor_enabled = true;
    c->cycle_statistics_enabled = true;
    c->load_parameters = true;
    c->storage.enabled = true;
    c->storage.factory_identity_write = false;
    c->parameter_compatibility_version = 1;
    c->log_backends = SRNOS_LOG_BACKEND_USB_CDC;
    c->SRNOS_RS485ResponseTimeoutMs = 100;
    c->application_stack_bytes = SRNOS_APP_STACK_BYTES;
    c->application_priority = SRNOS_APP_PRIORITY;
}

static bool valid_stack(uint32_t bytes) { return bytes >= 1024 && bytes <= 32768 && (bytes % 8) == 0; }
static bool valid_priority(uint32_t p) { return p >= 1 && p < 40; }

uint32_t SRNOS_SystemHeapBudget(const SRNOS_SystemConfig *c)
{
    if (!c || c->thread_count > SRNOS_SYSTEM_MAX_THREADS || c->serial_count > SRNOS_SYSTEM_MAX_SERIAL) return UINT32_MAX;
    /* Base: seven CAN workers, IMU, two bus dispatch workers, indicators,
     * idle/timer, TCB/TLS and synchronization overhead, plus startup reserve.
     * Optional reserves deliberately exceed current queue/control-block sizes.
     * HostLink active/pending state and Serial RX/TX semaphore storage are
     * fixed D1 objects, checked in the link map, not extra heap allocations.
     * CAN FD raw contexts are also fixed D1 objects, with no heap reservation. */
    uint64_t bytes = 60u * 1024u + c->application_stack_bytes + 512u;
    bytes += 3584u; /* INS/control worker including TCB/TLS. */
    bytes += 2560u; /* SPI/I2C request completion semaphores and startup reserve. */
    if (c->sensors.lsm_enabled) bytes += 2560u;
    if (c->sensors.lis_enabled || c->sensors.spa_enabled) bytes += 2560u;
    if (c->logging_enabled) bytes += 16u * 1024u;
    if (c->host_link_enabled) bytes += 9u * 1024u; /* HostComm 3 KiB + HostCmd 2 KiB, queues/TCB/TLS. */
    if (c->system_monitor_enabled) bytes += 5u * 1024u;
    if (c->serial_count) bytes += 3u * 1024u + c->serial_count * 2560u;
    if (c->remote.dr16_port || c->remote.vt03_port) bytes += 4u * 1024u;
    if (c->referee_port) bytes += 4u * 1024u;
    for (size_t i=0; i<c->serial_count; ++i) if (c->serial[i].port == SRNOS_SERIAL_UART7) bytes += 8u * 1024u;
    bytes += 8192u; /* Fixed lifecycle registry, allocated once before scheduling. */
    if(c->cycle_statistics_enabled)bytes += 8192u; /* Fixed cycle accumulators. */
    bytes += SRNOS_APP_OBJECT_POOL_BYTES; /* Includes all application thread objects. */
    return bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)bytes;
}

SRNOS_SystemResult SRNOS_SystemValidate(const SRNOS_SystemConfig *c)
{
    if (!c || c->serial_count > SRNOS_SYSTEM_MAX_SERIAL || c->thread_count > SRNOS_SYSTEM_MAX_THREADS ||
        !isfinite(c->sensors.heater_target_c) || c->sensors.heater_target_c < 20 || c->sensors.heater_target_c > 75 ||
        (c->ins.enabled && (c->sensors.bmi_period_ms != 1 || (c->sensors.lsm_enabled && c->sensors.lsm_period_ms != 1))) ||
        (c->sensors.heater_enabled && c->sensors.bmi_period_ms > 5 && (!c->sensors.lsm_enabled || c->sensors.lsm_period_ms > 5)) ||
        c->sensors.bmi_period_ms < 1 || c->sensors.bmi_period_ms > 100 ||
        c->sensors.lsm_period_ms < 1 || c->sensors.lsm_period_ms > 100 ||
        c->sensors.environment_period_ms < 10 || c->sensors.environment_period_ms > 100 ||
        !valid_stack(c->application_stack_bytes) || !valid_priority(c->application_priority) ||
        !c->SRNOS_RS485ResponseTimeoutMs || c->SRNOS_RS485ResponseTimeoutMs > 60000 ||
        !c->parameter_compatibility_version ||
        (c->host_link_enabled && !c->usb_enabled) ||
        (c->logging_enabled && (c->log_backends & ~7u)) ||
        (c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_USB_CDC) && !c->usb_enabled) ||
        !c->remote.idle_ms || c->remote.idle_ms >= c->remote.offline_ms ||
        c->remote.primary < SRNOS_RC_SRC_DR16 || c->remote.primary > SRNOS_RC_SRC_VT03 ||
        c->remote.secondary < SRNOS_RC_SRC_DR16 || c->remote.secondary > SRNOS_RC_SRC_VT03 ||
        c->remote.primary == c->remote.secondary) return SRNOS_SYSTEM_INVALID;
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i)
        if (!SRNOS_CANControllerConfigValid((uint8_t)(i + 1u), &c->can[i]))
            return SRNOS_SYSTEM_INVALID;
    double qnorm=0;
    for(unsigned i=0;i<4;i++) {
        if(!isfinite(c->ins.board_to_robot_wxyz[i])) return SRNOS_SYSTEM_INVALID;
        qnorm+=(double)c->ins.board_to_robot_wxyz[i]*c->ins.board_to_robot_wxyz[i];
    }
    if(qnorm<1e-12 || !isfinite(qnorm)) return SRNOS_SYSTEM_INVALID;
    SRNOS_SerialStatus ss = SRNOS_SerialValidate(c->serial, c->serial_count);
    if (ss != SRNOS_SERIAL_OK) return ss == SRNOS_SERIAL_CONFLICT ? SRNOS_SYSTEM_CONFLICT : SRNOS_SYSTEM_INVALID;
    const SRNOS_SerialPortId roles[] = { c->remote.dr16_port, c->remote.vt03_port, c->referee_port,
        c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_UART) ? c->log_port : SRNOS_SERIAL_NONE };
    if (c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_UART) && !c->log_port) return SRNOS_SYSTEM_INVALID;
    for (size_t i = 0; i < sizeof(roles)/sizeof(roles[0]); ++i) {
        if (roles[i] == SRNOS_SERIAL_NONE) continue;
        const SRNOS_SerialConfig *port = NULL;
        for (size_t j = 0; j < c->serial_count; ++j) if (c->serial[j].port == roles[i]) port = &c->serial[j];
        if (!port || (i < 3 ? !port->rx_enabled : !port->tx_enabled)) return SRNOS_SYSTEM_INVALID;
        for (size_t j = 0; j < i; ++j) if (roles[i] == roles[j]) return SRNOS_SYSTEM_CONFLICT;
        if (i == 0 && (port->baud_rate != 100000 || port->data_bits != 8 || port->stop_bits != 2 || port->parity != SRNOS_SERIAL_PARITY_EVEN)) return SRNOS_SYSTEM_INVALID;
        if (i == 1 && (port->baud_rate != 921600 || port->data_bits != 8 || port->stop_bits != 1 || port->parity != SRNOS_SERIAL_PARITY_NONE)) return SRNOS_SYSTEM_INVALID;
        if (i == 2 && (port->baud_rate != 115200 || port->data_bits != 8 || port->stop_bits != 1 || port->parity != SRNOS_SERIAL_PARITY_NONE || !port->tx_enabled)) return SRNOS_SYSTEM_INVALID;
    }
    if(c->thread_count>SRNOS_APP_THREAD_LIMIT)return SRNOS_SYSTEM_RESOURCE;
    uint64_t app_storage=0;
    for (size_t i = 0; i < c->thread_count; ++i) {
        const SRNOS_ThreadConfig *t = &c->threads[i];
        app_storage+=t->stack_bytes+1024u; /* Pool rounding, TCB, completion event and TLS margin. */
        if (!SRNOS_KernelThreadConfigValid(t)) return SRNOS_SYSTEM_INVALID;
        for(size_t j=0;j<i;j++) if(!strcmp(t->name,c->threads[j].name)) return SRNOS_SYSTEM_CONFLICT;
    }
    if(app_storage>SRNOS_APP_OBJECT_POOL_BYTES)return SRNOS_SYSTEM_RESOURCE;
    if (SRNOS_SystemHeapBudget(c) > SRNOS_HEAP_DTCM_BYTES + SRNOS_HEAP_D1_BYTES) return SRNOS_SYSTEM_RESOURCE;
    return SRNOS_SYSTEM_OK;
}

/* Internal single startup writer; applications only see the const snapshot. */
SRNOS_SystemResult SRNOS_SystemFreeze(const SRNOS_SystemConfig *c)
{
    if (frozen) return SRNOS_SYSTEM_FROZEN;
    SRNOS_SystemResult result = SRNOS_SystemValidate(c);
    if (result != SRNOS_SYSTEM_OK) return result;
    saved = *c;
    double norm=0;for(unsigned i=0;i<4;i++) norm+=(double)saved.ins.board_to_robot_wxyz[i]*saved.ins.board_to_robot_wxyz[i];
    norm=sqrt(norm);for(unsigned i=0;i<4;i++) saved.ins.board_to_robot_wxyz[i]=(float)(saved.ins.board_to_robot_wxyz[i]/norm);
    frozen = true;
    return SRNOS_SYSTEM_OK;
}
bool SRNOS_SystemConfigurationFrozen(void) { return frozen; }
const SRNOS_SystemConfig *SRNOS_SystemGetConfig(void) { return frozen ? &saved : NULL; }
