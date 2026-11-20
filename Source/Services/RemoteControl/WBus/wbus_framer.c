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
