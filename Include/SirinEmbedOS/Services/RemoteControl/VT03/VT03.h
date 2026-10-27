#pragma once

#include "SirinEmbedOS/IO/Serial/Serial.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VT03 and VT13 use the same remote-control UART frame:
 * 21-byte frame, 921600 bit/s, 8N1, no flow control, every 14 ms.
 */

#define SRNOS_VT03_FRAME_LEN       21u
#define SRNOS_VT03_SOF0            0xA9u
#define SRNOS_VT03_SOF1            0x53u
#define SRNOS_VT03_RC_OFFSET       1024
#define SRNOS_VT03_RC_MIN          364
#define SRNOS_VT03_RC_MAX          1684

#ifndef SRNOS_VT03_MAX_INSTANCES
#define SRNOS_VT03_MAX_INSTANCES            4u
#endif

typedef struct SRNOS_VT03DecS {
    uint16_t ch[4];      /* ch0 right-x, ch1 right-y, ch2 left-y, ch3 left-x */
    uint8_t  gear;       /* C=0, N=1, S=2 */
    uint8_t  pause;
    uint8_t  fn1;
    uint8_t  fn2;
    uint16_t wheel;      /* dial, raw [364..1684], center 1024 */
    uint8_t  trigger;
    int16_t  mx;
    int16_t  my;
    int16_t  mz;
    uint8_t  ml;
    uint8_t  mr;
    uint8_t  mm;
    uint16_t keys;       /* VT03/PC-style canonical order */
    uint16_t crc_frame;
    uint16_t crc_calc;
} SRNOS_VT03Dec;

/* A5 custom frames and A9 53 remote frames share this byte stream. A complete
 * valid A5 frame is consumed atomically, including any A9 53 in its payload.
 * Callback runs in the polling task and must not block. Payload is borrowed. */
typedef void (*SRNOS_VT03A5Callback)(uint16_t command,const uint8_t *payload,
    uint16_t length,uint8_t sequence,void *user);
typedef struct {
    uint8_t buf[309];
    uint16_t len;
    SRNOS_VT03A5Callback a5_callback;
    void *a5_user;
} SRNOS_VT03Framer;

typedef struct {
    uint8_t b[SRNOS_VT03_FRAME_LEN];
} SRNOS_VT03Raw;

typedef void (*SRNOS_VT03RawTapCb)(const uint8_t* data, uint16_t len, void* user);

typedef struct SRNOS_VT03DiagS {
    uint8_t started;
    uint8_t reserved[3];
    uint32_t rx_evt_idle_count;
    uint32_t rx_evt_tc_count;
    uint32_t rx_hw_error_count;
    uint32_t rx_recover_count;
    uint32_t rx_fifo_overflow_bytes;
    uint32_t rx_framer_drop_count;
    uint32_t rx_frame_ok_count;
    uint32_t rx_start_ret;
    uint32_t stream_resets;
} SRNOS_VT03Diag;

typedef void (*SRNOS_VT03FrameCallback)(const SRNOS_VT03Dec *,void *);

typedef struct {
    SRNOS_SerialChannel *channel;
    SRNOS_VT03RawTapCb raw_tap_cb;
    void *user;
    SRNOS_VT03FrameCallback frame_callback; /* Each decoded frame, borrowed in polling task. */
    SRNOS_VT03A5Callback a5_callback;
    void *a5_user;
} SRNOS_VT03Config;
typedef struct SRNOS_VT03S {
    SRNOS_SerialChannel *channel;
    SRNOS_VT03Framer framer;
    SRNOS_VT03RawTapCb raw_tap_cb;
    void *user;
    SRNOS_VT03FrameCallback frame_callback; /* Each decoded frame, borrowed in polling task. */
    SRNOS_VT03Diag diag;
    uint16_t debug_index;
    uint32_t framer_started_ms;
} SRNOS_VT03T;

int      SRNOS_VT03Init(SRNOS_VT03T* self, const SRNOS_VT03Config* cfg);
int      SRNOS_VT03Start(SRNOS_VT03T* self);
int      SRNOS_VT03Stop(SRNOS_VT03T* self);
int      SRNOS_VT03Rearm(SRNOS_VT03T* self);
int      SRNOS_VT03PollDec(SRNOS_VT03T* self, SRNOS_VT03Dec* out, uint32_t now_ms);
int      SRNOS_VT03ReadDiag(const SRNOS_VT03T* self, SRNOS_VT03Diag* out);
uint32_t SRNOS_VT03CheckSerialConfig(const SRNOS_SerialConfig* config);

uint16_t SRNOS_VT03CalcCrc(const uint8_t* data, uint16_t len);
int      SRNOS_VT03FrameSanity(const uint8_t* frame21);
int      SRNOS_VT03Decode21(const uint8_t* frame21, SRNOS_VT03Dec* out);
void     SRNOS_VT03FramerInit(SRNOS_VT03Framer* f);
size_t   SRNOS_VT03FramerFeed(SRNOS_VT03Framer* f,
                         const uint8_t* data,
                         size_t len,
                         SRNOS_VT03Raw* out_frames,
                         size_t max_frames,
                         uint32_t* drop_count);

#ifdef __cplusplus
}
#endif
