#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* SDK assembly only: no worker entry or configuration freezing in parent API. */
void SRNOS_CAN_FreezeConfiguration(void);
bool SRNOS_CAN_ConfigurationFrozen(void);
void SRNOS_CAN_PrepareResources(void);
void SRNOS_NativeCANRxRouterWorker(void *argument);
void SRNOS_NativeCANTxRouterWorker(void *argument);
void SRNOS_ExternalCANWorker(void *argument);
#ifdef __cplusplus
}
#endif
