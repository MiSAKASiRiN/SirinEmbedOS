#ifndef SRNOS_STORAGE_INTERNAL_H
#define SRNOS_STORAGE_INTERNAL_H
#include <stddef.h>
#include "SirinEmbedOS/Services/Storage.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SRNOS_STORAGE_RECORD_IDENTITY = 1,
    SRNOS_STORAGE_RECORD_BMI = 2,
    SRNOS_STORAGE_RECORD_LSM = 3,
    SRNOS_STORAGE_RECORD_PARAMETERS = 4
} SRNOS_StorageRecord;
#define SRNOS_STORAGE_PARAMETERS_MAX_BYTES 8064u
#define SRNOS_STORAGE_CALIBRATION_MAX_BYTES 896u
/* Caller owns output until return; failure may leave a partial output but
 * outputs length/revision stay zero. Caller must ignore data on failure. */
SRNOS_StorageStatus SRNOS_StorageReadRecord(SRNOS_StorageRecord record,
    uint32_t owner_id, uint16_t schema_version, void *out, size_t capacity,
    size_t *length, uint32_t *revision, uint32_t timeout_ms);
/* visitor receives provisional blocks (<=64 bytes) outside critical sections.
 * It must not block, perform I/O, or retain the data pointer. False aborts with
 * CORRUPT. Only final OK validates CRC; discard all tentative state on error. */
typedef bool (*SRNOS_StorageVisitor)(void *context, size_t offset,
    const uint8_t *data, size_t length);
SRNOS_StorageStatus SRNOS_StorageVisitRecord(SRNOS_StorageRecord record,
    uint32_t owner_id, uint16_t schema_version, SRNOS_StorageVisitor visitor,
    void *context, size_t *length, uint32_t *revision, uint32_t timeout_ms);
/* data must be an immutable snapshot until return. No retention after return.
 * Identity writes additionally require the frozen factory write permission. */
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord record,
    uint32_t owner_id, uint16_t schema_version, const void *data, size_t length,
    uint32_t *revision, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
