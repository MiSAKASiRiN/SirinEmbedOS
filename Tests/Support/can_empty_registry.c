/* Isolate unrelated host suites from CAN registration. These suites still use
 * the real controller validator and DLC helpers; CAN/FD covers real conflicts. */
#include "SirinEmbedOS/IO/CAN/Controller.h"
#include <stdbool.h>
#ifndef SRNOS_TEST_CAN_REAL_CLASSIC_CORE
bool SRNOS_CAN_ConfigurationFrozen(void) { return false; }
bool SRNOS_CANRouterCoreConfigurationCompatible(uint8_t channel,
                                                const SRNOS_CANControllerConfig *config)
{ (void)channel; (void)config; return true; }
#endif
bool SRNOS_CANFDCoreHasRegistrations(uint8_t channel) { (void)channel; return false; }
void SRNOS_CANFDCoreFreeze(void) {}
