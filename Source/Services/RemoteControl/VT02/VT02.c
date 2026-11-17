#include "SirinEmbedOS/Services/RemoteControl/VT02/VT02.h"
#include "../remote_internal.h"
#include "../../VideoLink/codec.h"
#include <string.h>

bool SRNOS_VT02Decode(const uint8_t *p, size_t length, SRNOS_VT02Dec *out)
{
    if (!p || !out || length != SRNOS_VT02_KEYBOARD_MOUSE_LENGTH ||
        p[6] > 1 || p[7] > 1) return false;
    SRNOS_VT02Dec d = {
        .mouse_x = (int16_t)SRNOS_VideoLinkU16(p),
        .mouse_y = (int16_t)SRNOS_VideoLinkU16(p + 2),
        .mouse_z = (int16_t)SRNOS_VideoLinkU16(p + 4),
        .keyboard = SRNOS_VideoLinkU16(p + 8),
        .reserved = SRNOS_VideoLinkU16(p + 10),
        .left = p[6] != 0, .right = p[7] != 0
    };
    *out = d;
    return true;
}

void SRNOS_VT02FramerInit(SRNOS_VT02Framer *f)
{
    if (f) memset(f, 0, sizeof(*f));
}

static void consume(SRNOS_VT02Framer *f, uint16_t size, bool discarded)
{
    if (discarded) f->discarded_bytes += size;
    f->length = (uint16_t)(f->length - size);
    memmove(f->buffer, f->buffer + size, f->length);
}

size_t SRNOS_VT02FramerFeed(SRNOS_VT02Framer *f, const uint8_t *data,
                           size_t size, SRNOS_VT02Dec *latest)
{
    if (!f || (!data && size)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < size; ++i) {
        f->buffer[f->length++] = data[i];
        while (f->length) {
            if (f->buffer[0] != 0xa5) { consume(f, 1, true); continue; }
            if (f->length < 5) break;
            const uint16_t n = SRNOS_VideoLinkU16(f->buffer + 1);
            if (n > SRNOS_VT02_MAX_PAYLOAD ||
                SRNOS_VideoLinkCRC(f->buffer, 4, 0xff, 0x8c) != f->buffer[4]) {
                ++f->header_errors;
                consume(f, 1, true);
                continue;
            }
            const uint16_t total = (uint16_t)(n + 9u);
            if (f->length < total) break;
            if (SRNOS_VideoLinkCRC(f->buffer, total - 2u, 0xffff, 0x8408) !=
                SRNOS_VideoLinkU16(f->buffer + total - 2u)) {
                ++f->crc_errors;
                consume(f, total, true);
                continue;
            }
            const uint16_t cmd = SRNOS_VideoLinkU16(f->buffer + 5);
            const uint8_t *payload = f->buffer + 7;
            if (cmd == SRNOS_VT02_CMD_KEYBOARD_MOUSE) {
                SRNOS_VT02Dec decoded;
                if (SRNOS_VT02Decode(payload, n, &decoded)) {
                    if (latest) *latest = decoded;
                    if (f->frame_callback) f->frame_callback(&decoded, f->user);
                    ++f->frames;
                    ++count;
                } else ++f->invalid_payloads;
            } else if (cmd == SRNOS_VT02_CMD_CUSTOM_CONTROLLER) {
                if (n == SRNOS_VT02_CUSTOM_LENGTH) {
                    if (f->custom_callback)
                        f->custom_callback(payload, f->buffer[3], f->user);
                    ++f->custom_frames;
                } else ++f->invalid_payloads;
            } else ++f->ignored_frames;
            consume(f, total, false);
        }
    }
    return count;
}

bool SRNOS_VT02SerialConfigValid(const SRNOS_SerialConfig *config)
{
    return config && config->mode == SRNOS_SERIAL_DUPLEX && config->rx_enabled && !config->rx_inverted &&
        config->baud_rate == 115200 && config->data_bits == 8 && config->stop_bits == 1 &&
        config->parity == SRNOS_SERIAL_PARITY_NONE;
}

SRNOS_SerialStatus SRNOS_VT02Init(SRNOS_VT02 *self, SRNOS_SerialChannel *channel)
{
    if (!self || !channel) return SRNOS_SERIAL_ARGUMENT;
    if (self->channel) return SRNOS_SERIAL_STATE;
    const SRNOS_SerialConfig *c = SRNOS_SerialGetConfig(channel);
    if (!SRNOS_VT02SerialConfigValid(c)) return SRNOS_SERIAL_ARGUMENT;
    SRNOS_SerialStatus result = SRNOS_SerialClaim(channel, self);
    if (result != SRNOS_SERIAL_OK) return result;
    memset(self, 0, sizeof(*self));
    self->channel = channel;
    return SRNOS_SERIAL_OK;
}

static void reset_partial(SRNOS_VT02 *self)
{
    self->framer.discarded_bytes += self->framer.length;
    self->framer.length = 0;
    ++self->diag.stream_resets;
}

static bool refresh_stats(SRNOS_VT02 *self)
{
    SRNOS_SerialStats stats;
    if (SRNOS_SerialGetStats(self->channel, &stats) != SRNOS_SERIAL_OK) return false;
    const bool continuous = stats.rx_overflow == self->diag.serial.rx_overflow &&
        stats.hardware_errors == self->diag.serial.hardware_errors &&
        stats.recoveries == self->diag.serial.recoveries;
    self->diag.serial = stats;
    return continuous;
}

size_t SRNOS_VT02Poll(SRNOS_VT02 *self, SRNOS_VT02Dec *latest, uint32_t now_ms)
{
    if (!self || !self->channel) return 0;
    if (!refresh_stats(self)) {
        reset_partial(self);
        (void)SRNOS_SerialDiscardRx(self->channel);
        return 0;
    }
    if (self->framer.length && now_ms - self->last_byte_ms > 30u)
        reset_partial(self);
    uint8_t bytes[128];
    size_t count = 0;
    for (unsigned batch = 0; batch < 8; ++batch) {
        size_t received = 0;
        SRNOS_SerialStatus status = SRNOS_SerialRead(self->channel, bytes,
            sizeof(bytes), &received, SRNOS_NO_WAIT);
        if (!refresh_stats(self) ||
            (status != SRNOS_SERIAL_OK && status != SRNOS_SERIAL_TIMEOUT)) {
            reset_partial(self);
            (void)SRNOS_SerialDiscardRx(self->channel);
            return 0;
        }
        if (status != SRNOS_SERIAL_OK || !received) break;
        self->last_byte_ms = now_ms;
        count += SRNOS_VT02FramerFeed(&self->framer, bytes, received, latest);
    }
    if (count) {
        self->diag.last_frame_ms = now_ms;
        self->diag.seen_frame = true;
    }
    return count;
}
