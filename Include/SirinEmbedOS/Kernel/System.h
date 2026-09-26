#ifndef SRNOS_KERNEL_SYSTEM_H
#define SRNOS_KERNEL_SYSTEM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/RemoteControl/Startup.h"
#include "SirinEmbedOS/Services/HostLink/tskptt_host_comm.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_SYSTEM_MAX_SERIAL 5u
#define SRNOS_SYSTEM_MAX_THREADS 8u
typedef struct {
    const char *name; /* Static storage; maximum 15 visible characters. */
    void (*entry)(void *);
    void *argument;
    uint32_t stack_bytes; /* >=1024, multiple of 8. */
    uint32_t priority;    /* CMSIS priority 1..39, below fixed bus workers. */
} SRNOS_ThreadConfig;
typedef struct {
    SRNOS_SerialConfig serial[SRNOS_SYSTEM_MAX_SERIAL];
    size_t serial_count;
    SRNOS_RemoteControlConfig remote;
    SRNOS_SerialPortId referee_port;
    SRNOS_SerialPortId log_port;
    uint32_t log_backends;
    bool logging_enabled;
    bool usb_enabled;
    bool host_link_enabled;
    bool diagnostics_enabled;
    bool load_parameters;
    uint32_t rs485_response_timeout_ms;
    uint32_t application_stack_bytes;
    uint32_t application_priority;
    SRNOS_ThreadConfig threads[SRNOS_SYSTEM_MAX_THREADS];
    size_t thread_count;
    tskptt_host_comm_project_setup_fn host_link_setup;
    void *host_link_user;
    /* Optional second configuration phase: Serial channels now exist, built-in
     * services already hold their ports. Declare H12H/RS485 or raw channel
     * owners here. No blocking I/O, threads or allocations through the kernel.
     * Return false to stop startup; all descriptors must have static lifetime. */
    bool (*bind_channels)(void *user);
    void *bind_channels_user;
} SRNOS_SystemConfig;

typedef enum {
    SRNOS_SYSTEM_OK = 0,
    SRNOS_SYSTEM_INVALID = -1,
    SRNOS_SYSTEM_CONFLICT = -2,
    SRNOS_SYSTEM_FROZEN = -3,
    SRNOS_SYSTEM_RESOURCE = -4
} SRNOS_SystemResult;
typedef enum {
    SRNOS_SYSTEM_CONFIGURING,
    SRNOS_SYSTEM_CONFIG_FROZEN,
    SRNOS_SYSTEM_THREADS_CREATED,
    SRNOS_SYSTEM_RUNNING
} SRNOS_SystemPhase;
typedef struct {
    SRNOS_SystemPhase phase;
    bool native_can_ready;
    bool usb_ready;
    int parameter_load_status;
} SRNOS_SystemStatus;

void SRNOS_SystemConfigDefaults(SRNOS_SystemConfig *config);
/* Conservative kernel heap reservation (bytes), including fixed workers,
 * optional service queues, application stacks and allocation overhead. */
uint32_t SRNOS_SystemHeapBudget(const SRNOS_SystemConfig *config);
SRNOS_SystemResult SRNOS_SystemValidate(const SRNOS_SystemConfig *config);
bool SRNOS_SystemConfigurationFrozen(void);
const SRNOS_SystemConfig *SRNOS_SystemGetConfig(void); /* NULL before freeze. */
void SRNOS_SystemGetStatus(SRNOS_SystemStatus *status);
/* Optional weak hook, called once after HAL and before kernel initialization. */
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config);

#ifdef __cplusplus
}
#endif
#endif
