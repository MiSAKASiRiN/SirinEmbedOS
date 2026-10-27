#ifndef SRNOS_SIRIN_LINK_PROTOCOL_H
#define SRNOS_SIRIN_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "SirinEmbedOS/Core/C/Endian.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_PROTOCOL_MAGIC             0xA5u
#define SRNOS_PROTOCOL_VERSION           0x02u
#define SRNOS_PROTOCOL_FLAG_MORE         0x01u
#define SRNOS_PROTOCOL_VALID_FLAGS       SRNOS_PROTOCOL_FLAG_MORE
#define SRNOS_PROTOCOL_HDR_LEN           12u
#define SRNOS_PROTOCOL_CRC_LEN           4u
#define SRNOS_PROTOCOL_MAX_PAYLOAD       480u
#define SRNOS_PROTOCOL_MAX_RAW_LEN       496u
#define SRNOS_PROTOCOL_MAX_COBS_LEN      498u
#define SRNOS_PROTOCOL_MAX_FRAME_LEN     500u
#define SRNOS_PROTOCOL_FRAME_SLOT_SIZE   512u

typedef enum {
    SRNOS_PROTOCOL_KIND_REQUEST = 0u,
    SRNOS_PROTOCOL_KIND_RESPONSE = 1u,
    SRNOS_PROTOCOL_KIND_EVENT = 2u,
} SRNOS_ProtocolKind;

typedef enum {
    SRNOS_PROTOCOL_OK = 0,
    SRNOS_PROTOCOL_ERR_NULL = -1,
    SRNOS_PROTOCOL_ERR_PARAM = -2,
    SRNOS_PROTOCOL_ERR_PACK = -3,
    SRNOS_PROTOCOL_ERR_CRC = -4,
    SRNOS_PROTOCOL_ERR_MAGIC = -5,
    SRNOS_PROTOCOL_ERR_VERSION = -6,
    SRNOS_PROTOCOL_ERR_FLAGS = -7,
    SRNOS_PROTOCOL_ERR_LEN = -8,
    SRNOS_PROTOCOL_ERR_RAW_LEN = -9,
    SRNOS_PROTOCOL_ERR_KIND = -10,
} SRNOS_ProtocolStatus;

typedef struct {
    uint8_t magic;
    uint8_t version;
    uint8_t kind;
    uint8_t flags;
    uint8_t service;
    uint8_t message;
    uint16_t payload_len;
    uint32_t sequence;
} SRNOS_ProtocolHdr;

typedef struct {
    SRNOS_ProtocolHdr header;
    const uint8_t *payload;
    size_t payload_len;
} SRNOS_ProtocolView;

uint32_t SRNOS_ProtocolCRC32(const uint8_t *data, size_t len);

SRNOS_ProtocolStatus SRNOS_ProtocolParse(const uint8_t *raw,
                                           size_t raw_len,
                                           SRNOS_ProtocolView *out);

/* Raw V2 bytes (header + payload + CRC32), for packet transports. */
SRNOS_ProtocolStatus SRNOS_ProtocolPackRaw(uint8_t kind,uint8_t flags,
    uint8_t service,uint8_t message,uint32_t sequence,const uint8_t *payload,
    size_t payload_len,uint8_t *out,size_t out_cap,size_t *out_len);
/* Stream frame: leading zero + COBS(raw V2) + trailing zero. */
SRNOS_ProtocolStatus SRNOS_ProtocolPack(uint8_t kind,
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

#endif /* SRNOS_SIRIN_LINK_PROTOCOL_H */
