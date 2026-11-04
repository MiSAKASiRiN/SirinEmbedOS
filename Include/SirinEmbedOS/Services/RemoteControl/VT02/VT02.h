#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_VT02_CMD_CUSTOM_CONTROLLER 0x0302u
#define SRNOS_VT02_CMD_KEYBOARD_MOUSE 0x0304u
#define SRNOS_VT02_CUSTOM_LENGTH 30u
#define SRNOS_VT02_KEYBOARD_MOUSE_LENGTH 12u
#define SRNOS_VT02_MAX_PAYLOAD 300u
/* Built-in 0302 delivery via Message: transport=SRNOS_MESSAGE_VT02,
 * endpoint=vt02_port, service/type below, length=30, sequence=A5 uint8 seq.
 * Payload is copied unchanged, no SM/SRMT envelope or input mapping implied. */
#define SRNOS_VT02_CUSTOM_SERVICE 0x22u
#define SRNOS_VT02_CUSTOM_MESSAGE 1u

/* Legacy VT02/VT12 video-link UART, 115200/8N1 (2024 protocol V1.6.4).
 * Independent of the referee power-module User UART and VT03/VT13 RC.
 * Wire keyboard bits already match SRNOS_InputButton W..B. No RC axes/gear.
 * Mouse values are signed wire samples, published once per received frame.
 * Reserved bytes are retained without imposing undocumented zero checks. */
typedef struct {
    int16_t mouse_x, mouse_y, mouse_z;
    uint16_t keyboard, reserved;
    bool left, right;
} SRNOS_VT02Dec;
typedef void (*SRNOS_VT02FrameCallback)(const SRNOS_VT02Dec *, void *);
/* Borrowed data, valid only during the synchronous Feed/Poll callback.
 * Receives only CRC-valid 0x0302 packets of exactly 30 bytes. */
typedef void (*SRNOS_VT02CustomCallback)(const uint8_t *, uint8_t sequence, void *);
typedef struct {
    SRNOS_VT02FrameCallback frame_callback;
    SRNOS_VT02CustomCallback custom_callback;
    void *user;
    uint8_t buffer[SRNOS_VT02_MAX_PAYLOAD + 9u];
    uint16_t length;
    uint32_t frames, custom_frames, ignored_frames, invalid_payloads;
    uint32_t header_errors, crc_errors, discarded_bytes;
} SRNOS_VT02Framer;

/* Pure, task-independent helpers. Decode requires exactly 12 bytes and boolean
 * mouse buttons (0/1); invalid input leaves out unchanged. Feed consumes all
 * bytes, returns keyboard/mouse frame count, optionally writes newest sample.
 * Every valid sample invokes its callback, preserving intra-poll edges.
 * CRC-valid unknown commands and recognized CRC16-bad envelopes are consumed
 * whole, never scanned for nested keyboard packets. Payload bound is 300 B.
 * Init clears callbacks; set them afterwards. No sequence deduplication. */
bool SRNOS_VT02Decode(const uint8_t *payload, size_t length, SRNOS_VT02Dec *out);
void SRNOS_VT02FramerInit(SRNOS_VT02Framer *);
size_t SRNOS_VT02FramerFeed(SRNOS_VT02Framer *, const uint8_t *, size_t,
                           SRNOS_VT02Dec *latest);

typedef struct {
    uint32_t last_frame_ms, stream_resets;
    bool seen_frame;
    SRNOS_SerialStats serial;
} SRNOS_VT02Diag;
typedef struct {
    SRNOS_SerialChannel *channel;
    SRNOS_VT02Framer framer;
    SRNOS_VT02Diag diag;
    uint32_t last_byte_ms;
} SRNOS_VT02;

/* Zero-init self, Init once before SerialStart (e.g. bind_channels).
 * Claims one RX-enabled duplex 115200/8N1 non-inverted channel. Init/Poll do not use TX.
 * Do not Init/Poll the built-in remote.vt02_port from the application as well.
 * Single polling task owns object, diagnostics and callbacks. No allocation.
 * Poll is nonblocking, task-only, max 1024 bytes/call; latest may be NULL.
 * UART errors/overflow/recovery discard queued and partial data; a partial
 * packet with no polled bytes for >30 ms is discarded. Only 0304 updates
 * seen_frame/last_frame_ms, at dequeue time in caller's SRNOS_TimeMs domain.
 * Neither seen_frame nor custom traffic implies fresh keyboard input. */
SRNOS_SerialStatus SRNOS_VT02Init(SRNOS_VT02 *, SRNOS_SerialChannel *);
size_t SRNOS_VT02Poll(SRNOS_VT02 *, SRNOS_VT02Dec *latest, uint32_t now_ms);

/* MCU RX means client -> VT12 -> VT02 -> MCU; TX is the reverse direction.
 * API name follows the VT02 wired to the MCU, covering the VT02/VT12 pair.
 * Optional built-in vt02_port TX; task-only, requires frozen tx_enabled=true.
 * Encodes one complete A5 frame (payload 0..300 B) and copies it to Serial.
 * Success means queued, not received by VT02 or the client. No automatic TX,
 * retry, referee routing, FEC or application input mapping. Firmware-specific
 * uplink command support must be checked with the peer. RX remains owned by
 * InputIO. Flush waits for the shared port's UART TC, not a remote acknowledgement.
 * Payload is borrowed only during Send and may be reused on return; NULL is
 * allowed only for length=0. Timeout units/range follow Serial (ms, NO_WAIT or
 * WAIT_FOREVER); each call has its own budget. A timeout never retracts bytes
 * already queued/sent. One caller task should own application TX ordering;
 * concurrent Send+Flush pairs do not form an atomic request/response.
 * Send uses a 309 B local buffer plus Serial's nested copy; budget caller stack.
 * ARGUMENT: bad payload/length/timeout; STATE: disabled/not started/wrong context;
 * TIMEOUT: local queue/lock/drain wait; IO: Flush reports a prior TX error.
 * No protocol-level command whitelist: encoding 0..300 B does not guarantee
 * peer firmware support for that command or length. */
SRNOS_SerialStatus SRNOS_VT02Send(uint16_t command, const uint8_t *payload,
    size_t length, uint8_t sequence, uint32_t timeout_ms);
SRNOS_SerialStatus SRNOS_VT02Flush(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
