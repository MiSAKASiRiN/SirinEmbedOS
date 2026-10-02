#pragma once
/* Explicit board-test scratch allocation, outside the application object pool.
 * This test-only adapter is not an SDK/application allocation contract. */
#include <stddef.h>
void *SRNOS_EEPROMTestAllocate(size_t bytes);

#include "SirinEmbedOS/Devices/EEPROM.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t core_hz, count, cycles[128];
    SRNOS_EEPROMStatus status;
} SRNOS_EEPROMTestSCL;
/* Board-test instrumentation only; may briefly occupy the CPU while observing SCL. */
bool SRNOS_EEPROMTestObserveSCL(uint8_t *workspace, volatile SRNOS_EEPROMTestSCL *snapshot);
void SRNOS_EEPROMTestPublishBuffer(const void *data, size_t bytes);
void SRNOS_EEPROMTestAcquireBuffer(void *data, size_t bytes);
