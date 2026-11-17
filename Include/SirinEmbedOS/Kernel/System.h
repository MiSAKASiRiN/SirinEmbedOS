#ifndef SRNOS_KERNEL_SYSTEM_H
#define SRNOS_KERNEL_SYSTEM_H
#include <stdbool.h>
#include <stddef.h>
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
#include "SirinEmbedOS/Services/Network.h"
#include <stdint.h>
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Services/Storage.h"
#include "SirinEmbedOS/Services/Blackbox.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/RemoteControl/Startup.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_SYSTEM_MAX_SERIAL 5u
#define SRNOS_SYSTEM_MAX_THREADS 8u

typedef struct {
    /* Indices 0..3 map to CAN1..4. Only startup configuration; frozen after
     * ApplicationConfigure/bind_channels. Default: Classic CAN at 1 Mbit/s. */
    SRNOS_CANControllerConfig can[SRNOS_CAN_CONTROLLER_COUNT];
    SRNOS_EthernetConfig ethernet;
    SRNOS_NetworkConfig network;
    SRNOS_SensorConfig sensors;
    SRNOS_INSConfig ins;
    SRNOS_SerialConfig serial[SRNOS_SYSTEM_MAX_SERIAL];
    size_t serial_count;
    SRNOS_RemoteControlConfig remote;
    SRNOS_MessageTransportConfig messages;
    SRNOS_SerialPortId referee_port;
    SRNOS_SerialPortId log_port;
    uint32_t log_backends;
    bool logging_enabled;
    bool usb_enabled;
    bool sirin_link_enabled;
    bool sirin_link_storage_enabled;
    bool bringup_indicators;
    bool system_monitor_enabled;
    bool cycle_statistics_enabled;
    bool load_parameters;
    SRNOS_StorageConfig storage;
    SRNOS_BlackboxConfig blackbox;
    /* Stable application-owned namespace. Nonzero when persistent parameters
     * exist; change compatibility version only for incompatible semantics. */
    uint32_t parameter_application_id;
    uint16_t parameter_compatibility_version;
    uint32_t SRNOS_RS485ResponseTimeoutMs;
    uint32_t application_stack_bytes;
    uint32_t application_priority;
    /* Startup names/arguments must outlive this configuration callback.
     * Runtime ThreadCreate instead copies the supplied description/name. */
    SRNOS_ThreadConfig threads[SRNOS_SYSTEM_MAX_THREADS];
    size_t thread_count;
    SRNOS_SirinLinkProjectSetupFn sirin_link_setup;
    void *sirin_link_user;
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
    SRNOS_StorageStatus identity_load_status;
} SRNOS_SystemStatus;

void SRNOS_SystemConfigDefaults(SRNOS_SystemConfig *config);
/* Conservative kernel heap reservation (bytes), including fixed workers,
 * optional service queues, fixed application object pool, lifecycle/statistics
 * storage and allocation overhead. Includes the CURRENT Input/Message registry,
 * so estimates made before binding/registration are incomplete. Startup checks
 * again after registration and before freeze. Static/DMA sections remain a
 * separate link-map constraint; a budget is not measured free heap. Runtime
 * pool exhaustion is recoverable. */
uint32_t SRNOS_SystemHeapBudget(const SRNOS_SystemConfig *config);
SRNOS_SystemResult SRNOS_SystemValidate(const SRNOS_SystemConfig *config);
bool SRNOS_SystemConfigurationFrozen(void);
const SRNOS_SystemConfig *SRNOS_SystemGetConfig(void); /* NULL before freeze. */
void SRNOS_SystemGetStatus(SRNOS_SystemStatus *status);
/* Nonzero random 64-bit identifier, generated once before ApplicationConfigure.
 * Stable until reset, independent of NTP and uptime. Not a device ID or an
 * authentication secret; random collisions are possible but negligible.
 * Returns zero before kernel startup. Read-only, no allocation or hardware I/O. */
uint64_t SRNOS_SystemBootId(void);
/* Optional weak hook, called once after HAL and before kernel initialization. */
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config);

#ifdef __cplusplus
}
#endif
#endif
