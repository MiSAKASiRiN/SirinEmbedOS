#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs_prot.h"

#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs.h"

static const uint32_t k_crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

static uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    crc = (crc >> 4u) ^ k_crc32_nibble[crc & 0x0Fu];
    crc = (crc >> 4u) ^ k_crc32_nibble[crc & 0x0Fu];
    return crc;
}

uint32_t srn_cobs_prot_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    if ((data == NULL) && (len != 0u)) {
        return 0u;
    }
    for (size_t i = 0u; i < len; ++i) {
        crc = crc32_update(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFFu;
}

srn_cobs_prot_status_t srn_cobs_prot_parse(const uint8_t *raw,
                                           size_t raw_len,
                                           srn_cobs_prot_view_t *out)
{
    if ((raw == NULL) || (out == NULL)) {
        return SRN_COBS_PROT_ERR_NULL;
    }
    if (raw_len < (SRN_COBS_PROT_HDR_LEN + SRN_COBS_PROT_CRC_LEN)) {
        return SRN_COBS_PROT_ERR_RAW_LEN;
    }
    if (raw[0] != SRN_COBS_PROT_MAGIC) {
        return SRN_COBS_PROT_ERR_MAGIC;
    }
    if (raw[1] != SRN_COBS_PROT_VERSION) {
        return SRN_COBS_PROT_ERR_VERSION;
    }
    if (raw[2] > (uint8_t)SRN_COBS_PROT_KIND_EVENT) {
        return SRN_COBS_PROT_ERR_KIND;
    }
    if ((raw[3] & (uint8_t)~SRN_COBS_PROT_VALID_FLAGS) != 0u) {
        return SRN_COBS_PROT_ERR_FLAGS;
    }

    const SRNOS_Memory_ByteSpan raw_span = SRNOS_Memory_Bytes(raw, raw_len);
    uint16_t payload_len = 0u;
    if (!SRNOS_Endian_LoadLE16(raw_span, 6u, &payload_len)) {
        return SRN_COBS_PROT_ERR_RAW_LEN;
    }
    if (payload_len > SRN_COBS_PROT_MAX_PAYLOAD) {
        return SRN_COBS_PROT_ERR_LEN;
    }
    const size_t expected = SRN_COBS_PROT_HDR_LEN +
                            (size_t)payload_len +
                            SRN_COBS_PROT_CRC_LEN;
    if (raw_len != expected) {
        return SRN_COBS_PROT_ERR_RAW_LEN;
    }

    uint32_t expected_crc = 0u;
    if (!SRNOS_Endian_LoadLE32(raw_span,
                            SRN_COBS_PROT_HDR_LEN + payload_len,
                            &expected_crc)) {
        return SRN_COBS_PROT_ERR_RAW_LEN;
    }
    const uint32_t actual_crc = srn_cobs_prot_crc32(
        raw, SRN_COBS_PROT_HDR_LEN + payload_len);
    if (expected_crc != actual_crc) {
        return SRN_COBS_PROT_ERR_CRC;
    }

    out->header.magic = raw[0];
    out->header.version = raw[1];
    out->header.kind = raw[2];
    out->header.flags = raw[3];
    out->header.service = raw[4];
    out->header.message = raw[5];
    out->header.payload_len = payload_len;
    if (!SRNOS_Endian_LoadLE32(raw_span, 8u, &out->header.sequence)) {
        return SRN_COBS_PROT_ERR_RAW_LEN;
    }
    out->payload = &raw[SRN_COBS_PROT_HDR_LEN];
    out->payload_len = payload_len;
    return SRN_COBS_PROT_OK;
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
    out[0] = SRN_COBS_DELIM;
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
    writer->out[writer->write++] = SRN_COBS_DELIM;
    *out_len = writer->write;
    return 1;
}

srn_cobs_prot_status_t srn_cobs_prot_pack(uint8_t kind,
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
        return SRN_COBS_PROT_ERR_NULL;
    }
    if ((kind > (uint8_t)SRN_COBS_PROT_KIND_EVENT) ||
        (payload_len > SRN_COBS_PROT_MAX_PAYLOAD) ||
        (out_cap < SRN_COBS_PROT_MAX_FRAME_LEN)) {
        return SRN_COBS_PROT_ERR_PARAM;
    }
    if ((flags & (uint8_t)~SRN_COBS_PROT_VALID_FLAGS) != 0u) {
        return SRN_COBS_PROT_ERR_FLAGS;
    }

    uint8_t header[SRN_COBS_PROT_HDR_LEN];
    header[0] = SRN_COBS_PROT_MAGIC;
    header[1] = SRN_COBS_PROT_VERSION;
    header[2] = kind;
    header[3] = flags;
    header[4] = service;
    header[5] = message;
    SRNOS_Memory_MutableByteSpan header_span =
        SRNOS_Memory_MutableBytes(header, sizeof(header));
    (void)SRNOS_Endian_StoreLE16(header_span, 6u, (uint16_t)payload_len);
    (void)SRNOS_Endian_StoreLE32(header_span, 8u, sequence);

    uint32_t crc = 0xFFFFFFFFu;
    cobs_writer_t writer;
    if (!writer_init(&writer, out, out_cap)) {
        return SRN_COBS_PROT_ERR_PACK;
    }

    for (size_t i = 0u; i < sizeof(header); ++i) {
        crc = crc32_update(crc, header[i]);
        if (!writer_byte(&writer, header[i])) {
            return SRN_COBS_PROT_ERR_PACK;
        }
    }
    for (size_t i = 0u; i < payload_len; ++i) {
        crc = crc32_update(crc, payload[i]);
        if (!writer_byte(&writer, payload[i])) {
            return SRN_COBS_PROT_ERR_PACK;
        }
    }
    crc ^= 0xFFFFFFFFu;
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        if (!writer_byte(&writer, (uint8_t)(crc >> shift))) {
            return SRN_COBS_PROT_ERR_PACK;
        }
    }

    return writer_finish(&writer, out_len) ?
        SRN_COBS_PROT_OK : SRN_COBS_PROT_ERR_PACK;
}
