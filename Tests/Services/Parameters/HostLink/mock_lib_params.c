#include "SirinEmbedOS/Services/Parameters/lib_params.h"

#include <string.h>

typedef struct {
    params_entry_info_t info;
    uint8_t value[4];
    uint8_t def[4];
    uint8_t min[4];
    uint8_t max[4];
} mock_param_t;

static mock_param_t s_params[4];
static uint8_t s_ready;

static void put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint32_t get32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) |
           ((uint32_t)in[2] << 16u) | ((uint32_t)in[3] << 24u);
}

static float get_float(const uint8_t *in)
{
    float value = 0.0f;
    memcpy(&value, in, sizeof(value));
    return value;
}

static void init_mock(void)
{
    if (s_ready != 0u) { return; }
    const char *names[] = {"gain", "offset", "limit", "enabled"};
    const params_type_t types[] = {
        PARAMS_TYPE_FLOAT, PARAMS_TYPE_INT32, PARAMS_TYPE_UINT32, PARAMS_TYPE_BOOL
    };
    for (size_t i = 0u; i < 4u; ++i) {
        s_params[i].info.index = i;
        s_params[i].info.name = names[i];
        s_params[i].info.type = types[i];
        s_params[i].info.size = (i == 3u) ? 1u : 4u;
        s_params[i].info.registered = 1u;
        put32(s_params[i].value, (uint32_t)(i + 1u));
        memcpy(s_params[i].def, s_params[i].value, 4u);
        put32(s_params[i].min, 0u);
        put32(s_params[i].max, 100u);
    }
    const float float_value = 1.0f;
    const float float_min = 0.0f;
    const float float_max = 10.0f;
    memcpy(s_params[0].value, &float_value, sizeof(float_value));
    memcpy(s_params[0].def, &float_value, sizeof(float_value));
    memcpy(s_params[0].min, &float_min, sizeof(float_min));
    memcpy(s_params[0].max, &float_max, sizeof(float_max));
    s_params[3].value[0] = 1u;
    s_params[3].def[0] = 1u;
    s_params[3].min[0] = 0u;
    s_params[3].max[0] = 1u;
    s_ready = 1u;
}

params_status_t params_init(void) { init_mock(); return PARAMS_STATUS_OK; }
params_status_t params_load(void) { init_mock(); return PARAMS_STATUS_OK; }
params_status_t params_save(void) { init_mock(); return PARAMS_STATUS_OK; }
size_t params_count(void) { init_mock(); return 4u; }
size_t params_payload_size(void) { return 13u; }
uint32_t params_layout_hash(void) { return 0x78563412u; }

params_status_t params_get_by_index(size_t index, params_entry_info_t *out)
{
    init_mock();
    if ((out == NULL) || (index >= 4u)) { return PARAMS_STATUS_NOT_FOUND; }
    *out = s_params[index].info;
    return PARAMS_STATUS_OK;
}

params_status_t params_copy_binary_by_index(size_t index,
                                            params_binary_value_t kind,
                                            uint8_t *out,
                                            size_t out_len,
                                            size_t *written)
{
    init_mock();
    if ((index >= 4u) || (out == NULL) || (written == NULL)) {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    const size_t len = s_params[index].info.size;
    if (out_len < len) { return PARAMS_STATUS_BUFFER_TOO_SMALL; }
    const uint8_t *source = s_params[index].value;
    if (kind == PARAMS_BINARY_VALUE_DEFAULT) { source = s_params[index].def; }
    else if (kind == PARAMS_BINARY_VALUE_MIN) { source = s_params[index].min; }
    else if (kind == PARAMS_BINARY_VALUE_MAX) { source = s_params[index].max; }
    memcpy(out, source, len);
    *written = len;
    return PARAMS_STATUS_OK;
}

params_status_t params_validate_binary_by_index(size_t index,
                                                const uint8_t *data,
                                                size_t len)
{
    init_mock();
    if ((index >= 4u) || (data == NULL)) { return PARAMS_STATUS_NOT_FOUND; }
    if (len != s_params[index].info.size) { return PARAMS_STATUS_TYPE_MISMATCH; }
    if (index == 3u) {
        return (data[0] <= 1u) ? PARAMS_STATUS_OK : PARAMS_STATUS_OUT_OF_RANGE;
    }
    if (index == 0u) {
        const float value = get_float(data);
        return ((value == value) && (value >= 0.0f) && (value <= 10.0f)) ?
            PARAMS_STATUS_OK : PARAMS_STATUS_OUT_OF_RANGE;
    }
    return (get32(data) <= 100u) ? PARAMS_STATUS_OK : PARAMS_STATUS_OUT_OF_RANGE;
}

params_status_t params_set_binary_by_index(size_t index,
                                           const uint8_t *data,
                                           size_t len)
{
    params_status_t status = params_validate_binary_by_index(index, data, len);
    if (status == PARAMS_STATUS_OK) { memcpy(s_params[index].value, data, len); }
    return status;
}

params_status_t params_set_binary_batch(const params_binary_write_t *writes,
                                        size_t count,
                                        size_t *failed_item)
{
    for (size_t i = 0u; i < count; ++i) {
        params_status_t status = params_validate_binary_by_index(
            writes[i].index, writes[i].data, writes[i].len);
        if (status != PARAMS_STATUS_OK) {
            if (failed_item != NULL) { *failed_item = i; }
            return status;
        }
    }
    for (size_t i = 0u; i < count; ++i) {
        (void)params_set_binary_by_index(writes[i].index, writes[i].data, writes[i].len);
    }
    if (failed_item != NULL) { *failed_item = count; }
    return PARAMS_STATUS_OK;
}

params_status_t params_snapshot(const uint16_t *indices, size_t count, uint8_t *out, size_t capacity, size_t *written)
{
    *written = 0;
    for (size_t i=0;i<count;++i) {
        size_t n=0;
        params_status_t st=params_copy_binary_by_index(indices[i],PARAMS_BINARY_VALUE_CURRENT,out+*written,capacity-*written,&n);
        if(st!=PARAMS_STATUS_OK)return st;
        *written+=n;
    }
    return PARAMS_STATUS_OK;
}
