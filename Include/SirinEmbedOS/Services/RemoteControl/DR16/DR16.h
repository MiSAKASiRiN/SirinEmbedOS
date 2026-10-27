#pragma once

#include "SirinEmbedOS/IO/Serial/Serial.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DJI DR16 DBUS protocol used by the OmniX receiver path:
 * 18-byte frame, 100 kbit/s, 8 data bits + even parity, 2 stop bits.
 * On STM32 HAL this is normally UART_WORDLENGTH_9B with UART_PARITY_EVEN.
 */

#define SRNOS_DR16_FRAME_LEN            18u
#define SRNOS_DR16_RC_OFFSET            1024
#define SRNOS_DR16_RC_MIN               364
#define SRNOS_DR16_RC_MAX               1684
#define SRNOS_DR16_RC_AUX_MAX           2047
#define SRNOS_DR16_RC_SCALE             660.0f

#ifndef SRNOS_DR16_MAX_INSTANCES
#define SRNOS_DR16_MAX_INSTANCES            4u
#endif

#ifndef SRNOS_DR16_NORM_DEADZONE
#define SRNOS_DR16_NORM_DEADZONE            0.03f
#endif

typedef struct {
    uint8_t b[SRNOS_DR16_FRAME_LEN];
} SRNOS_DR16Raw;

typedef struct SRNOS_DR16DecS {
    int16_t ch[5];     /* ch0..3: [364..1684], ch4/AUX: [364..2047] */
    uint8_t s1;        /* 1:up 3:mid 2:down */
    uint8_t s2;        /* 1:up 3:mid 2:down */
    int16_t mx;
    int16_t my;
    int16_t mz;
    uint8_t ml;        /* mouse left button, 0/1 */
    uint8_t mr;        /* mouse right button, 0/1 */
    uint16_t keys;     /* official low bits: W/S/A/D/Q/E/Shift/Ctrl */
} SRNOS_DR16Dec;

typedef struct {
    float   ch[5];
    uint8_t s1;
    uint8_t s2;
    int16_t mx;
    int16_t my;
    int16_t mz;
    uint8_t ml;
    uint8_t mr;
    uint16_t keys;
} SRNOS_DR16Norm;

typedef struct {
    uint8_t buf[SRNOS_DR16_FRAME_LEN];
    uint8_t len;
} SRNOS_DR16Framer;

typedef void (*SRNOS_DR16RawTapCb)(const uint8_t* data, uint16_t len, void* user);

typedef struct {
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
} SRNOS_DR16Diag;

typedef void (*SRNOS_DR16FrameCallback)(const SRNOS_DR16Dec *,void *);

typedef struct {
    SRNOS_SerialChannel *channel;
    SRNOS_DR16RawTapCb raw_tap_cb;
    void *user;
    SRNOS_DR16FrameCallback frame_callback; /* Each decoded frame, borrowed in polling task. */
} SRNOS_DR16Config;
typedef struct SRNOS_DR16S {
    SRNOS_SerialChannel *channel;
    SRNOS_DR16Framer framer;
    SRNOS_DR16RawTapCb raw_tap_cb;
    void *user;
    SRNOS_DR16FrameCallback frame_callback; /* Each decoded frame, borrowed in polling task. */
    SRNOS_DR16Diag diag;
    uint16_t debug_index;
    uint32_t framer_started_ms;
} SRNOS_DR16T;

int      SRNOS_DR16Init(SRNOS_DR16T* self, const SRNOS_DR16Config* cfg);
int      SRNOS_DR16Start(SRNOS_DR16T* self);
int      SRNOS_DR16Stop(SRNOS_DR16T* self);
int      SRNOS_DR16Rearm(SRNOS_DR16T* self);
int      SRNOS_DR16PollDec(SRNOS_DR16T* self, SRNOS_DR16Dec* out, uint32_t now_ms);
int      SRNOS_DR16ReadDiag(const SRNOS_DR16T* self, SRNOS_DR16Diag* out);
uint32_t SRNOS_DR16CheckSerialConfig(const SRNOS_SerialConfig* config);

int      SRNOS_DR16FrameSanity(const uint8_t* frame18);
void     SRNOS_DR16Decode18(const uint8_t* frame18, SRNOS_DR16Dec* out);
void     SRNOS_DR16Normalize(const SRNOS_DR16Dec* in, SRNOS_DR16Norm* out, float deadzone);
void     SRNOS_DR16FramerInit(SRNOS_DR16Framer* f);
size_t   SRNOS_DR16FramerFeed(SRNOS_DR16Framer* f,
                         const uint8_t* data,
                         size_t len,
                         SRNOS_DR16Raw* out_frames,
                         size_t max_frames,
                         uint32_t* drop_count);

#ifdef __cplusplus
}
#endif
