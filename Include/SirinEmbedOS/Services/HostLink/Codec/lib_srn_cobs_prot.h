#ifndef OMX_LIB_SRN_COBS_PROT_H
#define OMX_LIB_SRN_COBS_PROT_H

#include <stddef.h>
#include <stdint.h>
#include "SirinEmbedOS/Core/C/Endian.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRN_COBS_PROT_MAGIC             0xA5u
#define SRN_COBS_PROT_VERSION           0x02u
#define SRN_COBS_PROT_FLAG_MORE         0x01u
#define SRN_COBS_PROT_VALID_FLAGS       SRN_COBS_PROT_FLAG_MORE
#define SRN_COBS_PROT_HDR_LEN           12u
#define SRN_COBS_PROT_CRC_LEN           4u
#define SRN_COBS_PROT_MAX_PAYLOAD       480u
#define SRN_COBS_PROT_MAX_RAW_LEN       496u
#define SRN_COBS_PROT_MAX_COBS_LEN      498u
#define SRN_COBS_PROT_MAX_FRAME_LEN     500u
#define SRN_COBS_PROT_FRAME_SLOT_SIZE   512u

typedef enum {
    SRN_COBS_PROT_KIND_REQUEST = 0u,
    SRN_COBS_PROT_KIND_RESPONSE = 1u,
    SRN_COBS_PROT_KIND_EVENT = 2u,
} srn_cobs_prot_kind_t;

typedef enum {
    SRN_COBS_PROT_OK = 0,
    SRN_COBS_PROT_ERR_NULL = -1,
    SRN_COBS_PROT_ERR_PARAM = -2,
    SRN_COBS_PROT_ERR_PACK = -3,
    SRN_COBS_PROT_ERR_CRC = -4,
    SRN_COBS_PROT_ERR_MAGIC = -5,
    SRN_COBS_PROT_ERR_VERSION = -6,
    SRN_COBS_PROT_ERR_FLAGS = -7,
    SRN_COBS_PROT_ERR_LEN = -8,
    SRN_COBS_PROT_ERR_RAW_LEN = -9,
    SRN_COBS_PROT_ERR_KIND = -10,
} srn_cobs_prot_status_t;

typedef struct {
    uint8_t magic;
    uint8_t version;
    uint8_t kind;
    uint8_t flags;
    uint8_t service;
    uint8_t message;
    uint16_t payload_len;
    uint32_t sequence;
} srn_cobs_prot_hdr_t;

typedef struct {
    srn_cobs_prot_hdr_t header;
    const uint8_t *payload;
    size_t payload_len;
} srn_cobs_prot_view_t;

uint32_t srn_cobs_prot_crc32(const uint8_t *data, size_t len);

srn_cobs_prot_status_t srn_cobs_prot_parse(const uint8_t *raw,
                                           size_t raw_len,
                                           srn_cobs_prot_view_t *out);

srn_cobs_prot_status_t srn_cobs_prot_pack(uint8_t kind,
                                          uint8_t flags,
                                          uint8_t service,
                                          uint8_t message,
                                          uint32_t sequence,
                                          const uint8_t *payload,
                                          size_t payload_len,
                                          uint8_t *out,
                                          size_t out_cap,
                                          size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* OMX_LIB_SRN_COBS_PROT_H */
