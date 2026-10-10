#ifndef SRNOS_CAN_CONTROLLER_INTERNAL_H
#define SRNOS_CAN_CONTROLLER_INTERNAL_H
#include "SirinEmbedOS/IO/CAN/Controller.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Validate all registrations and modes before publishing any new config.
 * No hardware I/O or runtime allocation. Called once before final freeze. */
bool SRNOS_CANControllerPrepare(const SRNOS_CANControllerConfig config[SRNOS_CAN_CONTROLLER_COUNT]);
const SRNOS_CANControllerConfig *SRNOS_CANControllerConfigOf(uint8_t channel);
bool SRNOS_CANControllerIsClassic(uint8_t channel);
bool SRNOS_CANControllerIsFD(uint8_t channel);
bool SRNOS_CANControllerAdapterAllowed(uint8_t channel);
/* Exact DLC mapping; invalid arguments return 0xff. */
uint8_t SRNOS_CANFDLengthToDlc(uint8_t length);
uint8_t SRNOS_CANFDDlcToLength(uint8_t dlc);
#ifdef __cplusplus
}
#endif
#endif
