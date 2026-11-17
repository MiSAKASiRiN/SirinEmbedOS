#include "SirinEmbedOS/Kernel/System.h"
#include "thread_config.h"
#include "kernel_internal.h"
#include "IO/Serial/serial_internal.h"
#include "IO/RS485/rs485_internal.h"
#include "IO/Ethernet/ethernet_internal.h"
#include "IO/CAN/FD/can_fd_internal.h"
#include "Services/Input/input_internal.h"
#include "Services/Message/message_internal.h"
#include "Services/Blackbox/blackbox_internal.h"
#include "Services/RemoteControl/remote_internal.h"
#include "Services/Referee/referee_internal.h"
#include "Services/Logging/logging_internal.h"
#include "Services/VideoLink/video_link_internal.h"
#include "Services/Network/network_internal.h"
#include "Services/SystemMonitor/monitor_internal.h"
#include "Services/SirinLink/host_comm_internal.h"
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
    c->network.address[0]=192; c->network.address[1]=168; c->network.address[2]=1; c->network.address[3]=50;
    c->network.netmask[0]=c->network.netmask[1]=c->network.netmask[2]=255;
    c->ins.enabled=true;
    c->ins.board_to_robot_wxyz[0]=1;
    c->sensors.lsm_enabled=c->sensors.lis_enabled=c->sensors.spa_enabled=true;
    c->sensors.heater_enabled=true;
    c->sensors.heater_target_c=60;
    c->sensors.bmi_period_ms=1;
    c->sensors.lsm_period_ms=1;
    c->sensors.environment_period_ms=10;
    c->remote.offline_ms = 300;
    c->logging_enabled = true;
    c->usb_enabled = true;
    c->sirin_link_enabled = true;
    c->sirin_link_storage_enabled = true;
    c->system_monitor_enabled = true;
    c->cycle_statistics_enabled = true;
    c->load_parameters = true;
    c->storage.enabled = true;
    c->storage.factory_identity_write = false;
    SRNOS_BlackboxDefaultConfig(&c->blackbox);
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
    uint64_t bytes = SRNOS_KernelStartupHeapBudget(c);
    bytes += SRNOS_LoggingHeapBudget(c->logging_enabled);
    bytes += SRNOS_SirinLinkHeapBudget(c->sirin_link_enabled);
    bytes += SRNOS_SystemMonitorHeapBudget(c->system_monitor_enabled);
    bytes += SRNOS_NetworkHeapBudget(&c->network);
    bytes += SRNOS_EthernetHeapBudget(c->ethernet.enabled);
    bytes += SRNOS_SerialHeapBudget(c->serial_count);
    bytes += SRNOS_RemoteControlHeapBudget(c->remote.dr16_port || c->remote.vt03_port ||
        c->remote.wbus_port || c->remote.vt02_port || c->messages.uart_port);
    bytes += SRNOS_RefereeHeapBudget(c->referee_port != SRNOS_SERIAL_NONE);
    bool rs485_configured = false;
    for (size_t i=0; i<c->serial_count; ++i)
        if (c->serial[i].port == SRNOS_SERIAL_UART7) rs485_configured = true;
    bytes += SRNOS_RS485HeapBudget(rs485_configured);
    bytes += SRNOS_InputHeapBudget()+SRNOS_MessageHeapBudget()+SRNOS_MessageTransportHeapBudget();
    bytes += SRNOS_BlackboxHeapBudget(&c->blackbox);
    bytes += SRNOS_CANFDCoreHeapBudget(c->can);
    bytes += SRNOS_KernelThreadHeapBudget(c->cycle_statistics_enabled);
    return bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)bytes;
}

static const SRNOS_SerialConfig *SRNOS_SystemSerialConfig(const SRNOS_SystemConfig *config,
    SRNOS_SerialPortId port)
{
    for (size_t i = 0; i < config->serial_count; ++i)
        if (config->serial[i].port == port) return &config->serial[i];
    return NULL;
}

static bool SRNOS_SystemSerialRoleValid(const SRNOS_SystemConfig *config, SRNOS_SerialPortId port,
    bool (*validate)(const SRNOS_SerialConfig *))
{
    return port == SRNOS_SERIAL_NONE || validate(SRNOS_SystemSerialConfig(config, port));
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
        (c->sirin_link_enabled && !c->usb_enabled) ||
        (c->logging_enabled && (c->log_backends & ~7u)) ||
        (c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_USB_CDC) && !c->usb_enabled) ||
        !c->remote.offline_ms || c->remote.offline_ms > 60000 ||
        (c->remote.sirinrmt_transports & ~15u) ||
        !SRNOS_BlackboxConfigValid(&c->blackbox)) return SRNOS_SYSTEM_INVALID;
    if(c->remote.video_link_enabled&&!c->remote.vt03_port)return SRNOS_SYSTEM_INVALID;
    if((c->messages.host_enabled&&!c->sirin_link_enabled)||
       (c->messages.udp_port&&!c->network.enabled)||
       ((c->remote.sirinrmt_transports&SRNOS_MESSAGE_HOST)&&!c->messages.host_enabled)||
       ((c->remote.sirinrmt_transports&SRNOS_MESSAGE_UART)&&!c->messages.uart_port)||
       ((c->remote.sirinrmt_transports&SRNOS_MESSAGE_UDP)&&!c->messages.udp_port)||
       ((c->remote.sirinrmt_transports&SRNOS_MESSAGE_VIDEO)&&!c->remote.video_link_enabled))return SRNOS_SYSTEM_INVALID;
    for(unsigned i=0;i<c->network.udp_port_count && i<SRNOS_UDP_PORT_LIMIT;i++)
        if(c->messages.udp_port&&c->messages.udp_port==c->network.udp_ports[i])return SRNOS_SYSTEM_CONFLICT;
    for(unsigned i=0;i<16;i++)
        if(c->remote.wbus_channels[i].kind>SRNOS_INPUT_CHANNEL_POSITION)return SRNOS_SYSTEM_INVALID;
    for (unsigned i = 0; i < SRNOS_CAN_CONTROLLER_COUNT; ++i)
        if (!SRNOS_CANControllerConfigValid((uint8_t)(i + 1u), &c->can[i]))
            return SRNOS_SYSTEM_INVALID;
    if (!SRNOS_EthernetConfigValid(&c->ethernet) || !SRNOS_NetworkConfigValid(&c->network) ||
        (c->network.enabled && !c->ethernet.enabled)) return SRNOS_SYSTEM_INVALID;
    double qnorm=0;
    for(unsigned i=0;i<4;i++) {
        if(!isfinite(c->ins.board_to_robot_wxyz[i])) return SRNOS_SYSTEM_INVALID;
        qnorm+=(double)c->ins.board_to_robot_wxyz[i]*c->ins.board_to_robot_wxyz[i];
    }
    if(qnorm<1e-12 || !isfinite(qnorm)) return SRNOS_SYSTEM_INVALID;
    SRNOS_SerialStatus ss = SRNOS_SerialValidate(c->serial, c->serial_count);
    if (ss != SRNOS_SERIAL_OK) return ss == SRNOS_SERIAL_CONFLICT ? SRNOS_SYSTEM_CONFLICT : SRNOS_SYSTEM_INVALID;
    const SRNOS_SerialPortId roles[] = { c->remote.dr16_port, c->remote.vt03_port, c->remote.wbus_port, c->messages.uart_port, c->referee_port,
        c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_UART) ? c->log_port : SRNOS_SERIAL_NONE,
        c->remote.vt02_port };
    if (c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_UART) && !c->log_port) return SRNOS_SYSTEM_INVALID;
    for (size_t i = 0; i < sizeof(roles)/sizeof(roles[0]); ++i) {
        if (roles[i] == SRNOS_SERIAL_NONE) continue;
        for (size_t j = 0; j < i; ++j) if (roles[i] == roles[j]) return SRNOS_SYSTEM_CONFLICT;
    }
    if (!SRNOS_SystemSerialRoleValid(c, c->remote.dr16_port, SRNOS_DR16SerialConfigValid) ||
        !SRNOS_SystemSerialRoleValid(c, c->remote.vt03_port, SRNOS_VT03SerialConfigValid) ||
        !SRNOS_SystemSerialRoleValid(c, c->remote.wbus_port, SRNOS_WBusSerialConfigValid) ||
        !SRNOS_SystemSerialRoleValid(c, c->remote.vt02_port, SRNOS_VT02SerialConfigValid) ||
        !SRNOS_SystemSerialRoleValid(c, c->messages.uart_port, SRNOS_MessageSerialConfigValid) ||
        !SRNOS_SystemSerialRoleValid(c, c->referee_port, SRNOS_RefereeSerialConfigValid)) return SRNOS_SYSTEM_INVALID;
    if (c->logging_enabled && (c->log_backends & SRNOS_LOG_BACKEND_UART) &&
        !SRNOS_SystemSerialRoleValid(c, c->log_port, SRNOS_LoggingSerialConfigValid)) return SRNOS_SYSTEM_INVALID;
    if (c->remote.video_link_enabled &&
        !SRNOS_VideoLinkSerialConfigValid(SRNOS_SystemSerialConfig(c, c->remote.vt03_port))) return SRNOS_SYSTEM_INVALID;
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
