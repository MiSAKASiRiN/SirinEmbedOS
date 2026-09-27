#include "SirinEmbedOS/Services/Parameters/HostLink/tskptt_param_console.h"

#include "SirinEmbedOS/Services/Parameters/lib_params.h"
#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} writer_t;

static tskptt_param_service_config_t s_config;

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

static uint16_t map_params_status(params_status_t status)
{
    switch (status) {
    case PARAMS_STATUS_OK:
        return SRN_LINK_STATUS_OK;
    case PARAMS_STATUS_NOT_FOUND:
        return SRN_LINK_STATUS_NOT_FOUND;
    case PARAMS_STATUS_TYPE_MISMATCH:
        return SRN_LINK_STATUS_TYPE_MISMATCH;
    case PARAMS_STATUS_OUT_OF_RANGE:
        return SRN_LINK_STATUS_OUT_OF_RANGE;
    case PARAMS_STATUS_EEPROM_ERROR:
    case PARAMS_STATUS_EEPROM_BUSY:
    case PARAMS_STATUS_BAD_DATA:
    case PARAMS_STATUS_LAYOUT_MISMATCH:
    case PARAMS_STATUS_CRC_ERROR:
        return SRN_LINK_STATUS_STORAGE_ERROR;
    case PARAMS_STATUS_BUFFER_TOO_SMALL:
    case PARAMS_STATUS_INVALID_PARAM:
    case PARAMS_STATUS_PARSE_ERROR:
    case PARAMS_STATUS_READ_ONLY:
    case PARAMS_STATUS_LOCKED:
        return SRN_LINK_STATUS_INVALID_ARGUMENT;
    default:
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
}

static uint16_t handle_schema(void *user,
                              const srn_cobs_prot_view_t *request,
                              uint8_t *response,
                              size_t response_cap,
                              size_t *response_len,
                              uint8_t *response_flags)
{
    (void)user;
    if ((request->payload_len != 3u) || (request->payload[2] == 0u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const size_t total = params_count();
    uint16_t cursor_u16 = 0u;
    if (!SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(request->payload,
                                             request->payload_len),
                            0u, &cursor_u16)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const size_t cursor = cursor_u16;
    const size_t requested = request->payload[2];
    if ((cursor > total) || (total > UINT16_MAX)) {
        return SRN_LINK_STATUS_INVALID_ARGUMENT;
    }

    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, params_layout_hash()) ||
        !wr_u16(&writer, (uint16_t)total) ||
        !wr_u16(&writer, 0xFFFFu) ||
        !wr_u8(&writer, 0u)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    const size_t next_offset = 6u;
    const size_t count_offset = 8u;
    size_t index = cursor;
    uint8_t count = 0u;
    while ((index < total) && ((size_t)count < requested)) {
        params_entry_info_t info;
        params_status_t status = params_get_by_index(index, &info);
        if (status != PARAMS_STATUS_OK) {
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
            return SRN_LINK_STATUS_INTERNAL_ERROR;
        }
        const params_binary_value_t kinds[] = {
            PARAMS_BINARY_VALUE_DEFAULT,
            PARAMS_BINARY_VALUE_MIN,
            PARAMS_BINARY_VALUE_MAX,
        };
        for (size_t value = 0u; value < 3u; ++value) {
            size_t written = 0u;
            status = params_copy_binary_by_index(index, kinds[value],
                                                  &writer.buf[writer.len],
                                                  writer.cap - writer.len,
                                                  &written);
            if (status != PARAMS_STATUS_OK) {
                return map_params_status(status);
            }
            writer.len += written;
        }
        ++count;
        ++index;
    }
    if ((count == 0u) && (cursor < total)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    const uint16_t next = (index < total) ? (uint16_t)index : 0xFFFFu;
    (void)SRNOS_Endian_StoreLE16(
        SRNOS_Memory_MutableBytes(writer.buf, writer.cap), next_offset, next);
    writer.buf[count_offset] = count;
    *response_flags = (next != 0xFFFFu) ? SRN_COBS_PROT_FLAG_MORE : 0u;
    *response_len = writer.len;
    return SRN_LINK_STATUS_OK;
}

static uint16_t handle_read(void *user,
                            const srn_cobs_prot_view_t *request,
                            uint8_t *response,
                            size_t response_cap,
                            size_t *response_len,
                            uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if ((request->payload_len < 1u) || (request->payload[0] == 0u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const size_t count = request->payload[0];
    if (count > PARAMS_MAX_COUNT || request->payload_len != (1u + count * 2u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, params_layout_hash()) || !wr_u8(&writer, (uint8_t)count)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint16_t indices[PARAMS_MAX_COUNT];
    uint8_t values[PARAMS_MAX_COUNT * 4u];
    for (size_t item = 0; item < count; ++item) {
        if (!SRNOS_Endian_LoadLE16(payload, 1u + item * 2u, &indices[item]))
            return SRN_LINK_STATUS_BAD_LENGTH;
    }
    size_t snapshot_size = 0, snapshot_offset = 0;
    params_status_t snapshot_status = params_snapshot(indices, count, values, sizeof(values), &snapshot_size);
    if (snapshot_status != PARAMS_STATUS_OK) return map_params_status(snapshot_status);
    for (size_t item = 0u; item < count; ++item) {
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(payload, 1u + item * 2u, &index)) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        params_entry_info_t info;
        params_status_t status = params_get_by_index(index, &info);
        if (status != PARAMS_STATUS_OK) {
            return map_params_status(status);
        }
        if (!wr_u16(&writer, index) || !wr_u8(&writer, (uint8_t)info.type) ||
            !wr_u8(&writer, (uint8_t)info.size)) {
            return SRN_LINK_STATUS_INTERNAL_ERROR;
        }
        if (snapshot_offset + info.size > snapshot_size ||
            !wr_bytes(&writer, values + snapshot_offset, info.size))
            return SRN_LINK_STATUS_INTERNAL_ERROR;
        snapshot_offset += info.size;
    }
    *response_len = writer.len;
    return SRN_LINK_STATUS_OK;
}

static uint16_t handle_write(void *user,
                             const srn_cobs_prot_view_t *request,
                             uint8_t *response,
                             size_t response_cap,
                             size_t *response_len,
                             uint8_t *response_flags)
{
    (void)user;
    *response_flags = 0u;
    if (request->payload_len < 5u) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(request->payload, request->payload_len);
    uint32_t layout_hash = 0u;
    if (!SRNOS_Endian_LoadLE32(payload, 0u, &layout_hash)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const size_t count = request->payload[4];
    if ((layout_hash != params_layout_hash()) || (count == 0u) ||
        (count > PARAMS_MAX_COUNT)) {
        return (layout_hash != params_layout_hash()) ?
            SRN_LINK_STATUS_CONFLICT : SRN_LINK_STATUS_BAD_LENGTH;
    }

    params_binary_write_t writes[PARAMS_MAX_COUNT];
    size_t offset = 5u;
    for (size_t item = 0u; item < count; ++item) {
        if ((offset + 4u) > request->payload_len) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        uint16_t index = 0u;
        if (!SRNOS_Endian_LoadLE16(payload, offset, &index)) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        const uint8_t type = request->payload[offset + 2u];
        const uint8_t value_len = request->payload[offset + 3u];
        offset += 4u;
        if ((offset + value_len) > request->payload_len) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        params_entry_info_t info;
        params_status_t status = params_get_by_index(index, &info);
        if (status != PARAMS_STATUS_OK) {
            return map_params_status(status);
        }
        if ((type != (uint8_t)info.type) || (value_len != info.size)) {
            return SRN_LINK_STATUS_TYPE_MISMATCH;
        }
        writes[item].index = index;
        writes[item].data = &request->payload[offset];
        writes[item].len = value_len;
        offset += value_len;
    }
    if (offset != request->payload_len) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }

    size_t failed_item = count;
    const params_status_t status = params_set_binary_batch(writes, count, &failed_item);
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, params_layout_hash()) ||
        !wr_u8(&writer, (uint8_t)count) ||
        !wr_u8(&writer, (failed_item < count) ? (uint8_t)failed_item : 0xFFu) ||
        !wr_u8(&writer, (uint8_t)status) || !wr_u8(&writer, 0u)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    *response_len = writer.len;
    return map_params_status(status);
}

static uint16_t handle_storage(void *user,
                               const srn_cobs_prot_view_t *request,
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
    if (s_config.disable_storage) return SRN_LINK_STATUS_UNSUPPORTED_MESSAGE;
    if (request->payload_len != 0u) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    if ((s_config.defer == NULL) ||
        !s_config.defer(s_config.user, request->header.message,
                        request->header.sequence)) {
        return SRN_LINK_STATUS_BUSY;
    }
    return SRN_LINK_STATUS_DEFERRED_INTERNAL;
}

srn_link_result_t TSKPTT_ParamService_Register(
    srn_link_t *link,
    const tskptt_param_service_config_t *config)
{
    if (link == NULL) {
        return SRN_LINK_ERR_PARAM;
    }
    memset(&s_config, 0, sizeof(s_config));
    if (config != NULL) {
        s_config = *config;
    }
    const uint8_t request_mask = SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST);
    if (srn_link_register_handler(link, SRN_LINK_SERVICE_CONFIG,
                                  TSKPTT_PARAM_SERVICE_SCHEMA, request_mask,
                                  SRN_LINK_QOS_RELIABLE, handle_schema, NULL) != SRN_LINK_OK ||
        srn_link_register_handler(link, SRN_LINK_SERVICE_CONFIG,
                                  TSKPTT_PARAM_SERVICE_READ, request_mask,
                                  SRN_LINK_QOS_RELIABLE, handle_read, NULL) != SRN_LINK_OK ||
        srn_link_register_handler(link, SRN_LINK_SERVICE_CONFIG,
                                  TSKPTT_PARAM_SERVICE_WRITE, request_mask,
                                  SRN_LINK_QOS_RELIABLE, handle_write, NULL) != SRN_LINK_OK ||
        srn_link_register_handler(link, SRN_LINK_SERVICE_CONFIG,
                                  TSKPTT_PARAM_SERVICE_SAVE, request_mask,
                                  SRN_LINK_QOS_RELIABLE, handle_storage, NULL) != SRN_LINK_OK ||
        srn_link_register_handler(link, SRN_LINK_SERVICE_CONFIG,
                                  TSKPTT_PARAM_SERVICE_LOAD, request_mask,
                                  SRN_LINK_QOS_RELIABLE, handle_storage, NULL) != SRN_LINK_OK) {
        return SRN_LINK_ERR_FULL;
    }
    return SRN_LINK_OK;
}

uint16_t TSKPTT_ParamService_RunStorage(uint8_t message,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len)
{
    if ((response == NULL) || (response_len == NULL) || (response_cap < 7u)) {
        return SRN_LINK_STATUS_INVALID_ARGUMENT;
    }
    params_status_t status;
    if (message == TSKPTT_PARAM_SERVICE_SAVE) {
        status = params_save();
    } else if (message == TSKPTT_PARAM_SERVICE_LOAD) {
        status = params_load();
    } else {
        return SRN_LINK_STATUS_UNSUPPORTED_MESSAGE;
    }
    writer_t writer = { response, response_cap, 0u };
    if (!wr_u32(&writer, params_layout_hash()) ||
        !wr_u16(&writer, (uint16_t)params_count()) ||
        !wr_u8(&writer, (uint8_t)status)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    *response_len = writer.len;
    return map_params_status(status);
}
