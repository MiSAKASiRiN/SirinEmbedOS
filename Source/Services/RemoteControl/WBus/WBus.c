#include "SirinEmbedOS/Services/RemoteControl/WBus/WBus.h"
#include <string.h>

bool SRNOS_WBusDecode25(const uint8_t *frame, SRNOS_WBusDec *out)
{
    if (!frame || !out || frame[0] != 0x0fu || frame[24] != 0 ||
        (frame[23] & 0xf0u)) return false;
    SRNOS_WBusDec decoded = {0};
    for (unsigned ch = 0; ch < SRNOS_WBUS_CHANNEL_COUNT; ++ch) {
        /* The last channel uses only payload bytes 21..22, never the flags. */
        uint16_t value = 0;
        for (unsigned bit = 0; bit < 11; ++bit) {
            const unsigned offset = ch * 11u + bit;
            if (frame[1u + offset / 8u] & (1u << (offset % 8u)))
                value |= (uint16_t)(1u << bit);
        }
        decoded.ch[ch] = value;
    }
    decoded.digital17 = (frame[23] & 1u) != 0;
    decoded.digital18 = (frame[23] & 2u) != 0;
    decoded.frame_lost = (frame[23] & 4u) != 0;
    decoded.failsafe = (frame[23] & 8u) != 0;
    *out = decoded;
    return true;
}

void SRNOS_WBusFramerInit(SRNOS_WBusFramer *framer)
{
    if (framer) memset(framer, 0, sizeof(*framer));
}

size_t SRNOS_WBusFramerFeed(SRNOS_WBusFramer *f, const uint8_t *data,
                          size_t length, SRNOS_WBusDec *latest)
{
    if (!f || (!data && length)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < length; ++i) {
        if (!f->length && data[i] != 0x0fu) {
            ++f->discarded_bytes;
            continue;
        }
        f->buffer[f->length++] = data[i];
        if (f->length != SRNOS_WBUS_FRAME_LEN) continue;
        SRNOS_WBusDec decoded;
        if (SRNOS_WBusDecode25(f->buffer, &decoded)) {
            if (latest) *latest = decoded;
            if (f->frame_callback) f->frame_callback(&decoded, f->user);
            ++count;
            ++f->frames;
            f->frame_lost_frames += decoded.frame_lost;
            f->failsafe_frames += decoded.failsafe;
            f->length = 0;
        } else {
            /* Slide to the next candidate, including a header inside payload. */
            unsigned skip = 1;
            while (skip < f->length && f->buffer[skip] != 0x0fu) ++skip;
            f->length = (uint8_t)(f->length - skip);
            memmove(f->buffer, f->buffer + skip, f->length);
            f->discarded_bytes += skip;
        }
    }
    return count;
}

SRNOS_SerialStatus SRNOS_WBusInit(SRNOS_WBus *self, SRNOS_SerialChannel *channel)
{
    if (!self || !channel) return SRNOS_SERIAL_ARGUMENT;
    if (self->channel) return SRNOS_SERIAL_STATE;
    const SRNOS_SerialConfig *cfg = SRNOS_SerialGetConfig(channel);
    if (!cfg || cfg->mode != SRNOS_SERIAL_DUPLEX || !cfg->rx_enabled ||
        cfg->baud_rate != 100000 || cfg->data_bits != 8 || cfg->stop_bits != 2 ||
        cfg->parity != SRNOS_SERIAL_PARITY_EVEN) return SRNOS_SERIAL_ARGUMENT;
    SRNOS_SerialStatus result = SRNOS_SerialClaim(channel, self);
    if (result != SRNOS_SERIAL_OK) return result;
    memset(self, 0, sizeof(*self));
    self->channel = channel;
    return SRNOS_SERIAL_OK;
}

static void reset_partial(SRNOS_WBus *self)
{
    self->framer.discarded_bytes += self->framer.length;
    self->framer.length = 0;
    ++self->diag.stream_resets;
}

static bool refresh_stats(SRNOS_WBus *self)
{
    SRNOS_SerialStats stats;
    if (SRNOS_SerialGetStats(self->channel, &stats) != SRNOS_SERIAL_OK)
        return false;
    const bool continuous = stats.rx_overflow == self->diag.serial.rx_overflow &&
        stats.hardware_errors == self->diag.serial.hardware_errors &&
        stats.recoveries == self->diag.serial.recoveries;
    self->diag.serial = stats;
    return continuous;
}

size_t SRNOS_WBusPoll(SRNOS_WBus *self, SRNOS_WBusDec *latest, uint32_t now_ms)
{
    if (!self || !self->channel || !latest) return 0;
    if (!refresh_stats(self)) {
        reset_partial(self);
        (void)SRNOS_SerialDiscardRx(self->channel);
        return 0;
    }
    if (self->framer.length && now_ms - self->last_byte_ms > 30u)
        reset_partial(self);
    uint8_t bytes[128];
    size_t count = 0;
    SRNOS_WBusDec decoded;
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
        count += SRNOS_WBusFramerFeed(&self->framer, bytes, received, &decoded);
    }
    if (count) {
        *latest = decoded;
        self->diag.last_frame_ms = now_ms;
        self->diag.seen_frame = true;
    }
    return count;
}
