#include "SirinEmbedOS/Services/RemoteControl/DR16/DR16.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Numeric.h"
#include <string.h>
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgDR16 SRNOS_DebugDbgDR16[SRNOS_DR16_MAX_INSTANCES];
static unsigned next_debug_index;
int SRNOS_DR16FrameSanity(const uint8_t* s)
{
    if (s == NULL) {
        return 0;
    }

    SRNOS_DR16Dec d;
    SRNOS_DR16Decode18(s, &d);

    /* ch0..ch3 (sticks) use standard range */
    for (uint32_t i = 0; i < 4u; ++i) {
        if (d.ch[i] < SRNOS_DR16_RC_MIN || d.ch[i] > SRNOS_DR16_RC_MAX) {
            return 0;
        }
    }
    /* ch4 (AUX dial) has a wider physical range */
    if (d.ch[4] < SRNOS_DR16_RC_MIN || d.ch[4] > SRNOS_DR16_RC_AUX_MAX) {
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

void SRNOS_DR16Decode18(const uint8_t* s, SRNOS_DR16Dec* d)
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

void SRNOS_DR16Normalize(const SRNOS_DR16Dec* in, SRNOS_DR16Norm* out, float deadzone)
{
    if (in == NULL || out == NULL) {
        return;
    }
    if (deadzone < 0.0f) {
        deadzone = 0.0f;
    }

    memset(out, 0, sizeof(*out));
    for (uint32_t i = 0; i < 5u; ++i) {
        float v = ((float)in->ch[i] - (float)SRNOS_DR16_RC_OFFSET) / (float)SRNOS_DR16_RC_SCALE;
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

void SRNOS_DR16FramerInit(SRNOS_DR16Framer* f)
{
    if (f != NULL) {
        f->len = 0u;
    }
}

size_t SRNOS_DR16FramerFeed(SRNOS_DR16Framer* f,
                       const uint8_t* data,
                       size_t len,
                       SRNOS_DR16Raw* out_frames,
                       size_t max_frames,
                       uint32_t* drop_count)
{
    if (f == NULL || data == NULL || out_frames == NULL || max_frames == 0u) {
        return 0u;
    }

    size_t out_count = 0u;
    for (size_t i = 0u; i < len; ++i) {
        if (f->len < SRNOS_DR16_FRAME_LEN) {
            f->buf[f->len++] = data[i];
            if (f->len < SRNOS_DR16_FRAME_LEN) {
                continue;
            }
        }

        if (SRNOS_DR16FrameSanity(f->buf)) {
            if(out_count<max_frames) {
                memcpy(out_frames[out_count++].b,f->buf,SRNOS_DR16_FRAME_LEN);
            } else if(drop_count) {
                *drop_count+=SRNOS_DR16_FRAME_LEN;
            }
            f->len = 0u;
        } else {
            for (uint32_t j = 0u; j < SRNOS_DR16_FRAME_LEN - 1u; ++j) {
                f->buf[j] = f->buf[j + 1u];
            }
            f->len = (uint8_t)(SRNOS_DR16_FRAME_LEN - 1u);
            if (drop_count != NULL) {
                (*drop_count)++;
            }
        }
    }
    return out_count;
}


uint32_t SRNOS_DR16CheckSerialConfig(const SRNOS_SerialConfig *c) {
 return !c || c->baud_rate!=100000u || c->data_bits!=8 || c->stop_bits!=2 || c->parity!=SRNOS_SERIAL_PARITY_EVEN || !c->rx_enabled;
}
static int stream_discontinuity(SRNOS_DR16T *s) {
 SRNOS_SerialStats serial;
 if(SRNOS_SerialGetStats(s->channel,&serial)!=SRNOS_SERIAL_OK)return 1;
 int changed=s->diag.rx_hw_error_count!=serial.hardware_errors||s->diag.rx_recover_count!=serial.recoveries||s->diag.rx_fifo_overflow_bytes!=serial.rx_overflow;
 s->diag.rx_hw_error_count=serial.hardware_errors;s->diag.rx_recover_count=serial.recoveries;
 s->diag.rx_fifo_overflow_bytes=serial.rx_overflow;
 if(changed){s->framer.len=0;++s->diag.stream_resets;(void)SRNOS_SerialDiscardRx(s->channel);}
 return changed;
}
static void sync_debug(SRNOS_DR16T *s,uint32_t now) {
 volatile SRNOS_DbgDR16 *dbg=&SRNOS_DebugDbgDR16[s->debug_index];
 SRNOS_DbgBegin(&dbg->h);
 dbg->started=s->diag.started;dbg->rx_hw_error_count=s->diag.rx_hw_error_count;
 dbg->rx_recover_count=s->diag.rx_recover_count;dbg->rx_fifo_overflow_bytes=s->diag.rx_fifo_overflow_bytes;
 dbg->rx_framer_drop_count=s->diag.rx_framer_drop_count;dbg->rx_frame_ok_count=s->diag.rx_frame_ok_count;
 /* Legacy physical DMA fields stay zero; Serial owns the hardware now. */
 SRNOS_DbgEnd(&dbg->h,now,s->diag.started);
}
int SRNOS_DR16Init(SRNOS_DR16T *self,const SRNOS_DR16Config *cfg) {
 if(next_debug_index>=SRNOS_DR16_MAX_INSTANCES||!self||!cfg||SRNOS_DR16CheckSerialConfig(SRNOS_SerialGetConfig(cfg->channel)))return 0;
 if(SRNOS_SerialClaim(cfg->channel,self)!=SRNOS_SERIAL_OK)return 0;
 memset(self,0,sizeof(*self));self->channel=cfg->channel;self->raw_tap_cb=cfg->raw_tap_cb;self->user=cfg->user;self->frame_callback=cfg->frame_callback;
 self->debug_index=next_debug_index++;
 SRNOS_DbgZero(&SRNOS_DebugDbgDR16[self->debug_index],sizeof(SRNOS_DebugDbgDR16[self->debug_index]));
 SRNOS_DbgInit(&SRNOS_DebugDbgDR16[self->debug_index].h,SRNOS_DBG_MODULE_DR16,self->debug_index,sizeof(SRNOS_DebugDbgDR16[self->debug_index]));return 1;
}
int SRNOS_DR16Start(SRNOS_DR16T *s) {if(!s||!s->channel)return 0;s->diag.started=1;return 1;}
int SRNOS_DR16Stop(SRNOS_DR16T *s) {if(!s)return 0;s->diag.started=0;return 1;}
int SRNOS_DR16Rearm(SRNOS_DR16T *s) {return SRNOS_DR16Start(s);} /* Physical recovery belongs to Serial. */
int SRNOS_DR16ReadDiag(const SRNOS_DR16T *s,SRNOS_DR16Diag *out) {if(!s||!out)return 0;*out=s->diag;return 1;}
int SRNOS_DR16PollDec(SRNOS_DR16T *s,SRNOS_DR16Dec *out,uint32_t now_ms) {
 if(!s||!s->channel||!s->diag.started)return 0;
 if(stream_discontinuity(s)){sync_debug(s,now_ms);return 0;}
 if(s->framer.len && now_ms-s->framer_started_ms>=10u){s->diag.rx_framer_drop_count+=s->framer.len;s->framer.len=0;}
 if(!s->framer.len)s->framer_started_ms=now_ms;
 uint8_t bytes[128];int got_frame=0;SRNOS_DR16Dec latest={0};
 /* Bound work to one ring per invocation under a continuously active line. */
 for(unsigned budget=0;budget<8;budget++){
  size_t n=0;if(SRNOS_SerialRead(s->channel,bytes,sizeof(bytes),&n,0)!=SRNOS_SERIAL_OK)break;
  if(stream_discontinuity(s)){got_frame=0;break;}
  if(s->raw_tap_cb)s->raw_tap_cb(bytes,(uint16_t)n,s->user);
  SRNOS_DR16Raw frames[8];size_t count=SRNOS_DR16FramerFeed(&s->framer,bytes,n,frames,8,&s->diag.rx_framer_drop_count);
  for(size_t i=0;i<count;i++){SRNOS_DR16Decode18(frames[i].b,&latest);got_frame=1;s->diag.rx_frame_ok_count++;
   if(s->frame_callback)s->frame_callback(&latest,s->user);
  }
 }
 sync_debug(s,now_ms);if(got_frame&&out)*out=latest;return got_frame;
}
