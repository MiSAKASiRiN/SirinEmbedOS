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

#define DR16_FRAME_LEN            18u
#define DR16_RC_OFFSET            1024
#define DR16_RC_MIN               364
#define DR16_RC_MAX               1684
#define DR16_RC_AUX_MAX           2047
#define DR16_RC_SCALE             660.0f

#ifndef DR16_MAX_INSTANCES
#define DR16_MAX_INSTANCES            4u
#endif

#ifndef DR16_NORM_DEADZONE
#define DR16_NORM_DEADZONE            0.03f
#endif

typedef struct {
    uint8_t b[DR16_FRAME_LEN];
} dr16_raw_t;

typedef struct dr16_dec_s {
    int16_t ch[5];     /* ch0..3: [364..1684], ch4/AUX: [364..2047] */
    uint8_t s1;        /* 1:up 3:mid 2:down */
    uint8_t s2;        /* 1:up 3:mid 2:down */
    int16_t mx;
    int16_t my;
    int16_t mz;
    uint8_t ml;        /* mouse left button, 0/1 */
    uint8_t mr;        /* mouse right button, 0/1 */
    uint16_t keys;     /* official low bits: W/S/A/D/Q/E/Shift/Ctrl */
} dr16_dec_t;

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
} dr16_norm_t;

typedef struct {
    uint8_t buf[DR16_FRAME_LEN];
    uint8_t len;
} dr16_framer_t;

typedef void (*dr16_raw_tap_cb_t)(const uint8_t* data, uint16_t len, void* user);

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
} dr16_diag_t;

typedef struct {
    SRNOS_SerialChannel *channel;
    dr16_raw_tap_cb_t raw_tap_cb;
    void *user;
} dr16_config_t;
typedef struct dr16_s {
    SRNOS_SerialChannel *channel;
    dr16_framer_t framer;
    dr16_raw_tap_cb_t raw_tap_cb;
    void *user;
    dr16_diag_t diag;
    uint16_t debug_index;
} dr16_t;

int      DR16_Init(dr16_t* self, const dr16_config_t* cfg);
int      DR16_Start(dr16_t* self);
int      DR16_Stop(dr16_t* self);
int      DR16_Rearm(dr16_t* self);
int      DR16_PollDec(dr16_t* self, dr16_dec_t* out, uint32_t now_ms);
int      DR16_ReadDiag(const dr16_t* self, dr16_diag_t* out);
uint32_t DR16_CheckSerialConfig(const SRNOS_SerialConfig* config);

int      DR16_FrameSanity(const uint8_t* frame18);
void     DR16_Decode18(const uint8_t* frame18, dr16_dec_t* out);
void     DR16_Normalize(const dr16_dec_t* in, dr16_norm_t* out, float deadzone);
void     DR16_FramerInit(dr16_framer_t* f);
size_t   DR16_FramerFeed(dr16_framer_t* f,
                         const uint8_t* data,
                         size_t len,
                         dr16_raw_t* out_frames,
                         size_t max_frames,
                         uint32_t* drop_count);

#ifdef __cplusplus
}
#endif
