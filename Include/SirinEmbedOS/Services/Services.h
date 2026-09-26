#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/HostLink/tskptt_host_comm.h"
#include "SirinEmbedOS/Services/Parameters/lib_params.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Called exactly once during startup. Setup registers business commands and
 * telemetry only; it must not expose system routing/configuration for writing. */
typedef tskptt_host_comm_project_setup_fn SRNOS_HostLinkSetupFn;
bool SRNOS_LoggingPrepare(uint32_t backend_mask, SRNOS_SerialChannel *uart_channel);
bool SRNOS_LoggingStart(void);
bool SRNOS_HostLinkPrepare(bool enable_usb, SRNOS_HostLinkSetupFn setup, void *user);
bool SRNOS_HostLinkStart(void);
/* Called from the SDK startup thread after HAL PCD initialization. */
bool SRNOS_USBStart(void);
#ifdef __cplusplus
}
#endif
