#ifndef OMX_LIB_SRN_COBS_H
#define OMX_LIB_SRN_COBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRN_COBS_DELIM
#define SRN_COBS_DELIM 0x00u
#endif

typedef enum {
    SRN_COBS_OK = 0,
    SRN_COBS_ERR_NULL = -1,
    SRN_COBS_ERR_NO_SPACE = -2,
    SRN_COBS_ERR_DECODE = -3,
    SRN_COBS_ERR_EMPTY_FRAME = -4,
    SRN_COBS_ERR_FRAME_TOO_LONG = -5,
    SRN_COBS_ERR_OUTPUT_TOO_SMALL = -6,
} srn_cobs_status_t;

typedef srn_cobs_status_t (*srn_cobs_on_frame_cb_t)(void *user,
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

    srn_cobs_on_frame_cb_t on_frame;
    void *user;
} srn_cobs_ctx_t;

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
    srn_cobs_on_frame_cb_t on_frame;
    void *user;
} srn_cobs_stream_t;

srn_cobs_status_t srn_cobs_init(srn_cobs_ctx_t *ctx,
                                uint8_t *rx_cobs_buf,
                                size_t rx_cobs_cap,
                                uint8_t *rx_raw_buf,
                                size_t rx_raw_cap,
                                srn_cobs_on_frame_cb_t on_frame,
                                void *user);

void srn_cobs_reset_rx(srn_cobs_ctx_t *ctx);

srn_cobs_status_t srn_cobs_feed_bytes(srn_cobs_ctx_t *ctx,
                                      const uint8_t *data,
                                      size_t len);

srn_cobs_status_t srn_cobs_pack(const uint8_t *raw,
                                size_t raw_len,
                                uint8_t *out,
                                size_t out_cap,
                                size_t *out_len);

srn_cobs_status_t srn_cobs_encode(const uint8_t *raw,
                                  size_t raw_len,
                                  uint8_t *cobs,
                                  size_t cobs_cap,
                                  size_t *cobs_len);

srn_cobs_status_t srn_cobs_decode(const uint8_t *cobs,
                                  size_t cobs_len,
                                  uint8_t *raw,
                                  size_t raw_cap,
                                  size_t *raw_len);

srn_cobs_status_t srn_cobs_stream_init(srn_cobs_stream_t *stream,
                                       uint8_t *raw_buf,
                                       size_t raw_cap,
                                       srn_cobs_on_frame_cb_t on_frame,
                                       void *user);

void srn_cobs_stream_reset(srn_cobs_stream_t *stream);

srn_cobs_status_t srn_cobs_stream_feed(srn_cobs_stream_t *stream,
                                       const uint8_t *data,
                                       size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OMX_LIB_SRN_COBS_H */
