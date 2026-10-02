#ifndef SRNOS_HOST_LINK_COBS_H
#define SRNOS_HOST_LINK_COBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_COBS_DELIM
#define SRNOS_COBS_DELIM 0x00u
#endif

typedef enum {
    SRNOS_COBS_OK = 0,
    SRNOS_COBS_ERR_NULL = -1,
    SRNOS_COBS_ERR_NO_SPACE = -2,
    SRNOS_COBS_ERR_DECODE = -3,
    SRNOS_COBS_ERR_EMPTY_FRAME = -4,
    SRNOS_COBS_ERR_FRAME_TOO_LONG = -5,
    SRNOS_COBS_ERR_OUTPUT_TOO_SMALL = -6,
} SRNOS_COBSStatus;

typedef SRNOS_COBSStatus (*SRNOS_COBSOnFrameCb)(void *user,
                                                    const uint8_t *raw,
                                                    size_t raw_len);

typedef struct {
    uint8_t *rx_cobs_buf;
    size_t rx_cobs_cap;
    size_t rx_cobs_len;
    uint8_t dropping;

    uint8_t *rx_raw_buf;
    size_t rx_raw_cap;

    uint32_t stat_frames_ok;
    uint32_t stat_frames_drop_decode;
    uint32_t stat_frames_drop_too_long;
    uint32_t stat_empty_delim;

    SRNOS_COBSOnFrameCb on_frame;
    void *user;
} SRNOS_COBSCtx;

/*
 * Incremental decoder for streams that carry frames as
 *   0x00 | COBS bytes | 0x00
 * and may contain non-zero out-of-band bytes (for example ASCII logs)
 * between frames. The decoder writes directly to raw_buf and therefore does
 * not need a second encoded-frame buffer.
 */
typedef struct {
    uint8_t *raw_buf;
    size_t raw_cap;
    size_t raw_len;
    size_t block_remaining;
    uint8_t in_frame;
    uint8_t need_code;
    uint8_t pending_zero;
    uint8_t dropping;
    uint32_t stat_frames_ok;
    uint32_t stat_decode_error;
    uint32_t stat_too_long;
    uint32_t stat_ignored_bytes;
    uint32_t stat_empty_frames;
    SRNOS_COBSOnFrameCb on_frame;
    void *user;
} SRNOS_COBSStream;

SRNOS_COBSStatus SRNOS_COBSInit(SRNOS_COBSCtx *ctx,
                                uint8_t *rx_cobs_buf,
                                size_t rx_cobs_cap,
                                uint8_t *rx_raw_buf,
                                size_t rx_raw_cap,
                                SRNOS_COBSOnFrameCb on_frame,
                                void *user);

void SRNOS_COBSResetRx(SRNOS_COBSCtx *ctx);

SRNOS_COBSStatus SRNOS_COBSFeedBytes(SRNOS_COBSCtx *ctx,
                                      const uint8_t *data,
                                      size_t len);

SRNOS_COBSStatus SRNOS_COBSPack(const uint8_t *raw,
                                size_t raw_len,
                                uint8_t *out,
                                size_t out_cap,
                                size_t *out_len);

SRNOS_COBSStatus SRNOS_COBSEncode(const uint8_t *raw,
                                  size_t raw_len,
                                  uint8_t *cobs,
                                  size_t cobs_cap,
                                  size_t *cobs_len);

SRNOS_COBSStatus SRNOS_COBSDecode(const uint8_t *cobs,
                                  size_t cobs_len,
                                  uint8_t *raw,
                                  size_t raw_cap,
                                  size_t *raw_len);

SRNOS_COBSStatus SRNOS_COBSStreamInit(SRNOS_COBSStream *stream,
                                       uint8_t *raw_buf,
                                       size_t raw_cap,
                                       SRNOS_COBSOnFrameCb on_frame,
                                       void *user);

void SRNOS_COBSStreamReset(SRNOS_COBSStream *stream);

SRNOS_COBSStatus SRNOS_COBSStreamFeed(SRNOS_COBSStream *stream,
                                       const uint8_t *data,
                                       size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_HOST_LINK_COBS_H */
