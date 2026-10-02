#include "SirinEmbedOS/Services/Parameters/HostLink/ParameterProtocol.h"

#include "SirinEmbedOS/Services/Parameters/Parameters.h"
#include "SirinEmbedOS/Services/Storage.h"
#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} writer_t;

static SRNOS_ParameterProtocolConfig s_config;

static bool wr_u8(writer_t *writer, uint8_t value)
{
    if (writer->len >= writer->cap) {
        return false;
    }
    writer->buf[writer->len++] = value;
    return true;
}

static bool wr_u16(writer_t *writer, uint16_t value)
{
    if (!SRNOS_Bounds_RangeValid(writer->len, 2u, writer->cap)) {
        return false;
    }
    if (!SRNOS_Endian_StoreLE16(
            SRNOS_Memory_MutableBytes(writer->buf, writer->cap),
            writer->len, value)) return false;
    writer->len += 2u;
    return true;
}

static bool wr_u32(writer_t *writer, uint32_t value)
{
    if (!SRNOS_Bounds_RangeValid(writer->len, 4u, writer->cap)) {
        return false;
    }
    if (!SRNOS_Endian_StoreLE32(
            SRNOS_Memory_MutableBytes(writer->buf, writer->cap),
            writer->len, value)) return false;
    writer->len += 4u;
    return true;
}

static bool wr_bytes(writer_t *writer, const void *data, size_t len)
{
    if (((data == NULL) && (len != 0u)) || ((writer->len + len) > writer->cap)) {
        return false;
    }
    if (len != 0u) {
        if (!SRNOS_Memory_Copy(
                SRNOS_Memory_MutableBytes(&writer->buf[writer->len],
                                       writer->cap - writer->len),
                SRNOS_Memory_Bytes(data, len))) return false;
        writer->len += len;
    }
    return true;
}

static uint16_t map_params_status(SRNOS_ParametersStatus status)
{
    switch (status) {
    case SRNOS_PARAMETERS_STATUS_OK:
        return SRNOS_HOST_LINK_STATUS_OK;
    case SRNOS_PARAMETERS_STATUS_NOT_FOUND:
        return SRNOS_HOST_LINK_STATUS_NOT_FOUND;
    case SRNOS_PARAMETERS_STATUS_TYPE_MISMATCH:
        return SRNOS_HOST_LINK_STATUS_TYPE_MISMATCH;
    case SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE:
        return SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE;
    case SRNOS_PARAMETERS_STATUS_EEPROM_BUSY:
        return SRNOS_HOST_LINK_STATUS_BUSY;
    case SRNOS_PARAMETERS_STATUS_CONFLICT:
        return SRNOS_HOST_LINK_STATUS_CONFLICT;
    case SRNOS_PARAMETERS_STATUS_DISABLED:
        return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    case SRNOS_PARAMETERS_STATUS_TIMEOUT:
    case SRNOS_PARAMETERS_STATUS_EEPROM_ERROR:
    case SRNOS_PARAMETERS_STATUS_BAD_DATA:
    case SRNOS_PARAMETERS_STATUS_LAYOUT_MISMATCH:
    case SRNOS_PARAMETERS_STATUS_CRC_ERROR:
        return SRNOS_HOST_LINK_STATUS_STORAGE_ERROR;
    case SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL:
    case SRNOS_PARAMETERS_STATUS_INVALID_PARAM:
    case SRNOS_PARAMETERS_STATUS_DUPLICATE:
    case SRNOS_PARAMETERS_STATUS_PARSE_ERROR:
    case SRNOS_PARAMETERS_STATUS_READ_ONLY:
    case SRNOS_PARAMETERS_STATUS_LOCKED:
        return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    default:
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
}

static uint16_t handle_schema(void *user,
                              const SRNOS_ProtocolView *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags)
{
    (void)user;
    if ((request->payload_len != 3u) || (request->payload[2] == 0u)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const size_t total = SRNOS_ParametersCount();
    uint16_t cursor_u16 = 0u;
    if (!SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(request->payload,
                                             request->payload_len),
                            0u, &cursor_u16)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const size_t cursor = cursor_u16;
    const size_t requested = request->payload[2];
    if ((cursor > total) || (total > UINT16_MAX)) {
        return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    }

    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, SRNOS_ParametersLayoutHash()) ||
        !wr_u16(&writer, (uint16_t)total) ||
        !wr_u16(&writer, 0xFFFFu) ||
        !wr_u8(&writer, 0u)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    const size_t next_offset = 6u;
    const size_t count_offset = 8u;
    size_t index = cursor;
    uint8_t count = 0u;
    while ((index < total) && ((size_t)count < requested)) {
        SRNOS_ParametersEntryInfo info;
        SRNOS_ParametersStatus status = SRNOS_ParametersGetByIndex(index, &info);
        if (status != SRNOS_PARAMETERS_STATUS_OK) {
            return map_params_status(status);
        }
        const size_t name_len = strlen(info.name);
        const size_t entry_len = 6u + name_len + ((size_t)info.size * 3u);
        if ((writer.len + entry_len) > writer.cap) {
            break;
        }
        if (!wr_u16(&writer, (uint16_t)info.index) ||
            !wr_u8(&writer, (uint8_t)info.type) ||
            !wr_u8(&writer, (uint8_t)(info.registered != 0u)) ||
            !wr_u8(&writer, (uint8_t)info.size) ||
            !wr_u8(&writer, (uint8_t)name_len) ||
            !wr_bytes(&writer, info.name, name_len)) {
            return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        }
        const SRNOS_ParametersBinaryValue kinds[] = {
            SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT,
            SRNOS_PARAMETERS_BINARY_VALUE_MIN,
            SRNOS_PARAMETERS_BINARY_VALUE_MAX,
        };
        for (size_t value = 0u; value < 3u; ++value) {
            size_t written = 0u;
            status = SRNOS_ParametersCopyBinaryByIndex(index, kinds[value],
                                                  &writer.buf[writer.len],
                                                  writer.cap - writer.len,
                                                  &written);
            if (status != SRNOS_PARAMETERS_STATUS_OK) {
                return map_params_status(status);
            }
            writer.len += written;
        }
        ++count;
        ++index;
    }
    if ((count == 0u) && (cursor < total)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    const uint16_t next = (index < total) ? (uint16_t)index : 0xFFFFu;
    (void)SRNOS_Endian_StoreLE16(
        SRNOS_Memory_MutableBytes(writer.buf, writer.cap), next_offset, next);
    writer.buf[count_offset] = count;
    *response_flags = (next != 0xFFFFu) ? SRNOS_PROTOCOL_FLAG_MORE : 0u;
    *response_len = writer.len;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static uint16_t handle_read(void *user,
                            const SRNOS_ProtocolView *request,
                            uint8_t *response,
                            size_t response_cap,
                            size_t *response_len,
                            uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if ((request->payload_len < 1u) || (request->payload[0] == 0u)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const size_t count = request->payload[0];
    if (count > SRNOS_PARAMETERS_MAX_COUNT || request->payload_len != (1u + count * 2u)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, SRNOS_ParametersLayoutHash()) || !wr_u8(&writer, (uint8_t)count)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint16_t indices[SRNOS_PARAMETERS_MAX_COUNT];
    uint8_t values[SRNOS_PARAMETERS_MAX_COUNT * 4u];
    for (size_t item = 0; item < count; ++item) {
        if (!SRNOS_Endian_LoadLE16(payload, 1u + item * 2u, &indices[item]))
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    size_t snapshot_size = 0, snapshot_offset = 0;
    SRNOS_ParametersStatus snapshot_status = SRNOS_ParametersSnapshot(indices, count, values, sizeof(values), &snapshot_size);
    if (snapshot_status != SRNOS_PARAMETERS_STATUS_OK) return map_params_status(snapshot_status);
    for (size_t item = 0u; item < count; ++item) {
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(payload, 1u + item * 2u, &index)) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        SRNOS_ParametersEntryInfo info;
        SRNOS_ParametersStatus status = SRNOS_ParametersGetByIndex(index, &info);
        if (status != SRNOS_PARAMETERS_STATUS_OK) {
            return map_params_status(status);
        }
        if (!wr_u16(&writer, index) || !wr_u8(&writer, (uint8_t)info.type) ||
            !wr_u8(&writer, (uint8_t)info.size)) {
            return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        }
        if (snapshot_offset + info.size > snapshot_size ||
            !wr_bytes(&writer, values + snapshot_offset, info.size))
            return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        snapshot_offset += info.size;
    }
    *response_len = writer.len;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static uint16_t handle_write(void *user,
                             const SRNOS_ProtocolView *request,
                             uint8_t *response,
                             size_t response_cap,
                             size_t *response_len,
                             uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if (request->payload_len < 5u) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint32_t layout_hash = 0u;
    if (!SRNOS_Endian_LoadLE32(payload, 0u, &layout_hash)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const size_t count = request->payload[4];
    if ((layout_hash != SRNOS_ParametersLayoutHash()) || (count == 0u) ||
        (count > SRNOS_PARAMETERS_MAX_COUNT)) {
        return (layout_hash != SRNOS_ParametersLayoutHash()) ?
            SRNOS_HOST_LINK_STATUS_CONFLICT : SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }

    SRNOS_ParametersBinaryWrite writes[SRNOS_PARAMETERS_MAX_COUNT];
    size_t offset = 5u;
    for (size_t item = 0u; item < count; ++item) {
        if ((offset + 4u) > request->payload_len) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(payload, offset, &index)) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        const uint8_t type = request->payload[offset + 2u];
        const uint8_t value_len = request->payload[offset + 3u];
        offset += 4u;
        if ((offset + value_len) > request->payload_len) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        SRNOS_ParametersEntryInfo info;
        SRNOS_ParametersStatus status = SRNOS_ParametersGetByIndex(index, &info);
        if (status != SRNOS_PARAMETERS_STATUS_OK) {
            return map_params_status(status);
        }
        if ((type != (uint8_t)info.type) || (value_len != info.size)) {
            return SRNOS_HOST_LINK_STATUS_TYPE_MISMATCH;
        }
        writes[item].index = index;
        writes[item].data = &request->payload[offset];
        writes[item].len = value_len;
        offset += value_len;
    }
    if (offset != request->payload_len) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }

    size_t failed_item = count;
    const SRNOS_ParametersStatus status = SRNOS_ParametersSetBinaryBatch(writes, count, &failed_item);
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, SRNOS_ParametersLayoutHash()) ||
        !wr_u8(&writer, (uint8_t)count) ||
        !wr_u8(&writer, (failed_item < count) ? (uint8_t)failed_item : 0xFFu) ||
        !wr_u8(&writer, (uint8_t)status) || !wr_u8(&writer, 0u)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    *response_len = writer.len;
    return map_params_status(status);
}

static uint16_t handle_storage(void *user,
                               const SRNOS_ProtocolView *request,
                               uint8_t *response,
                               size_t response_cap,
                               size_t *response_len,
                               uint8_t *response_flags)
{
    (void)user;
    (void)response;
    (void)response_cap;
    *response_len = 0u;
    *response_flags = 0u;
    if (s_config.disable_storage) return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    if (request->payload_len != 0u) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    if ((s_config.defer == NULL) ||
        !s_config.defer(s_config.user, request->header.message,
                        request->header.sequence)) {
        return SRNOS_HOST_LINK_STATUS_BUSY;
    }
    return SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL;
}

static uint16_t handle_state(void *user, const SRNOS_ProtocolView *request,
    uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*length=0;*flags=0;
    if(request->payload_len)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    SRNOS_ParametersState state;
    SRNOS_ParametersStatus r=SRNOS_ParametersGetState(&state);
    if(r!=SRNOS_PARAMETERS_STATUS_OK)return map_params_status(r);
    writer_t w={response,cap,0};
    uint8_t bits=(state.dirty?1u:0u)|(state.saved_valid?2u:0u)|(state.busy?4u:0u);
    if(!wr_u8(&w,1)||!wr_u8(&w,bits)||!wr_u16(&w,state.compatibility_version)||
       !wr_u32(&w,state.application_id)||!wr_u32(&w,state.generation)||
       !wr_u32(&w,state.saved_generation)||!wr_u32(&w,state.storage_revision)||
       !wr_u16(&w,(uint16_t)state.last_load)||!wr_u16(&w,(uint16_t)state.last_save)||
       !wr_u16(&w,state.loaded)||!wr_u16(&w,state.defaulted)||!wr_u16(&w,state.ignored)||
       !wr_u16(&w,(uint16_t)SRNOS_ParametersCount()))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    *length=w.len;return SRNOS_HOST_LINK_STATUS_OK;
}
static uint16_t handle_identity(void *user,const SRNOS_ProtocolView *request,
    uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*length=0;*flags=0;
    if(request->payload_len)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    SRNOS_StorageIdentity identity;
    if(!SRNOS_StorageGetIdentity(&identity))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    const uint8_t empty[SRNOS_STORAGE_SERIAL_LENGTH]={0};
    writer_t w={response,cap,0};
    if(!wr_u8(&w,1)||!wr_u8(&w,identity.valid?1:0)||!wr_u16(&w,(uint16_t)identity.status)||
       !wr_u32(&w,identity.revision)||!wr_bytes(&w,identity.valid?(const void*)identity.serial:(const void*)empty,SRNOS_STORAGE_SERIAL_LENGTH))
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    *length=w.len;return SRNOS_HOST_LINK_STATUS_OK;
}
static uint16_t handle_stable_ids(void *user,const SRNOS_ProtocolView *request,
    uint8_t *response,size_t cap,size_t *length,uint8_t *flags)
{
    (void)user;*length=0;*flags=0;
    if(request->payload_len!=3u||!request->payload[2])return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    uint16_t cursor=0;
    if(!SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(request->payload,request->payload_len),0,&cursor))return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    const size_t total=SRNOS_ParametersCount();
    if(cursor>total)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    writer_t w={response,cap,0};
    if(!wr_u32(&w,SRNOS_ParametersLayoutHash())||!wr_u16(&w,(uint16_t)total)||!wr_u16(&w,0xffff)||!wr_u8(&w,0))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    uint8_t n=0;
    while(cursor<total&&n<request->payload[2]&&w.cap-w.len>=6u){
        SRNOS_ParametersEntryInfo info;
        if(SRNOS_ParametersGetByIndex(cursor,&info)!=SRNOS_PARAMETERS_STATUS_OK)return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        if(!wr_u16(&w,cursor)||!wr_u32(&w,info.stable_id))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        ++cursor;++n;
    }
    if(!n&&cursor<total)return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    const uint16_t next=cursor<total?cursor:0xffff;
    (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(response,cap),6,next);
    response[8]=n;*flags=next!=0xffff?SRNOS_PROTOCOL_FLAG_MORE:0;*length=w.len;
    return SRNOS_HOST_LINK_STATUS_OK;
}

SRNOS_HostLinkResult SRNOS_ParameterProtocolRegister(
    SRNOS_HostLinkRegistration *link,
    const SRNOS_ParameterProtocolConfig *config)
{
    if (link == NULL) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    SRNOS_ParameterProtocolConfig candidate = {0};
    if (config != NULL) candidate = *config;
    const uint8_t request_mask = SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST);
    if (SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_SCHEMA, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_schema, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_READ, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_read, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_WRITE, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_write, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_SAVE, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_storage, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_LOAD, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_storage, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_STATE, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_state, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_IDENTITY, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_identity, NULL) != SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkRegisterHandler(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  SRNOS_PARAMETER_PROTOCOL_STABLE_IDS, request_mask,
                                  SRNOS_HOST_LINK_QOS_RELIABLE, handle_stable_ids, NULL) != SRNOS_HOST_LINK_OK) {
        return SRNOS_HOST_LINK_ERR_FULL;
    }
    /* Failed/frozen registration must never mutate the live service policy. */
    s_config = candidate;
    return SRNOS_HOST_LINK_OK;
}

uint16_t SRNOS_ParameterProtocolRunStorage(uint8_t message,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len)
{
    if ((response == NULL) || (response_len == NULL) || (response_cap < 7u)) {
        return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    }
    SRNOS_ParametersStatus status;
    if (message == SRNOS_PARAMETER_PROTOCOL_SAVE) {
        status = SRNOS_ParametersSave();
    } else if (message == SRNOS_PARAMETER_PROTOCOL_LOAD) {
        status = SRNOS_ParametersLoad();
    } else {
        return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    }
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, SRNOS_ParametersLayoutHash()) ||
        !wr_u16(&writer, (uint16_t)SRNOS_ParametersCount()) ||
        !wr_u8(&writer, (uint8_t)status)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    *response_len = writer.len;
    return map_params_status(status);
}
