#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs.h"

#include <stdbool.h>

static srn_cobs_status_t srn_cobs_check_ctx(const srn_cobs_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->rx_cobs_buf == NULL) ||
        (ctx->rx_raw_buf == NULL) || (ctx->on_frame == NULL)) {
        return SRN_COBS_ERR_NULL;
    }
    if ((ctx->rx_cobs_cap == 0u) || (ctx->rx_raw_cap == 0u)) {
        return SRN_COBS_ERR_NULL;
    }
    return SRN_COBS_OK;
}

srn_cobs_status_t srn_cobs_init(srn_cobs_ctx_t *ctx,
                                uint8_t *rx_cobs_buf,
                                size_t rx_cobs_cap,
                                uint8_t *rx_raw_buf,
                                size_t rx_raw_cap,
                                srn_cobs_on_frame_cb_t on_frame,
                                void *user)
{
    if ((ctx == NULL) || (rx_cobs_buf == NULL) || (rx_raw_buf == NULL) ||
        (on_frame == NULL)) {
        return SRN_COBS_ERR_NULL;
    }

    ctx->rx_cobs_buf = rx_cobs_buf;
    ctx->rx_cobs_cap = rx_cobs_cap;
    ctx->rx_cobs_len = 0u;
    ctx->dropping = 0u;
    ctx->rx_raw_buf = rx_raw_buf;
    ctx->rx_raw_cap = rx_raw_cap;
    ctx->stat_frames_ok = 0u;
    ctx->stat_frames_drop_decode = 0u;
    ctx->stat_frames_drop_too_long = 0u;
    ctx->stat_empty_delim = 0u;
    ctx->on_frame = on_frame;
    ctx->user = user;
    return SRN_COBS_OK;
}

void srn_cobs_reset_rx(srn_cobs_ctx_t *ctx)
{
    if (ctx != NULL) {
        ctx->rx_cobs_len = 0u;
        ctx->dropping = 0u;
    }
}

srn_cobs_status_t srn_cobs_encode(const uint8_t *raw,
                                  size_t raw_len,
                                  uint8_t *cobs,
                                  size_t cobs_cap,
                                  size_t *cobs_len)
{
    if (((raw == NULL) && (raw_len != 0u)) || (cobs == NULL) || (cobs_len == NULL)) {
        return SRN_COBS_ERR_NULL;
    }
    if (cobs_cap == 0u) {
        return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
    }

    size_t read_index = 0u;
    size_t write_index = 0u;
    size_t code_index = 0u;
    uint8_t code = 1u;

    cobs[write_index++] = 0u;

    while (read_index < raw_len) {
        const uint8_t b = raw[read_index++];
        if (b == 0u) {
            cobs[code_index] = code;
            code = 1u;
            code_index = write_index;
            if (write_index >= cobs_cap) {
                return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
            }
            cobs[write_index++] = 0u;
        } else {
            if (write_index >= cobs_cap) {
                return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
            }
            cobs[write_index++] = b;
            code++;
            if (code == 0xFFu) {
                cobs[code_index] = code;
                code = 1u;
                code_index = write_index;
                if (write_index >= cobs_cap) {
                    return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
                }
                cobs[write_index++] = 0u;
            }
        }
    }

    cobs[code_index] = code;
    *cobs_len = write_index;
    return SRN_COBS_OK;
}

srn_cobs_status_t srn_cobs_decode(const uint8_t *cobs,
                                  size_t cobs_len,
                                  uint8_t *raw,
                                  size_t raw_cap,
                                  size_t *raw_len)
{
    if ((cobs == NULL) || (raw == NULL) || (raw_len == NULL)) {
        return SRN_COBS_ERR_NULL;
    }
    if (cobs_len == 0u) {
        return SRN_COBS_ERR_EMPTY_FRAME;
    }

    size_t read_index = 0u;
    size_t write_index = 0u;

    while (read_index < cobs_len) {
        const uint8_t code = cobs[read_index++];
        if (code == 0u) {
            return SRN_COBS_ERR_DECODE;
        }

        const uint8_t copy_len = (uint8_t)(code - 1u);
        if ((size_t)copy_len > (cobs_len - read_index)) {
            return SRN_COBS_ERR_DECODE;
        }
        if ((write_index + (size_t)copy_len) > raw_cap) {
            return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
        }

        for (uint8_t i = 0u; i < copy_len; ++i) {
            raw[write_index++] = cobs[read_index++];
        }

        if ((code != 0xFFu) && (read_index < cobs_len)) {
            if (write_index >= raw_cap) {
                return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
            }
            raw[write_index++] = 0u;
        }
    }

    *raw_len = write_index;
    return SRN_COBS_OK;
}

srn_cobs_status_t srn_cobs_pack(const uint8_t *raw,
                                size_t raw_len,
                                uint8_t *out,
                                size_t out_cap,
                                size_t *out_len)
{
    if (((raw == NULL) && (raw_len != 0u)) || (out == NULL) || (out_len == NULL)) {
        return SRN_COBS_ERR_NULL;
    }
    if (out_cap < 2u) {
        return SRN_COBS_ERR_OUTPUT_TOO_SMALL;
    }

    out[0] = (uint8_t)SRN_COBS_DELIM;

    size_t cobs_len = 0u;
    srn_cobs_status_t st =
        srn_cobs_encode(raw, raw_len, &out[1], out_cap - 2u, &cobs_len);
    if (st != SRN_COBS_OK) {
        return st;
    }

    out[1u + cobs_len] = (uint8_t)SRN_COBS_DELIM;
    *out_len = 2u + cobs_len;
    return SRN_COBS_OK;
}

srn_cobs_status_t srn_cobs_feed_bytes(srn_cobs_ctx_t *ctx,
                                      const uint8_t *data,
                                      size_t len)
{
    srn_cobs_status_t st = srn_cobs_check_ctx(ctx);
    if (st != SRN_COBS_OK) {
        return st;
    }
    if ((data == NULL) && (len != 0u)) {
        return SRN_COBS_ERR_NULL;
    }

    for (size_t i = 0u; i < len; ++i) {
        const uint8_t b = data[i];
        if (b == (uint8_t)SRN_COBS_DELIM) {
            if (ctx->dropping) { ctx->dropping=0; ctx->rx_cobs_len=0; continue; }
            if (ctx->rx_cobs_len == 0u) {
                ctx->stat_empty_delim++;
                continue;
            }

            size_t raw_len = 0u;
            st = srn_cobs_decode(ctx->rx_cobs_buf,
                                 ctx->rx_cobs_len,
                                 ctx->rx_raw_buf,
                                 ctx->rx_raw_cap,
                                 &raw_len);
            ctx->rx_cobs_len = 0u;
            if (st != SRN_COBS_OK) {
                ctx->stat_frames_drop_decode++;
                continue;
            }

            (void)ctx->on_frame(ctx->user, ctx->rx_raw_buf, raw_len);
            ctx->stat_frames_ok++;
        } else {
            if (ctx->dropping) continue;
            if (ctx->rx_cobs_len >= ctx->rx_cobs_cap) {
                ctx->rx_cobs_len = 0u;
                ctx->stat_frames_drop_too_long++;
                ctx->dropping=1;
                continue;
            }
            ctx->rx_cobs_buf[ctx->rx_cobs_len++] = b;
        }
    }

    return SRN_COBS_OK;
}

srn_cobs_status_t srn_cobs_stream_init(srn_cobs_stream_t *stream,
                                       uint8_t *raw_buf,
                                       size_t raw_cap,
                                       srn_cobs_on_frame_cb_t on_frame,
                                       void *user)
{
    if ((stream == NULL) || (raw_buf == NULL) || (raw_cap == 0u) ||
        (on_frame == NULL)) {
        return SRN_COBS_ERR_NULL;
    }

    stream->raw_buf = raw_buf;
    stream->raw_cap = raw_cap;
    stream->on_frame = on_frame;
    stream->user = user;
    stream->stat_frames_ok = 0u;
    stream->stat_decode_error = 0u;
    stream->stat_too_long = 0u;
    stream->stat_ignored_bytes = 0u;
    stream->stat_empty_frames = 0u;
    srn_cobs_stream_reset(stream);
    return SRN_COBS_OK;
}

void srn_cobs_stream_reset(srn_cobs_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    stream->raw_len = 0u;
    stream->block_remaining = 0u;
    stream->in_frame = 0u;
    stream->need_code = 1u;
    stream->pending_zero = 0u;
    stream->dropping = 0u;
}

static void stream_start_frame(srn_cobs_stream_t *stream)
{
    stream->raw_len = 0u;
    stream->block_remaining = 0u;
    stream->in_frame = 1u;
    stream->need_code = 1u;
    stream->pending_zero = 0u;
    stream->dropping = 0u;
}

static bool stream_push_raw(srn_cobs_stream_t *stream, uint8_t value)
{
    if (stream->raw_len >= stream->raw_cap) {
        stream->dropping = 1u;
        return false;
    }
    stream->raw_buf[stream->raw_len++] = value;
    return true;
}

srn_cobs_status_t srn_cobs_stream_feed(srn_cobs_stream_t *stream,
                                       const uint8_t *data,
                                       size_t len)
{
    if ((stream == NULL) || (stream->raw_buf == NULL) ||
        (stream->on_frame == NULL) || ((data == NULL) && (len != 0u))) {
        return SRN_COBS_ERR_NULL;
    }

    for (size_t i = 0u; i < len; ++i) {
        const uint8_t b = data[i];
        if (stream->in_frame == 0u) {
            if (b == (uint8_t)SRN_COBS_DELIM) {
                stream_start_frame(stream);
            } else {
                stream->stat_ignored_bytes++;
            }
            continue;
        }

        if (b == (uint8_t)SRN_COBS_DELIM) {
            if(stream->raw_len==0u && !stream->dropping && stream->need_code) {
                ++stream->stat_empty_frames;
                stream_start_frame(stream); /* repeated leading delimiters */
                continue;
            }
            const bool valid = (stream->dropping == 0u) &&
                               (stream->raw_len != 0u) &&
                               (stream->need_code != 0u) &&
                               (stream->block_remaining == 0u);
            if (valid) {
                (void)stream->on_frame(stream->user,
                                       stream->raw_buf,
                                       stream->raw_len);
                stream->stat_frames_ok++;
            } else if (stream->raw_len == 0u) {
                stream->stat_empty_frames++;
            } else if (stream->dropping != 0u) {
                stream->stat_too_long++;
            } else {
                stream->stat_decode_error++;
            }
            srn_cobs_stream_reset(stream);
            continue;
        }

        if (stream->dropping != 0u) {
            continue;
        }

        if (stream->need_code != 0u) {
            if (stream->pending_zero != 0u) {
                if (!stream_push_raw(stream, 0u)) {
                    continue;
                }
            }
            stream->block_remaining = (size_t)b - 1u;
            stream->pending_zero = (b != 0xFFu) ? 1u : 0u;
            stream->need_code = (stream->block_remaining == 0u) ? 1u : 0u;
            continue;
        }

        if (!stream_push_raw(stream, b)) {
            continue;
        }
        stream->block_remaining--;
        if (stream->block_remaining == 0u) {
            stream->need_code = 1u;
        }
    }

    return SRN_COBS_OK;
}
