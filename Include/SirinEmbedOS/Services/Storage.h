#ifndef SRNOS_SERVICES_STORAGE_H
#define SRNOS_SERVICES_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_STORAGE_READ_TIMEOUT_MS 2000u
#define SRNOS_STORAGE_WRITE_TIMEOUT_MS 5000u
#define SRNOS_STORAGE_SERIAL_LENGTH 20u
typedef enum {
    SRNOS_STORAGE_OK = 0, SRNOS_STORAGE_BUSY, SRNOS_STORAGE_DISABLED,
    SRNOS_STORAGE_INVALID_ARGUMENT, SRNOS_STORAGE_CONTEXT_ERROR,
    SRNOS_STORAGE_NOT_FOUND, SRNOS_STORAGE_CORRUPT, SRNOS_STORAGE_INCOMPATIBLE,
    SRNOS_STORAGE_IO_ERROR, SRNOS_STORAGE_TIMEOUT, SRNOS_STORAGE_VERIFY_ERROR,
    SRNOS_STORAGE_READ_ONLY
} SRNOS_StorageStatus;
typedef struct {
    bool enabled;                 /* Default true; false permits raw maintenance. */
    bool factory_identity_write; /* Default false; never host-writable. */
} SRNOS_StorageConfig;
typedef struct {
    char serial[SRNOS_STORAGE_SERIAL_LENGTH + 1u];
    uint32_t revision;
    SRNOS_StorageStatus status;
    bool valid;
} SRNOS_StorageIdentity;
typedef struct {
    uint32_t read_count, write_count, skipped_writes, error_count, busy_count;
    SRNOS_StorageStatus last_status;
    bool enabled, busy, identity_write_enabled;
} SRNOS_StorageSnapshot;
/* Configuration only, before kernel start. Prepare freezes a copied config;
 * repeated Prepare is rejected. NULL config selects the defaults. */
void SRNOS_StorageDefaultConfig(SRNOS_StorageConfig *config);
bool SRNOS_StoragePrepare(const SRNOS_StorageConfig *config);
/* Task context with IRQs enabled. timeout=0 selects the default; other finite
 * timeouts are 1..INT32_MAX ms over the entire operation including readback.
 * These calls block cooperatively; competing operations return BUSY immediately.
 * A failed single-copy write can destroy the previous record. No implicit save.
 */
SRNOS_StorageStatus SRNOS_StorageLoadIdentity(uint32_t timeout_ms);
SRNOS_StorageStatus SRNOS_StorageWriteIdentity(const char serial[SRNOS_STORAGE_SERIAL_LENGTH], uint32_t timeout_ms);
/* Task-context copies; no EEPROM I/O. Invalid identity has an empty serial. */
bool SRNOS_StorageGetIdentity(SRNOS_StorageIdentity *out);
bool SRNOS_StorageGetSnapshot(SRNOS_StorageSnapshot *out);
#ifdef __cplusplus
}
#endif
#endif
