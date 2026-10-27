#ifndef SRNOS_PARAMETERS_H
#define SRNOS_PARAMETERS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef SRNOS_PARAMETERS_MAX_COUNT
#define SRNOS_PARAMETERS_MAX_COUNT 32U
#endif
#ifndef SRNOS_PARAMETERS_MAX_NAME_LEN
#define SRNOS_PARAMETERS_MAX_NAME_LEN 48U
#endif
#define SRNOS_PARAMETERS_VALUE_STRING_MAX_LEN 32U
/* This registry is exclusively for bounded, mutable BUSINESS values. Never
 * register hardware routes, CAN/IMU configuration, service or memory choices.
 * Registration copies all data/name, and freezes before scheduling. */
typedef enum {
    SRNOS_PARAMETERS_STATUS_OK=0, SRNOS_PARAMETERS_STATUS_ERROR,
    SRNOS_PARAMETERS_STATUS_INVALID_PARAM, SRNOS_PARAMETERS_STATUS_NOT_FOUND,
    SRNOS_PARAMETERS_STATUS_DUPLICATE, SRNOS_PARAMETERS_STATUS_NO_SPACE,
    SRNOS_PARAMETERS_STATUS_TYPE_MISMATCH, SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE,
    SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL, SRNOS_PARAMETERS_STATUS_PARSE_ERROR,
    SRNOS_PARAMETERS_STATUS_EEPROM_ERROR, SRNOS_PARAMETERS_STATUS_EEPROM_BUSY,
    SRNOS_PARAMETERS_STATUS_BAD_DATA, SRNOS_PARAMETERS_STATUS_LAYOUT_MISMATCH,
    SRNOS_PARAMETERS_STATUS_CRC_ERROR, SRNOS_PARAMETERS_STATUS_NOT_INITIALIZED,
    SRNOS_PARAMETERS_STATUS_LOCKED, SRNOS_PARAMETERS_STATUS_READ_ONLY,
    SRNOS_PARAMETERS_STATUS_CONFLICT, SRNOS_PARAMETERS_STATUS_TIMEOUT,
    SRNOS_PARAMETERS_STATUS_DISABLED, SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR
} SRNOS_ParametersStatus;
typedef enum {
    SRNOS_PARAMETERS_TYPE_INVALID=0, SRNOS_PARAMETERS_TYPE_FLOAT,
    SRNOS_PARAMETERS_TYPE_INT32, SRNOS_PARAMETERS_TYPE_UINT32,
    SRNOS_PARAMETERS_TYPE_BOOL
} SRNOS_ParametersType;
typedef union { float f32; int32_t i32; uint32_t u32; bool boolean; } SRNOS_ParameterValue;
typedef struct {
    uint32_t stable_id; /* Nonzero ID chosen by the parent; never reuse for another meaning. */
    const char *name;
    SRNOS_ParametersType type;
    SRNOS_ParameterValue default_value, minimum, maximum;
} SRNOS_PersistentParameterDefinition;
typedef struct {
    uint32_t index;
    const char *name; /* Immutable SDK-owned name; never a mutable value pointer. */
    uint32_t name_hash, stable_id;
    SRNOS_ParametersType type;
    uint16_t size;
    uint8_t registered;
} SRNOS_ParametersEntryInfo;
typedef struct {
    uint32_t application_id;
    uint16_t compatibility_version;
    uint32_t generation, saved_generation, storage_revision;
    bool dirty, saved_valid, busy;
    SRNOS_ParametersStatus last_load, last_save;
    uint16_t loaded, defaulted, ignored;
} SRNOS_ParametersState;
typedef enum {
    SRNOS_PARAMETERS_BINARY_VALUE_CURRENT=0, SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT,
    SRNOS_PARAMETERS_BINARY_VALUE_MIN, SRNOS_PARAMETERS_BINARY_VALUE_MAX
} SRNOS_ParametersBinaryValue;
typedef struct { uint16_t index; const uint8_t *data; uint16_t len; } SRNOS_ParametersBinaryWrite;
typedef void (*SRNOS_ParametersDumpCallback)(const SRNOS_ParametersEntryInfo *,const char *,const char *,const char *,const char *,void *);
SRNOS_ParametersStatus SRNOS_ParametersRegisterPersistent(const SRNOS_PersistentParameterDefinition *,uint16_t *index);
SRNOS_ParametersStatus SRNOS_ParametersInit(void);
SRNOS_ParametersStatus SRNOS_ParametersFreeze(void);
/* Task context only after startup. Get/Set operate on SDK-owned copies, not
 * application controller memory. Apply a Snapshot at the business cycle boundary.
 * Set changes RAM only. Batch writes are validated then applied atomically. */
SRNOS_ParametersStatus SRNOS_ParametersGet(uint16_t index,SRNOS_ParameterValue *out);
SRNOS_ParametersStatus SRNOS_ParametersSet(uint16_t index,const SRNOS_ParameterValue *value);
SRNOS_ParametersStatus SRNOS_ParametersGetState(SRNOS_ParametersState *out);
SRNOS_ParametersStatus SRNOS_ParametersSnapshot(const uint16_t *indices,size_t count,uint8_t *out,size_t capacity,size_t *written);
/* Blocking only in the calling task; never call from a control loop or ISR.
 * EEPROM work runs outside value locks. Concurrent storage returns BUSY.
 * Runtime LOAD failure leaves RAM unchanged; a concurrent RAM write causes
 * CONFLICT. SAVE freezes its snapshot and reports its own saved generation.
 * timeout_ms is the whole operation deadline; 0 selects 2000/5000 ms defaults.
 * Single-copy storage: interrupted SAVE can invalidate the previous record. */
/* Reset to declared defaults in RAM; persistence still requires explicit SAVE. */
SRNOS_ParametersStatus SRNOS_ParametersResetDefaults(void);
SRNOS_ParametersStatus SRNOS_ParametersLoad(void);
SRNOS_ParametersStatus SRNOS_ParametersSave(void);
SRNOS_ParametersStatus SRNOS_ParametersLoadWithTimeout(uint32_t timeout_ms);
SRNOS_ParametersStatus SRNOS_ParametersSaveWithTimeout(uint32_t timeout_ms);
size_t SRNOS_ParametersCount(void);
size_t SRNOS_ParametersPayloadSize(void);
uint32_t SRNOS_ParametersLayoutHash(void);
SRNOS_ParametersStatus SRNOS_ParametersGetByIndex(size_t,SRNOS_ParametersEntryInfo *);
SRNOS_ParametersStatus SRNOS_ParametersGetByName(const char *,SRNOS_ParametersEntryInfo *);
SRNOS_ParametersStatus SRNOS_ParametersGetValue(const char *,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetValueByIndex(size_t,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetDefaultValue(const char *,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetDefaultValueByIndex(size_t,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetMinValue(const char *,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetMinValueByIndex(size_t,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetMaxValue(const char *,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersGetMaxValueByIndex(size_t,char *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersSetValue(const char *,const char *);
SRNOS_ParametersStatus SRNOS_ParametersSetValueByIndex(size_t,const char *);
/* Binary values use little endian IEEE754 binary32/int32/uint32, bool one byte
 * 0 or 1; lengths and non-finite floats are strictly rejected. */
SRNOS_ParametersStatus SRNOS_ParametersCopyBinaryByIndex(size_t,SRNOS_ParametersBinaryValue,uint8_t *,size_t,size_t *);
SRNOS_ParametersStatus SRNOS_ParametersValidateBinaryByIndex(size_t,const uint8_t *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersSetBinaryByIndex(size_t,const uint8_t *,size_t);
SRNOS_ParametersStatus SRNOS_ParametersSetBinaryBatch(const SRNOS_ParametersBinaryWrite *,size_t,size_t *);
SRNOS_ParametersStatus SRNOS_ParametersDumpRegistry(SRNOS_ParametersDumpCallback,void *);
/* Task-only diagnostic dump through the SDK Logging queue at INFO level;
 * scheduler must be running or locked. Never enters startup boot capture.
 * DISABLED: no selected backend allows INFO. NO_SPACE: queue/pool rejected a
 * record. BUFFER_TOO_SMALL: a complete name=value line exceeds the log body.
 * Stops on first failure; accepted earlier records are not rolled back.
 * OK means every record was accepted, not delivered; Logging may drop records.
 * An empty registry is a successful no-op. Use DumpRegistry for custom export. */
SRNOS_ParametersStatus SRNOS_ParametersDumpDebugPort(void);
const char *SRNOS_ParametersStatusName(SRNOS_ParametersStatus);
const char *SRNOS_ParametersTypeName(SRNOS_ParametersType);
#ifdef __cplusplus
}
#endif
#endif
