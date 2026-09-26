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

#define VT03_FRAME_LEN       21u
#define VT03_SOF0            0xA9u
#define VT03_SOF1            0x53u
#define VT03_RC_OFFSET       1024
#define VT03_RC_MIN          364
#define VT03_RC_MAX          1684

#ifndef VT03_MAX_INSTANCES
#define VT03_MAX_INSTANCES            4u
#endif

typedef struct vt03_dec_s {
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
} vt03_dec_t;

typedef struct {
    uint8_t buf[VT03_FRAME_LEN];
    uint8_t len;
} vt03_framer_t;

typedef struct {
    uint8_t b[VT03_FRAME_LEN];
} vt03_raw_t;

typedef void (*vt03_raw_tap_cb_t)(const uint8_t* data, uint16_t len, void* user);

typedef struct vt03_diag_s {
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
} vt03_diag_t;

typedef struct {
    SRNOS_SerialChannel *channel;
    vt03_raw_tap_cb_t raw_tap_cb;
    void *user;
} vt03_config_t;
typedef struct vt03_s {
    SRNOS_SerialChannel *channel;
    vt03_framer_t framer;
    vt03_raw_tap_cb_t raw_tap_cb;
    void *user;
    vt03_diag_t diag;
    uint16_t debug_index;
} vt03_t;

int      VT03_Init(vt03_t* self, const vt03_config_t* cfg);
int      VT03_Start(vt03_t* self);
int      VT03_Stop(vt03_t* self);
int      VT03_Rearm(vt03_t* self);
int      VT03_PollDec(vt03_t* self, vt03_dec_t* out, uint32_t now_ms);
int      VT03_ReadDiag(const vt03_t* self, vt03_diag_t* out);
uint32_t VT03_CheckSerialConfig(const SRNOS_SerialConfig* config);

uint16_t VT03_CalcCrc(const uint8_t* data, uint16_t len);
int      VT03_FrameSanity(const uint8_t* frame21);
int      VT03_Decode21(const uint8_t* frame21, vt03_dec_t* out);
void     VT03_FramerInit(vt03_framer_t* f);
size_t   VT03_FramerFeed(vt03_framer_t* f,
                         const uint8_t* data,
                         size_t len,
                         vt03_raw_t* out_frames,
                         size_t max_frames,
                         uint32_t* drop_count);

#ifdef __cplusplus
}
#endif
