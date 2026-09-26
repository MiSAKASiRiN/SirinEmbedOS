#include "SirinEmbedOS/Services/RemoteControl/VT03/bsp_vt03.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Numeric.h"
#include <string.h>
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
OMX_DEBUG_WATCH_BSS volatile omx_dbg_vt03_t g_omx_dbg_vt03[VT03_MAX_INSTANCES];
static unsigned next_debug_index;
static inline uint32_t get_bits(const uint8_t* buf, uint16_t bit_off, uint8_t bit_len)
{
    uint32_t acc = 0u;
    for (uint8_t i = 0u; i < bit_len; ++i) {
        const uint16_t bidx = (uint16_t)(bit_off + i);
        const uint8_t bit = (uint8_t)((buf[bidx >> 3] >> (bidx & 7u)) & 0x01u);
        acc |= ((uint32_t)bit) << i;
    }
    return acc;
}

static const uint16_t vt03_crc_tab[256] = {
    0x0000,0x1189,0x2312,0x329b,0x4624,0x57ad,0x6536,0x74bf,
    0x8c48,0x9dc1,0xaf5a,0xbed3,0xca6c,0xdbe5,0xe97e,0xf8f7,
    0x1081,0x0108,0x3393,0x221a,0x56a5,0x472c,0x75b7,0x643e,
    0x9cc9,0x8d40,0xbfdb,0xae52,0xdaed,0xcb64,0xf9ff,0xe876,
    0x2102,0x308b,0x0210,0x1399,0x6726,0x76af,0x4434,0x55bd,
    0xad4a,0xbcc3,0x8e58,0x9fd1,0xeb6e,0xfae7,0xc87c,0xd9f5,
    0x3183,0x200a,0x1291,0x0318,0x77a7,0x662e,0x54b5,0x453c,
    0xbdcb,0xac42,0x9ed9,0x8f50,0xfbef,0xea66,0xd8fd,0xc974,
    0x4204,0x538d,0x6116,0x709f,0x0420,0x15a9,0x2732,0x36bb,
    0xce4c,0xdfc5,0xed5e,0xfcd7,0x8868,0x99e1,0xab7a,0xbaf3,
    0x5285,0x430c,0x7197,0x601e,0x14a1,0x0528,0x37b3,0x263a,
    0xdecd,0xcf44,0xfddf,0xec56,0x98e9,0x8960,0xbbfb,0xaa72,
    0x6306,0x728f,0x4014,0x519d,0x2522,0x34ab,0x0630,0x17b9,
    0xef4e,0xfec7,0xcc5c,0xddd5,0xa96a,0xb8e3,0x8a78,0x9bf1,
    0x7387,0x620e,0x5095,0x411c,0x35a3,0x242a,0x16b1,0x0738,
    0xffcf,0xee46,0xdcdd,0xcd54,0xb9eb,0xa862,0x9af9,0x8b70,
    0x8408,0x9581,0xa71a,0xb693,0xc22c,0xd3a5,0xe13e,0xf0b7,
    0x0840,0x19c9,0x2b52,0x3adb,0x4e64,0x5fed,0x6d76,0x7cff,
    0x9489,0x8500,0xb79b,0xa612,0xd2ad,0xc324,0xf1bf,0xe036,
    0x18c1,0x0948,0x3bd3,0x2a5a,0x5ee5,0x4f6c,0x7df7,0x6c7e,
    0xa50a,0xb483,0x8618,0x9791,0xe32e,0xf2a7,0xc03c,0xd1b5,
    0x2942,0x38cb,0x0a50,0x1bd9,0x6f66,0x7eef,0x4c74,0x5dfd,
    0xb58b,0xa402,0x9699,0x8710,0xf3af,0xe226,0xd0bd,0xc134,
    0x39c3,0x284a,0x1ad1,0x0b58,0x7fe7,0x6e6e,0x5cf5,0x4d7c,
    0xc60c,0xd785,0xe51e,0xf497,0x8028,0x91a1,0xa33a,0xb2b3,
    0x4a44,0x5bcd,0x6956,0x78df,0x0c60,0x1de9,0x2f72,0x3efb,
    0xd68d,0xc704,0xf59f,0xe416,0x90a9,0x8120,0xb3bb,0xa232,
    0x5ac5,0x4b4c,0x79d7,0x685e,0x1ce1,0x0d68,0x3ff3,0x2e7a,
    0xe70e,0xf687,0xc41c,0xd595,0xa12a,0xb0a3,0x8238,0x93b1,
    0x6b46,0x7acf,0x4854,0x59dd,0x2d62,0x3ceb,0x0e70,0x1ff9,
    0xf78f,0xe606,0xd49d,0xc514,0xb1ab,0xa022,0x92b9,0x8330,
    0x7bc7,0x6a4e,0x58d5,0x495c,0x3de3,0x2c6a,0x1ef1,0x0f78
};

uint16_t VT03_CalcCrc(const uint8_t* data, uint16_t len)
{
    if (data == NULL) {
        return 0xFFFFu;
    }

    uint16_t crc = 0xFFFFu;
    while (len-- != 0u) {
        const uint8_t byte = *data++;
        crc = (uint16_t)((crc >> 8) ^ vt03_crc_tab[(crc ^ byte) & 0xFFu]);
    }
    return crc;
}

static uint16_t frame_crc(const uint8_t* s)
{
    return (uint16_t)(((uint16_t)s[VT03_FRAME_LEN - 1u] << 8) | s[VT03_FRAME_LEN - 2u]);
}

static void decode_payload_unchecked(const uint8_t* s, vt03_dec_t* d)
{
    memset(d, 0, sizeof(*d));
    d->ch[0] = (uint16_t)get_bits(s, 16u, 11u);
    d->ch[1] = (uint16_t)get_bits(s, 27u, 11u);
    d->ch[2] = (uint16_t)get_bits(s, 38u, 11u);
    d->ch[3] = (uint16_t)get_bits(s, 49u, 11u);
    d->gear = (uint8_t)get_bits(s, 60u, 2u);
    d->pause = (uint8_t)get_bits(s, 62u, 1u);
    d->fn1 = (uint8_t)get_bits(s, 63u, 1u);
    d->fn2 = (uint8_t)get_bits(s, 64u, 1u);
    d->wheel = (uint16_t)get_bits(s, 65u, 11u);
    d->trigger = (uint8_t)get_bits(s, 76u, 1u);
    d->mx = (int16_t)get_bits(s, 80u, 16u);
    d->my = (int16_t)get_bits(s, 96u, 16u);
    d->mz = (int16_t)get_bits(s, 112u, 16u);
    d->ml = (uint8_t)get_bits(s, 128u, 2u);
    d->mr = (uint8_t)get_bits(s, 130u, 2u);
    d->mm = (uint8_t)get_bits(s, 132u, 2u);
    d->keys = (uint16_t)get_bits(s, 136u, 16u);
    d->crc_calc = VT03_CalcCrc(s, (uint16_t)(VT03_FRAME_LEN - 2u));
    d->crc_frame = frame_crc(s);
}

static int in_rc_range(uint16_t v)
{
    return (v >= VT03_RC_MIN && v <= VT03_RC_MAX);
}

int VT03_FrameSanity(const uint8_t* s)
{
    if (s == NULL) {
        return 0;
    }
    if (s[0] != VT03_SOF0 || s[1] != VT03_SOF1) {
        return 0;
    }

    const uint16_t crc_calc = VT03_CalcCrc(s, (uint16_t)(VT03_FRAME_LEN - 2u));
    if (crc_calc != frame_crc(s)) {
        return 0;
    }

    vt03_dec_t d;
    decode_payload_unchecked(s, &d);
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (!in_rc_range(d.ch[i])) {
            return 0;
        }
    }
    if (!in_rc_range(d.wheel)) {
        return 0;
    }
    if (d.gear > 2u || d.pause > 1u || d.fn1 > 1u || d.fn2 > 1u || d.trigger > 1u) {
        return 0;
    }
    if (d.ml > 1u || d.mr > 1u || d.mm > 1u) {
        return 0;
    }
    return 1;
}

int VT03_Decode21(const uint8_t* s, vt03_dec_t* d)
{
    if (s == NULL || d == NULL) {
        return 0;
    }
    if (!VT03_FrameSanity(s)) {
        return 0;
    }

    decode_payload_unchecked(s, d);
    return 1;
}

void VT03_FramerInit(vt03_framer_t* f)
{
    if (f != NULL) {
        f->len = 0u;
    }
}

size_t VT03_FramerFeed(vt03_framer_t* f,
                       const uint8_t* data,
                       size_t len,
                       vt03_raw_t* out_frames,
                       size_t max_frames,
                       uint32_t* drop_count)
{
    if (f == NULL || data == NULL || out_frames == NULL || max_frames == 0u) {
        return 0u;
    }

    size_t out_count = 0u;
    for (size_t i = 0u; i < len; ++i) {
        if (f->len < VT03_FRAME_LEN) {
            f->buf[f->len++] = data[i];
            if (f->len < VT03_FRAME_LEN) {
                continue;
            }
        }

        if (VT03_FrameSanity(f->buf)) {
            for (uint32_t j = 0u; j < VT03_FRAME_LEN; ++j) {
                out_frames[out_count].b[j] = f->buf[j];
            }
            out_count++;
            f->len = 0u;
            if (out_count >= max_frames) {
                break;
            }
        } else {
            for (uint32_t j = 0u; j < VT03_FRAME_LEN - 1u; ++j) {
                f->buf[j] = f->buf[j + 1u];
            }
            f->len = (uint8_t)(VT03_FRAME_LEN - 1u);
            if (drop_count != NULL) {
                (*drop_count)++;
            }
        }
    }
    return out_count;
}

static int16_t clamp_i32_to_i16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}


uint32_t VT03_CheckSerialConfig(const SRNOS_SerialConfig *c) {
 return !c || c->baud_rate!=921600u || c->data_bits!=8 || c->stop_bits!=1 || c->parity!=SRNOS_SERIAL_PARITY_NONE || !c->rx_enabled;
}
static void sync_debug(vt03_t *s,uint32_t now) {
 SRNOS_SerialStats serial;
 if(SRNOS_SerialGetStats(s->channel,&serial)==SRNOS_SERIAL_OK){
  s->diag.rx_hw_error_count=serial.hardware_errors;s->diag.rx_recover_count=serial.recoveries;
  s->diag.rx_fifo_overflow_bytes=serial.rx_overflow;
 }
 volatile omx_dbg_vt03_t *dbg=&g_omx_dbg_vt03[s->debug_index];
 omx_dbg_begin(&dbg->h);
 dbg->started=s->diag.started;dbg->rx_hw_error_count=s->diag.rx_hw_error_count;
 dbg->rx_recover_count=s->diag.rx_recover_count;dbg->rx_fifo_overflow_bytes=s->diag.rx_fifo_overflow_bytes;
 dbg->rx_framer_drop_count=s->diag.rx_framer_drop_count;dbg->rx_frame_ok_count=s->diag.rx_frame_ok_count;
 /* Legacy physical DMA fields stay zero; Serial owns the hardware now. */
 omx_dbg_end(&dbg->h,now,s->diag.started);
}
int VT03_Init(vt03_t *self,const vt03_config_t *cfg) {
 if(next_debug_index>=VT03_MAX_INSTANCES||!self||!cfg||VT03_CheckSerialConfig(SRNOS_SerialGetConfig(cfg->channel)))return 0;
 if(SRNOS_SerialClaim(cfg->channel,self)!=SRNOS_SERIAL_OK)return 0;
 memset(self,0,sizeof(*self));self->channel=cfg->channel;self->raw_tap_cb=cfg->raw_tap_cb;self->user=cfg->user;
 self->debug_index=next_debug_index++;
 omx_dbg_zero(&g_omx_dbg_vt03[self->debug_index],sizeof(g_omx_dbg_vt03[self->debug_index]));
 omx_dbg_init(&g_omx_dbg_vt03[self->debug_index].h,OMX_DBG_MODULE_VT03,self->debug_index,sizeof(g_omx_dbg_vt03[self->debug_index]));return 1;
}
int VT03_Start(vt03_t *s) {if(!s||!s->channel)return 0;s->diag.started=1;return 1;}
int VT03_Stop(vt03_t *s) {if(!s)return 0;s->diag.started=0;return 1;}
int VT03_Rearm(vt03_t *s) {return VT03_Start(s);} /* Physical recovery belongs to Serial. */
int VT03_ReadDiag(const vt03_t *s,vt03_diag_t *out) {if(!s||!out)return 0;*out=s->diag;return 1;}
int VT03_PollDec(vt03_t *s,vt03_dec_t *out,uint32_t now_ms) {
 (void)now_ms;if(!s||!s->channel||!s->diag.started)return 0;
 uint8_t bytes[128];int got_frame=0;vt03_dec_t latest={0};
 int32_t mx=0,my=0,mz=0;
 /* Bound work to one ring per invocation under a continuously active line. */
 for(unsigned budget=0;budget<8;budget++){
  size_t n=0;if(SRNOS_SerialRead(s->channel,bytes,sizeof(bytes),&n,0)!=SRNOS_SERIAL_OK)break;
  if(s->raw_tap_cb)s->raw_tap_cb(bytes,(uint16_t)n,s->user);
  vt03_raw_t frames[8];size_t count=VT03_FramerFeed(&s->framer,bytes,n,frames,8,&s->diag.rx_framer_drop_count);
  for(size_t i=0;i<count;i++){VT03_Decode21(frames[i].b,&latest);got_frame=1;s->diag.rx_frame_ok_count++;
   mx+=latest.mx;my+=latest.my;mz+=latest.mz;
  }
 }
 latest.mx=clamp_i32_to_i16(mx);latest.my=clamp_i32_to_i16(my);latest.mz=clamp_i32_to_i16(mz);
 sync_debug(s,now_ms);if(got_frame&&out)*out=latest;return got_frame;
}
