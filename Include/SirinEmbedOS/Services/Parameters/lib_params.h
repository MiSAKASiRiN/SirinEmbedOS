#ifndef OMX_LIB_PARAMS_H
#define OMX_LIB_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PARAMS_MAX_COUNT
#define PARAMS_MAX_COUNT 32U
#endif

#ifndef PARAMS_MAX_NAME_LEN
#define PARAMS_MAX_NAME_LEN 48U
#endif

#ifndef PARAMS_VALUE_STRING_MAX_LEN
#define PARAMS_VALUE_STRING_MAX_LEN 32U
#endif

#ifndef PARAMS_EEPROM_I2C_BUS_ID
#define PARAMS_EEPROM_I2C_BUS_ID 0U
#endif

#ifndef PARAMS_EEPROM_I2C_ADDR_7BIT
#define PARAMS_EEPROM_I2C_ADDR_7BIT 0x50U
#endif

#ifndef PARAMS_EEPROM_START_ADDR
#define PARAMS_EEPROM_START_ADDR 0x40U
#endif

#ifndef PARAMS_EEPROM_XFER_TIMEOUT_MS
#define PARAMS_EEPROM_XFER_TIMEOUT_MS 20U
#endif

#ifndef PARAMS_EEPROM_WRITE_CYCLE_TIMEOUT_MS
#define PARAMS_EEPROM_WRITE_CYCLE_TIMEOUT_MS 10U
#endif

typedef enum
{
    PARAMS_STATUS_OK = 0,
    PARAMS_STATUS_ERROR,
    PARAMS_STATUS_INVALID_PARAM,
    PARAMS_STATUS_NOT_FOUND,
    PARAMS_STATUS_DUPLICATE,
    PARAMS_STATUS_NO_SPACE,
    PARAMS_STATUS_TYPE_MISMATCH,
    PARAMS_STATUS_OUT_OF_RANGE,
    PARAMS_STATUS_BUFFER_TOO_SMALL,
    PARAMS_STATUS_PARSE_ERROR,
    PARAMS_STATUS_EEPROM_ERROR,
    PARAMS_STATUS_EEPROM_BUSY,
    PARAMS_STATUS_BAD_DATA,
    PARAMS_STATUS_LAYOUT_MISMATCH,
    PARAMS_STATUS_CRC_ERROR,
    PARAMS_STATUS_NOT_INITIALIZED,
    PARAMS_STATUS_LOCKED,
    PARAMS_STATUS_READ_ONLY
} params_status_t;

typedef enum
{
    PARAMS_TYPE_INVALID = 0,
    PARAMS_TYPE_FLOAT,
    PARAMS_TYPE_INT32,
    PARAMS_TYPE_UINT32,
    PARAMS_TYPE_BOOL
} params_type_t;

/* This registry contains application/business values only. Hardware routes,
 * service selection, CAN modes, IMU selection and memory budgets must never be
 * registered here. Remote writes require explicit MUTABLE_BUSINESS access. */
typedef enum { PARAMS_READ_ONLY = 0, PARAMS_MUTABLE_BUSINESS = 1 } params_access_t;

typedef struct
{
    uint32_t      index;
    const char   *name;
    uint32_t      name_hash;
    params_type_t type;
    uint16_t      size;
    uint8_t       registered;
    uint8_t       access;
} params_entry_info_t;

typedef enum
{
    PARAMS_BINARY_VALUE_CURRENT = 0,
    PARAMS_BINARY_VALUE_DEFAULT,
    PARAMS_BINARY_VALUE_MIN,
    PARAMS_BINARY_VALUE_MAX
} params_binary_value_t;

typedef struct
{
    uint16_t index;
    const uint8_t *data;
    uint16_t len;
} params_binary_write_t;

typedef void (*params_dump_callback_t)(const params_entry_info_t *info,
                                       const char *value,
                                       const char *default_value,
                                       const char *min_value,
                                       const char *max_value,
                                       void *ctx);

params_status_t params_init(void);
params_status_t SRNOS_ParametersFreeze(void);
/* Copy multiple values under one short lock; output is packed in index order.
 * Task context only; no returned pointer aliases mutable storage. */
params_status_t params_snapshot(const uint16_t *indices, size_t count,
                                uint8_t *out, size_t capacity, size_t *written);
params_status_t params_load(void);
params_status_t params_save(void);

size_t params_count(void);
size_t params_payload_size(void);
uint32_t params_layout_hash(void);

params_status_t params_get_by_index(size_t index, params_entry_info_t *out);
params_status_t params_get_by_name(const char *name, params_entry_info_t *out);
params_status_t params_get_value(const char *name, char *out, size_t out_len);
params_status_t params_get_value_by_index(size_t index, char *out, size_t out_len);
params_status_t params_get_default_value(const char *name, char *out, size_t out_len);
params_status_t params_get_default_value_by_index(size_t index, char *out, size_t out_len);
params_status_t params_get_min_value(const char *name, char *out, size_t out_len);
params_status_t params_get_min_value_by_index(size_t index, char *out, size_t out_len);
params_status_t params_get_max_value(const char *name, char *out, size_t out_len);
params_status_t params_get_max_value_by_index(size_t index, char *out, size_t out_len);
params_status_t params_set_value(const char *name, const char *text);
params_status_t params_set_value_by_index(size_t index, const char *text);

params_status_t params_copy_binary_by_index(size_t index,
                                            params_binary_value_t value_kind,
                                            uint8_t *out,
                                            size_t out_len,
                                            size_t *written);
params_status_t params_validate_binary_by_index(size_t index,
                                                const uint8_t *data,
                                                size_t len);
params_status_t params_set_binary_by_index(size_t index,
                                           const uint8_t *data,
                                           size_t len);
params_status_t params_set_binary_batch(const params_binary_write_t *writes,
                                        size_t count,
                                        size_t *failed_item);

params_status_t params_dump_registry(params_dump_callback_t cb, void *ctx);
params_status_t params_dump_debug_port(void);

const char *params_status_name(params_status_t status);
const char *params_type_name(params_type_t type);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OMX_LIB_PARAMS_H */
