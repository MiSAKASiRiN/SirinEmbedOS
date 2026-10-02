#include "SirinEmbedOS/Services/Parameters/Parameters.h"

#include <string.h>
#include "SirinEmbedOS/Services/Storage.h"

typedef struct {
    SRNOS_ParametersEntryInfo info;
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
    const SRNOS_ParametersType types[] = {
        SRNOS_PARAMETERS_TYPE_FLOAT, SRNOS_PARAMETERS_TYPE_INT32, SRNOS_PARAMETERS_TYPE_UINT32, SRNOS_PARAMETERS_TYPE_BOOL
    };
    for (size_t i = 0u; i < 4u; ++i) {
        s_params[i].info.index = i;
        s_params[i].info.stable_id = (uint32_t)(100u+i);
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

SRNOS_ParametersStatus SRNOS_ParametersInit(void) { init_mock(); return SRNOS_PARAMETERS_STATUS_OK; }
SRNOS_ParametersStatus SRNOS_ParametersLoad(void) { init_mock(); return SRNOS_PARAMETERS_STATUS_OK; }
SRNOS_ParametersStatus SRNOS_ParametersSave(void) { init_mock(); return SRNOS_PARAMETERS_STATUS_OK; }
size_t SRNOS_ParametersCount(void) { init_mock(); return 4u; }
size_t SRNOS_ParametersPayloadSize(void) { return 13u; }
uint32_t SRNOS_ParametersLayoutHash(void) { return 0x78563412u; }

SRNOS_ParametersStatus SRNOS_ParametersGetByIndex(size_t index, SRNOS_ParametersEntryInfo *out)
{
    init_mock();
    if ((out == NULL) || (index >= 4u)) { return SRNOS_PARAMETERS_STATUS_NOT_FOUND; }
    *out = s_params[index].info;
    return SRNOS_PARAMETERS_STATUS_OK;
}

SRNOS_ParametersStatus SRNOS_ParametersCopyBinaryByIndex(size_t index,
                                            SRNOS_ParametersBinaryValue kind,
                                            uint8_t *out,
                                            size_t out_len,
                                            size_t *written)
{
    init_mock();
    if ((index >= 4u) || (out == NULL) || (written == NULL)) {
        return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    }
    const size_t len = s_params[index].info.size;
    if (out_len < len) { return SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL; }
    const uint8_t *source = s_params[index].value;
    if (kind == SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT) { source = s_params[index].def; }
    else if (kind == SRNOS_PARAMETERS_BINARY_VALUE_MIN) { source = s_params[index].min; }
    else if (kind == SRNOS_PARAMETERS_BINARY_VALUE_MAX) { source = s_params[index].max; }
    memcpy(out, source, len);
    *written = len;
    return SRNOS_PARAMETERS_STATUS_OK;
}

SRNOS_ParametersStatus SRNOS_ParametersValidateBinaryByIndex(size_t index,
                                                const uint8_t *data,
                                                size_t len)
{
    init_mock();
    if ((index >= 4u) || (data == NULL)) { return SRNOS_PARAMETERS_STATUS_NOT_FOUND; }
    if (len != s_params[index].info.size) { return SRNOS_PARAMETERS_STATUS_TYPE_MISMATCH; }
    if (index == 3u) {
        return (data[0] <= 1u) ? SRNOS_PARAMETERS_STATUS_OK : SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;
    }
    if (index == 0u) {
        const float value = get_float(data);
        return ((value == value) && (value >= 0.0f) && (value <= 10.0f)) ?
            SRNOS_PARAMETERS_STATUS_OK : SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;
    }
    return (get32(data) <= 100u) ? SRNOS_PARAMETERS_STATUS_OK : SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;
}

SRNOS_ParametersStatus SRNOS_ParametersSetBinaryByIndex(size_t index,
                                           const uint8_t *data,
                                           size_t len)
{
    SRNOS_ParametersStatus status = SRNOS_ParametersValidateBinaryByIndex(index, data, len);
    if (status == SRNOS_PARAMETERS_STATUS_OK) { memcpy(s_params[index].value, data, len); }
    return status;
}

SRNOS_ParametersStatus SRNOS_ParametersSetBinaryBatch(const SRNOS_ParametersBinaryWrite *writes,
                                        size_t count,
                                        size_t *failed_item)
{
    for (size_t i = 0u; i < count; ++i) {
        SRNOS_ParametersStatus status = SRNOS_ParametersValidateBinaryByIndex(
            writes[i].index, writes[i].data, writes[i].len);
        for(size_t j=0;j<i;++j)if(writes[j].index==writes[i].index)status=SRNOS_PARAMETERS_STATUS_DUPLICATE;
        if (status != SRNOS_PARAMETERS_STATUS_OK) {
            if (failed_item != NULL) { *failed_item = i; }
            return status;
        }
    }
    for (size_t i = 0u; i < count; ++i) {
        (void)SRNOS_ParametersSetBinaryByIndex(writes[i].index, writes[i].data, writes[i].len);
    }
    if (failed_item != NULL) { *failed_item = count; }
    return SRNOS_PARAMETERS_STATUS_OK;
}

SRNOS_ParametersStatus SRNOS_ParametersSnapshot(const uint16_t *indices, size_t count, uint8_t *out, size_t capacity, size_t *written)
{
    *written = 0;
    for (size_t i=0;i<count;++i) {
        size_t n=0;
        SRNOS_ParametersStatus st=SRNOS_ParametersCopyBinaryByIndex(indices[i],SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,out+*written,capacity-*written,&n);
        if(st!=SRNOS_PARAMETERS_STATUS_OK)return st;
        *written+=n;
    }
    return SRNOS_PARAMETERS_STATUS_OK;
}

SRNOS_ParametersStatus SRNOS_ParametersGetState(SRNOS_ParametersState *out)
{
    memset(out,0,sizeof(*out));out->application_id=0x12345678u;out->compatibility_version=2;
    out->generation=12;out->saved_generation=10;out->storage_revision=5;
    out->dirty=true;out->saved_valid=true;out->loaded=4;
    return SRNOS_PARAMETERS_STATUS_OK;
}
bool SRNOS_StorageGetIdentity(SRNOS_StorageIdentity *out)
{
    memset(out,0,sizeof(*out));memcpy(out->serial,"00123456789012345678",20);
    out->valid=true;out->revision=3;out->status=SRNOS_STORAGE_OK;return true;
}
