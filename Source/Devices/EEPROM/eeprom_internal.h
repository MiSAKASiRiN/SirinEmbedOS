#ifndef SRNOS_EEPROM_INTERNAL_H
#define SRNOS_EEPROM_INTERNAL_H
#include "SirinEmbedOS/Devices/EEPROM.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Storage owns the management policy; set once before scheduler start. */
bool SRNOS_EEPROMStorageConfigure(bool managed);
/* Lease prevents raw operations interleaving between record phases. */
SRNOS_EEPROMStatus SRNOS_EEPROMStorageAcquire(void);
void SRNOS_EEPROMStorageRelease(void);
SRNOS_EEPROMStatus SRNOS_EEPROMStorageReadAsync(uint16_t address, void *data, size_t length, uint32_t timeout_ms);
SRNOS_EEPROMStatus SRNOS_EEPROMStorageWriteAsync(uint16_t address, const void *data, size_t length, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
