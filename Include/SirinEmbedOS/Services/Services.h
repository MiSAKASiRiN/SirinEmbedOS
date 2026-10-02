#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/HostLink/HostLink.h"
#include "SirinEmbedOS/Services/Parameters/Parameters.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Called exactly once during startup. Setup registers business commands and
 * telemetry only; it must not expose system routing/configuration for writing. */
typedef SRNOS_HostLinkProjectSetupFn SRNOS_HostLinkSetupFn;
bool SRNOS_LoggingPrepare(uint32_t backend_mask, SRNOS_SerialChannel *uart_channel);
bool SRNOS_LoggingStart(void);
bool SRNOS_HostLinkPrepare(bool enable_usb, SRNOS_HostLinkSetupFn setup, void *user);
bool SRNOS_HostLinkPrepareEx(bool enable_usb, bool storage, SRNOS_HostLinkSetupFn setup, void *user);
bool SRNOS_HostLinkStart(void);
/* Called from the SDK startup thread after HAL PCD initialization. */
bool SRNOS_USBStart(void);
#ifdef __cplusplus
}
#endif
