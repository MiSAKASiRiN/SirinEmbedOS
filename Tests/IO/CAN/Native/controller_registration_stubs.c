/* Registration policy is covered by the controller/core suite. Native tests
 * link the real controller config and DLC helpers against an empty registry. */
#include "IO/CAN/can_controller_internal.h"
#include <assert.h>
bool fake_config_frozen;
bool SRNOS_CAN_ConfigurationFrozen(void) { return fake_config_frozen; }
bool SRNOS_CANRouterCoreConfigurationCompatible(uint8_t channel, const SRNOS_CANControllerConfig *c)
{ (void)channel; (void)c; return true; }
bool SRNOS_CANFDCoreHasRegistrations(uint8_t channel) { (void)channel; return false; }
void fake_freeze_classic(void)
{
    SRNOS_CANControllerConfig configs[4];
    for (unsigned i=0;i<4;++i) configs[i]=(SRNOS_CANControllerConfig){SRNOS_CAN_MODE_CLASSIC,1000000u,0u,false};
    assert(SRNOS_CANControllerPrepare(configs));
    fake_config_frozen=true;
}
