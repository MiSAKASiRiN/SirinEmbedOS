#include "SirinEmbedOS/Services/RemoteControl/DR16/bsp_dr16.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Numeric.h"
#include <string.h>
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
OMX_DEBUG_WATCH_BSS volatile omx_dbg_dr16_t g_omx_dbg_dr16[DR16_MAX_INSTANCES];
static unsigned next_debug_index;
int DR16_FrameSanity(const uint8_t* s)
{
    if (s == NULL) {
        return 0;
    }

    dr16_dec_t d;
    DR16_Decode18(s, &d);

    /* ch0..ch3 (sticks) use standard range */
    for (uint32_t i = 0; i < 4u; ++i) {
        if (d.ch[i] < DR16_RC_MIN || d.ch[i] > DR16_RC_MAX) {
            return 0;
        }
    }
    /* ch4 (AUX dial) has a wider physical range */
    if (d.ch[4] < DR16_RC_MIN || d.ch[4] > DR16_RC_AUX_MAX) {
        return 0;
    }
    if (d.s1 < 1u || d.s1 > 3u || d.s2 < 1u || d.s2 > 3u) {
        return 0;
    }
    if (d.ml > 1u || d.mr > 1u) {
        return 0;
    }
    return 1;
}

void DR16_Decode18(const uint8_t* s, dr16_dec_t* d)
{
    if (s == NULL || d == NULL) {
        return;
    }

    const SRNOS_Memory_ByteSpan frame = SRNOS_Memory_Bytes(s, 18u);
    uint16_t mouse_x = 0u;
    uint16_t mouse_y = 0u;
    uint16_t mouse_z = 0u;
    uint16_t keys = 0u;
    if (!SRNOS_Endian_LoadLE16(frame, 6u, &mouse_x) ||
        !SRNOS_Endian_LoadLE16(frame, 8u, &mouse_y) ||
        !SRNOS_Endian_LoadLE16(frame, 10u, &mouse_z) ||
        !SRNOS_Endian_LoadLE16(frame, 14u, &keys)) return;

    memset(d, 0, sizeof(*d));
    d->ch[0] = (int16_t)((s[0] | ((uint16_t)s[1] << 8)) & 0x07FFu);
    d->ch[1] = (int16_t)(((s[1] >> 3) | ((uint16_t)s[2] << 5)) & 0x07FFu);
    d->ch[2] = (int16_t)(((s[2] >> 6) | ((uint16_t)s[3] << 2) | ((uint16_t)s[4] << 10)) & 0x07FFu);
    d->ch[3] = (int16_t)(((s[4] >> 1) | ((uint16_t)s[5] << 7)) & 0x07FFu);
    d->s1 = (uint8_t)((s[5] >> 4) & 0x03u);
    d->s2 = (uint8_t)((s[5] >> 6) & 0x03u);
    d->mx = (int16_t)mouse_x;
    d->my = (int16_t)mouse_y;
    d->mz = (int16_t)mouse_z;
    d->ml = s[12];
    d->mr = s[13];
    d->keys = keys;
    d->ch[4] = (int16_t)((s[16] | ((uint16_t)s[17] << 8)) & 0x07FFu);
}

void DR16_Normalize(const dr16_dec_t* in, dr16_norm_t* out, float deadzone)
{
    if (in == NULL || out == NULL) {
        return;
    }
    if (deadzone < 0.0f) {
        deadzone = 0.0f;
    }

    memset(out, 0, sizeof(*out));
    for (uint32_t i = 0; i < 5u; ++i) {
        float v = ((float)in->ch[i] - (float)DR16_RC_OFFSET) / (float)DR16_RC_SCALE;
        v = SRNOS_Numeric_ClampF32(v, -1.2f, 1.2f);
        if (v > -deadzone && v < deadzone) {
            v = 0.0f;
        }
        out->ch[i] = SRNOS_Numeric_ClampF32(v, -1.0f, 1.0f);
    }
    out->s1 = in->s1;
    out->s2 = in->s2;
    out->mx = in->mx;
    out->my = in->my;
    out->mz = in->mz;
    out->ml = (uint8_t)(in->ml != 0u);
    out->mr = (uint8_t)(in->mr != 0u);
    out->keys = in->keys;
}

void DR16_FramerInit(dr16_framer_t* f)
{
    if (f != NULL) {
        f->len = 0u;
    }
}

size_t DR16_FramerFeed(dr16_framer_t* f,
                       const uint8_t* data,
                       size_t len,
                       dr16_raw_t* out_frames,
                       size_t max_frames,
                       uint32_t* drop_count)
{
    if (f == NULL || data == NULL || out_frames == NULL || max_frames == 0u) {
        return 0u;
    }

    size_t out_count = 0u;
    for (size_t i = 0u; i < len; ++i) {
        if (f->len < DR16_FRAME_LEN) {
            f->buf[f->len++] = data[i];
            if (f->len < DR16_FRAME_LEN) {
                continue;
            }
        }

        if (DR16_FrameSanity(f->buf)) {
            for (uint32_t j = 0u; j < DR16_FRAME_LEN; ++j) {
                out_frames[out_count].b[j] = f->buf[j];
            }
            out_count++;
            f->len = 0u;
            if (out_count >= max_frames) {
                break;
            }
        } else {
            for (uint32_t j = 0u; j < DR16_FRAME_LEN - 1u; ++j) {
                f->buf[j] = f->buf[j + 1u];
            }
            f->len = (uint8_t)(DR16_FRAME_LEN - 1u);
            if (drop_count != NULL) {
                (*drop_count)++;
            }
        }
    }
    return out_count;
}


uint32_t DR16_CheckSerialConfig(const SRNOS_SerialConfig *c) {
 return !c || c->baud_rate!=100000u || c->data_bits!=8 || c->stop_bits!=2 || c->parity!=SRNOS_SERIAL_PARITY_EVEN || !c->rx_enabled;
}
static void sync_debug(dr16_t *s,uint32_t now) {
 SRNOS_SerialStats serial;
 if(SRNOS_SerialGetStats(s->channel,&serial)==SRNOS_SERIAL_OK){
  s->diag.rx_hw_error_count=serial.hardware_errors;s->diag.rx_recover_count=serial.recoveries;
  s->diag.rx_fifo_overflow_bytes=serial.rx_overflow;
 }
 volatile omx_dbg_dr16_t *dbg=&g_omx_dbg_dr16[s->debug_index];
 omx_dbg_begin(&dbg->h);
 dbg->started=s->diag.started;dbg->rx_hw_error_count=s->diag.rx_hw_error_count;
 dbg->rx_recover_count=s->diag.rx_recover_count;dbg->rx_fifo_overflow_bytes=s->diag.rx_fifo_overflow_bytes;
 dbg->rx_framer_drop_count=s->diag.rx_framer_drop_count;dbg->rx_frame_ok_count=s->diag.rx_frame_ok_count;
 /* Legacy physical DMA fields stay zero; Serial owns the hardware now. */
 omx_dbg_end(&dbg->h,now,s->diag.started);
}
int DR16_Init(dr16_t *self,const dr16_config_t *cfg) {
 if(next_debug_index>=DR16_MAX_INSTANCES||!self||!cfg||DR16_CheckSerialConfig(SRNOS_SerialGetConfig(cfg->channel)))return 0;
 if(SRNOS_SerialClaim(cfg->channel,self)!=SRNOS_SERIAL_OK)return 0;
 memset(self,0,sizeof(*self));self->channel=cfg->channel;self->raw_tap_cb=cfg->raw_tap_cb;self->user=cfg->user;
 self->debug_index=next_debug_index++;
 omx_dbg_zero(&g_omx_dbg_dr16[self->debug_index],sizeof(g_omx_dbg_dr16[self->debug_index]));
 omx_dbg_init(&g_omx_dbg_dr16[self->debug_index].h,OMX_DBG_MODULE_DR16,self->debug_index,sizeof(g_omx_dbg_dr16[self->debug_index]));return 1;
}
int DR16_Start(dr16_t *s) {if(!s||!s->channel)return 0;s->diag.started=1;return 1;}
int DR16_Stop(dr16_t *s) {if(!s)return 0;s->diag.started=0;return 1;}
int DR16_Rearm(dr16_t *s) {return DR16_Start(s);} /* Physical recovery belongs to Serial. */
int DR16_ReadDiag(const dr16_t *s,dr16_diag_t *out) {if(!s||!out)return 0;*out=s->diag;return 1;}
int DR16_PollDec(dr16_t *s,dr16_dec_t *out,uint32_t now_ms) {
 (void)now_ms;if(!s||!s->channel||!s->diag.started)return 0;
 uint8_t bytes[128];int got_frame=0;dr16_dec_t latest={0};
 /* Bound work to one ring per invocation under a continuously active line. */
 for(unsigned budget=0;budget<8;budget++){
  size_t n=0;if(SRNOS_SerialRead(s->channel,bytes,sizeof(bytes),&n,0)!=SRNOS_SERIAL_OK)break;
  if(s->raw_tap_cb)s->raw_tap_cb(bytes,(uint16_t)n,s->user);
  dr16_raw_t frames[8];size_t count=DR16_FramerFeed(&s->framer,bytes,n,frames,8,&s->diag.rx_framer_drop_count);
  for(size_t i=0;i<count;i++){DR16_Decode18(frames[i].b,&latest);got_frame=1;s->diag.rx_frame_ok_count++;
  }
 }
 sync_debug(s,now_ms);if(got_frame&&out)*out=latest;return got_frame;
}
