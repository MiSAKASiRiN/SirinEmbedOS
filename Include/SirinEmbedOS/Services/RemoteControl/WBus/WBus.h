#pragma once

#include "SirinEmbedOS/IO/Serial/Serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_WBUS_FRAME_LEN 25u
#define SRNOS_WBUS_CHANNEL_COUNT 16u

/* WFLY W.BUS in S.BUS-compatible output mode: 100000 bit/s, 8E2,
 * inverted signal. Configure rx_inverted according to the board's input path.
 * This decoder accepts the classic 0x00 footer, not S.BUS2 telemetry slots.
 * There is no checksum; framing validation cannot detect all payload errors. */
typedef struct {
    uint16_t ch[SRNOS_WBUS_CHANNEL_COUNT]; /* Wire order CH1..CH16, raw 0..2047. */
    bool digital17, digital18, frame_lost, failsafe;
} SRNOS_WBusDec;

typedef void (*SRNOS_WBusFrameCallback)(const SRNOS_WBusDec *,void *);
typedef struct {
    SRNOS_WBusFrameCallback frame_callback; /* Optional, each decoded frame. */
    void *user;
    uint8_t buffer[SRNOS_WBUS_FRAME_LEN];
    uint8_t length;
    uint32_t frames, discarded_bytes, frame_lost_frames, failsafe_frames;
} SRNOS_WBusFramer;

/* Pure helpers. Decode returns false without modifying out on invalid input.
 * Feed consumes every supplied byte, returns the number of decoded frames,
 * and writes only the newest one to latest. NULL latest is allowed.
 * Counts include frames whose receiver reports frame_lost/failsafe. */
bool SRNOS_WBusDecode25(const uint8_t *frame, SRNOS_WBusDec *out);
void SRNOS_WBusFramerInit(SRNOS_WBusFramer *framer);
size_t SRNOS_WBusFramerFeed(SRNOS_WBusFramer *framer, const uint8_t *data,
                          size_t length, SRNOS_WBusDec *latest);

typedef struct {
    uint32_t last_frame_ms, stream_resets;
    bool seen_frame;
    SRNOS_SerialStats serial;
} SRNOS_WBusDiag;

typedef struct {
    SRNOS_SerialChannel *channel;
    SRNOS_WBusFramer framer;
    SRNOS_WBusDiag diag;
    uint32_t last_byte_ms;
} SRNOS_WBus;

/* Zero-initialize self, then call Init once in bind_channels, before SerialStart.
 * Claims exclusive RX ownership; no extra task, allocation or RC arbitration.
 * Config must be duplex, RX enabled, 100000/8E2. TX is never used.
 * All subsequent calls/access to this object belong to one polling task. */
SRNOS_SerialStatus SRNOS_WBusInit(SRNOS_WBus *self, SRNOS_SerialChannel *channel);
/* Nonblocking task-only poll, at most 1024 bytes per call. Return decoded frame
 * count; zero leaves latest unchanged. last_frame_ms uses caller's clock at
 * dequeue, not a hardware arrival timestamp. Poll regularly (e.g. every 10 ms).
 * UART errors/overflow discard queued bytes and incomplete frames. A partial
 * frame with no polled bytes for >30 ms is discarded. Neither a decoded frame
 * nor seen_frame implies an online/safe control source: inspect age and flags. */
size_t SRNOS_WBusPoll(SRNOS_WBus *self, SRNOS_WBusDec *latest, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
