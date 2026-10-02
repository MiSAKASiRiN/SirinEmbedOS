#include "SirinEmbedOS/Services/HostLink/Codec/Protocol.h"

#include "SirinEmbedOS/Services/HostLink/Codec/COBS.h"

#include "SirinEmbedOS/Core/C/CRC32.h"

uint32_t SRNOS_ProtocolCRC32(const uint8_t *data, size_t len)
{
    return SRNOS_CRC32(data, len);
}

SRNOS_ProtocolStatus SRNOS_ProtocolParse(const uint8_t *raw,
                                           size_t raw_len,
                                           SRNOS_ProtocolView *out)
{
    if ((raw == NULL) || (out == NULL)) {
        return SRNOS_PROTOCOL_ERR_NULL;
    }
    if (raw_len < (SRNOS_PROTOCOL_HDR_LEN + SRNOS_PROTOCOL_CRC_LEN)) {
        return SRNOS_PROTOCOL_ERR_RAW_LEN;
    }
    if (raw[0] != SRNOS_PROTOCOL_MAGIC) {
        return SRNOS_PROTOCOL_ERR_MAGIC;
    }
    if (raw[1] != SRNOS_PROTOCOL_VERSION) {
        return SRNOS_PROTOCOL_ERR_VERSION;
    }
    if (raw[2] > (uint8_t)SRNOS_PROTOCOL_KIND_EVENT) {
        return SRNOS_PROTOCOL_ERR_KIND;
    }
    if ((raw[3] & (uint8_t)~SRNOS_PROTOCOL_VALID_FLAGS) != 0u) {
        return SRNOS_PROTOCOL_ERR_FLAGS;
    }

    const SRNOS_Memory_ByteSpan raw_span = SRNOS_Memory_Bytes(raw, raw_len);
    uint16_t payload_len = 0u;
    if (!SRNOS_Endian_LoadLE16(raw_span, 6u, &payload_len)) {
        return SRNOS_PROTOCOL_ERR_RAW_LEN;
    }
    if (payload_len > SRNOS_PROTOCOL_MAX_PAYLOAD) {
        return SRNOS_PROTOCOL_ERR_LEN;
    }
    const size_t expected = SRNOS_PROTOCOL_HDR_LEN +
                            (size_t)payload_len +
                            SRNOS_PROTOCOL_CRC_LEN;
    if (raw_len != expected) {
        return SRNOS_PROTOCOL_ERR_RAW_LEN;
    }

    uint32_t expected_crc = 0u;
    if (!SRNOS_Endian_LoadLE32(raw_span,
                            SRNOS_PROTOCOL_HDR_LEN + payload_len,
                            &expected_crc)) {
        return SRNOS_PROTOCOL_ERR_RAW_LEN;
    }
    const uint32_t actual_crc = SRNOS_ProtocolCRC32(
        raw, SRNOS_PROTOCOL_HDR_LEN + payload_len);
    if (expected_crc != actual_crc) {
        return SRNOS_PROTOCOL_ERR_CRC;
    }

    out->header.magic = raw[0];
    out->header.version = raw[1];
    out->header.kind = raw[2];
    out->header.flags = raw[3];
    out->header.service = raw[4];
    out->header.message = raw[5];
    out->header.payload_len = payload_len;
    if (!SRNOS_Endian_LoadLE32(raw_span, 8u, &out->header.sequence)) {
        return SRNOS_PROTOCOL_ERR_RAW_LEN;
    }
    out->payload = &raw[SRNOS_PROTOCOL_HDR_LEN];
    out->payload_len = payload_len;
    return SRNOS_PROTOCOL_OK;
}

typedef struct {
    uint8_t *out;
    size_t cap;
    size_t write;
    size_t code_index;
    uint8_t code;
} cobs_writer_t;

static int writer_init(cobs_writer_t *writer, uint8_t *out, size_t cap)
{
    if ((writer == NULL) || (out == NULL) || (cap < 3u)) {
        return 0;
    }
    out[0] = SRNOS_COBS_DELIM;
    writer->out = out;
    writer->cap = cap;
    writer->write = 2u;
    writer->code_index = 1u;
    writer->code = 1u;
    out[1] = 0u;
    return 1;
}

static int writer_byte(cobs_writer_t *writer, uint8_t value)
{
    if (value == 0u) {
        writer->out[writer->code_index] = writer->code;
        if (writer->write >= writer->cap) {
            return 0;
        }
        writer->code_index = writer->write++;
        writer->out[writer->code_index] = 0u;
        writer->code = 1u;
        return 1;
    }

    if (writer->write >= writer->cap) {
        return 0;
    }
    writer->out[writer->write++] = value;
    writer->code++;
    if (writer->code == 0xFFu) {
        writer->out[writer->code_index] = writer->code;
        if (writer->write >= writer->cap) {
            return 0;
        }
        writer->code_index = writer->write++;
        writer->out[writer->code_index] = 0u;
        writer->code = 1u;
    }
    return 1;
}

static int writer_finish(cobs_writer_t *writer, size_t *out_len)
{
    if ((writer == NULL) || (out_len == NULL) ||
        (writer->write >= writer->cap)) {
        return 0;
    }
    writer->out[writer->code_index] = writer->code;
    writer->out[writer->write++] = SRNOS_COBS_DELIM;
    *out_len = writer->write;
    return 1;
}

SRNOS_ProtocolStatus SRNOS_ProtocolPack(uint8_t kind,
                                          uint8_t flags,
                                          uint8_t service,
                                          uint8_t message,
                                          uint32_t sequence,
                                          const uint8_t *payload,
                                          size_t payload_len,
                                          uint8_t *out,
                                          size_t out_cap,
                                          size_t *out_len)
{
    if ((out == NULL) || (out_len == NULL) ||
        ((payload == NULL) && (payload_len != 0u))) {
        return SRNOS_PROTOCOL_ERR_NULL;
    }
    if ((kind > (uint8_t)SRNOS_PROTOCOL_KIND_EVENT) ||
        (payload_len > SRNOS_PROTOCOL_MAX_PAYLOAD) ||
        (out_cap < SRNOS_PROTOCOL_MAX_FRAME_LEN)) {
        return SRNOS_PROTOCOL_ERR_PARAM;
    }
    if ((flags & (uint8_t)~SRNOS_PROTOCOL_VALID_FLAGS) != 0u) {
        return SRNOS_PROTOCOL_ERR_FLAGS;
    }

    uint8_t header[SRNOS_PROTOCOL_HDR_LEN];
    header[0] = SRNOS_PROTOCOL_MAGIC;
    header[1] = SRNOS_PROTOCOL_VERSION;
    header[2] = kind;
    header[3] = flags;
    header[4] = service;
    header[5] = message;
    SRNOS_Memory_MutableByteSpan header_span =
        SRNOS_Memory_MutableBytes(header, sizeof(header));
    (void)SRNOS_Endian_StoreLE16(header_span, 6u, (uint16_t)payload_len);
    (void)SRNOS_Endian_StoreLE32(header_span, 8u, sequence);

    uint32_t crc = SRNOS_CRC32Update(SRNOS_CRC32_INIT, header, sizeof(header));
    crc = SRNOS_CRC32Update(crc, payload, payload_len);
    cobs_writer_t writer;
    if (!writer_init(&writer, out, out_cap)) {
        return SRNOS_PROTOCOL_ERR_PACK;
    }

    for (size_t i = 0u; i < sizeof(header); ++i) {
        if (!writer_byte(&writer, header[i])) {
            return SRNOS_PROTOCOL_ERR_PACK;
        }
    }
    for (size_t i = 0u; i < payload_len; ++i) {
        if (!writer_byte(&writer, payload[i])) {
            return SRNOS_PROTOCOL_ERR_PACK;
        }
    }
    crc ^= SRNOS_CRC32_FINAL_XOR;
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        if (!writer_byte(&writer, (uint8_t)(crc >> shift))) {
            return SRNOS_PROTOCOL_ERR_PACK;
        }
    }

    return writer_finish(&writer, out_len) ?
        SRNOS_PROTOCOL_OK : SRNOS_PROTOCOL_ERR_PACK;
}
