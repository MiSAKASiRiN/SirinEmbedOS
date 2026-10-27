#ifndef SRNOS_NOR_BOARD_H
#define SRNOS_NOR_BOARD_H

#include "Devices/NOR/nor_internal.h"
#include <stdbool.h>

/* Private transport: owner is the task that successfully initializes it.
 * Every transaction has at most 256 bytes, optional 24-bit address, and one
 * direction. NOR opcode/protection policy remains in the device driver. */
SRNOS_NORStatus SRNOS_NORBoardInitialize(void);
bool SRNOS_NORBoardIsOwner(void);
SRNOS_NORStatus SRNOS_NORBoardTransfer(uint8_t instruction, bool addressed,
                                     uint32_t address, const void *tx,
                                     void *rx, size_t length);
uint32_t SRNOS_NORBoardTimeMs(void);
SRNOS_NORStatus SRNOS_NORBoardYield(void);

#endif
