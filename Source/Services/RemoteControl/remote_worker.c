/* Mapping preserved from prototype Tasks/Body/Src/RC_Task.c. */
#include "SirinEmbedOS/Services/RemoteControl/Startup.h"
#include "SirinEmbedOS/Services/RemoteControl/DR16/bsp_dr16.h"
#include "SirinEmbedOS/Services/RemoteControl/VT03/bsp_vt03.h"
#include "cmsis_os2.h"
#include <string.h>
static dr16_t dr16;
static vt03_t vt03;
static bool configured,started;
void SRNOS_RemoteControlFreeze(void);
static uint16_t dr16_keys_to_canonical(uint16_t keys)
{
    uint16_t out = 0u;

    if (keys & ((uint16_t)1u << 0)) out |= KEY_PRESSED_OFFSET_W;
    if (keys & ((uint16_t)1u << 1)) out |= KEY_PRESSED_OFFSET_S;
    if (keys & ((uint16_t)1u << 2)) out |= KEY_PRESSED_OFFSET_A;
    if (keys & ((uint16_t)1u << 3)) out |= KEY_PRESSED_OFFSET_D;
    if (keys & ((uint16_t)1u << 4)) out |= KEY_PRESSED_OFFSET_Q;
    if (keys & ((uint16_t)1u << 5)) out |= KEY_PRESSED_OFFSET_E;
    if (keys & ((uint16_t)1u << 6)) out |= KEY_PRESSED_OFFSET_SHIFT;
    if (keys & ((uint16_t)1u << 7)) out |= KEY_PRESSED_OFFSET_CTRL;

    return out;
}

static uint8_t vt03_gear_to_rc_sw(uint8_t gear)
{
    switch (gear) {
        case 0u: return RC_SW_UP;    /* C */
        case 1u: return RC_SW_MID;   /* N */
        case 2u: return RC_SW_DOWN;  /* S */
        default: return RC_SW_UNAVAILABLE;
    }
}

static void update_from_dr16(const dr16_dec_t* d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }

    RC_State s;
    memset(&s, 0, sizeof(s));

    s.rc.ch[RC_CH_RIGHT_X] = (int16_t)(d->ch[0] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_RIGHT_Y] = (int16_t)(d->ch[1] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_LEFT_Y] = (int16_t)(d->ch[2] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_LEFT_X] = (int16_t)(d->ch[3] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_AUX] = (int16_t)(d->ch[4] - RC_CH_VALUE_OFFSET);
    s.rc.s[0] = d->s1;
    s.rc.s[1] = d->s2;
    s.rc.s_valid_mask = 0x03u;

    s.mouse.x = d->mx;
    s.mouse.y = d->my;
    s.mouse.z = d->mz;
    s.mouse.press_l = (uint8_t)(d->ml ? 1u : 0u);
    s.mouse.press_r = (uint8_t)(d->mr ? 1u : 0u);
    s.key.v = dr16_keys_to_canonical(d->keys);
    s.key.v_ext = 0u;

    RC_UpdateFromSource(RC_SRC_DR16, &s, now_ms);
}

static void update_from_vt03(const vt03_dec_t* v, uint32_t now_ms)
{
    if (v == NULL) {
        return;
    }

    RC_State s;
    memset(&s, 0, sizeof(s));

    s.rc.ch[RC_CH_RIGHT_X] = (int16_t)(v->ch[0] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_RIGHT_Y] = (int16_t)(v->ch[1] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_LEFT_Y] = (int16_t)(v->ch[2] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_LEFT_X] = (int16_t)(v->ch[3] - RC_CH_VALUE_OFFSET);
    s.rc.ch[RC_CH_AUX] = (int16_t)(v->wheel - RC_CH_VALUE_OFFSET);
    s.rc.s[0] = vt03_gear_to_rc_sw(v->gear);
    s.rc.s[1] = RC_SW_UNAVAILABLE;
    s.rc.s_valid_mask = 0x01u;

    s.mouse.x = v->mx;
    s.mouse.y = v->my;
    s.mouse.z = v->mz;
    s.mouse.press_l = (uint8_t)(v->ml ? 1u : 0u);
    s.mouse.press_r = (uint8_t)(v->mr ? 1u : 0u);
    s.mouse.press_m = (uint8_t)(v->mm ? 1u : 0u);

    s.key.v = v->keys;
    if (v->pause)   s.key.v_ext |= KEY_EXT_VT03_PAUSE;
    if (v->fn1)     s.key.v_ext |= KEY_EXT_VT03_BTN_L;
    if (v->fn2)     s.key.v_ext |= KEY_EXT_VT03_BTN_R;
    if (v->trigger) s.key.v_ext |= KEY_EXT_VT03_TRIGGER;

    RC_UpdateFromSource(RC_SRC_VT03, &s, now_ms);
}


int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *cfg) {
 if(configured||!cfg)return -1;
 if(cfg->primary<RC_SRC_NONE||cfg->primary>=RC_SOURCE_COUNT||cfg->secondary<RC_SRC_NONE||cfg->secondary>=RC_SOURCE_COUNT||
 !cfg->idle_ms||cfg->offline_ms<=cfg->idle_ms)return -1;
 if(cfg->dr16_port){dr16_config_t c={SRNOS_SerialGetChannel(cfg->dr16_port),NULL,NULL};if(!DR16_Init(&dr16,&c))return -1;}
 if(cfg->vt03_port){vt03_config_t c={SRNOS_SerialGetChannel(cfg->vt03_port),NULL,NULL};if(!VT03_Init(&vt03,&c))return -1;}
 RC_InputInit();RC_SetSourcePriority(cfg->primary,cfg->secondary);RC_SetTimeouts(cfg->idle_ms,cfg->offline_ms);
 RC_SetStickDeadzone(cfg->stick_deadzone);RC_RegisterPostMapHook(cfg->post_map_hook);
 SRNOS_RemoteControlFreeze();configured=true;return 0;
}
static void remote_worker(void *arg) {
 (void)arg;if(dr16.channel)DR16_Start(&dr16);if(vt03.channel)VT03_Start(&vt03);
 for(;;){uint32_t now=(uint32_t)((uint64_t)osKernelGetTickCount()*1000/osKernelGetTickFreq());
  dr16_dec_t d;if(dr16.channel&&DR16_PollDec(&dr16,&d,now))update_from_dr16(&d,now);
  vt03_dec_t v;if(vt03.channel&&VT03_PollDec(&vt03,&v,now))update_from_vt03(&v,now);
  RC_Tick(now);osDelay(1);
 }
}
int SRNOS_RemoteControlStart(void) {
 if(started||!configured||osKernelGetState()!=osKernelReady)return -1;started=true;
 if(!dr16.channel&&!vt03.channel)return 0;
 const osThreadAttr_t attr={.name="RemoteControl",.stack_size=3072,.priority=osPriorityAboveNormal};
 return osThreadNew(remote_worker,NULL,&attr)?0:-1;
}
