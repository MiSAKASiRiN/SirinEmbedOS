#include "thread_internal.h"
/* Mapping preserved from prototype Tasks/Body/Src/SRNOS_RC_Task.c. */
#include "SirinEmbedOS/Services/RemoteControl/Startup.h"
#include "SirinEmbedOS/Services/RemoteControl/DR16/DR16.h"
#include "SirinEmbedOS/Services/RemoteControl/VT03/VT03.h"
#include "cmsis_os2.h"
#include <string.h>
static SRNOS_DR16T dr16;
static SRNOS_VT03T vt03;
static bool configured,started;
void SRNOS_RemoteControlFreeze(void);
static uint16_t SRNOS_DR16KeysToCanonical(uint16_t keys)
{
    uint16_t out = 0u;

    if (keys & ((uint16_t)1u << 0)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_W;
    if (keys & ((uint16_t)1u << 1)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_S;
    if (keys & ((uint16_t)1u << 2)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_A;
    if (keys & ((uint16_t)1u << 3)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_D;
    if (keys & ((uint16_t)1u << 4)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_Q;
    if (keys & ((uint16_t)1u << 5)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_E;
    if (keys & ((uint16_t)1u << 6)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_SHIFT;
    if (keys & ((uint16_t)1u << 7)) out |= SRNOS_RC_KEY_PRESSED_OFFSET_CTRL;

    return out;
}

static uint8_t SRNOS_VT03GearToRcSw(uint8_t gear)
{
    switch (gear) {
        case 0u: return SRNOS_RC_SW_UP;    /* C */
        case 1u: return SRNOS_RC_SW_MID;   /* N */
        case 2u: return SRNOS_RC_SW_DOWN;  /* S */
        default: return SRNOS_RC_SW_UNAVAILABLE;
    }
}

static void update_from_dr16(const SRNOS_DR16Dec* d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }

    SRNOS_RC_State s;
    memset(&s, 0, sizeof(s));

    s.rc.ch[SRNOS_RC_CH_RIGHT_X] = (int16_t)(d->ch[0] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_RIGHT_Y] = (int16_t)(d->ch[1] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_LEFT_Y] = (int16_t)(d->ch[2] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_LEFT_X] = (int16_t)(d->ch[3] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_AUX] = (int16_t)(d->ch[4] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.s[0] = d->s1;
    s.rc.s[1] = d->s2;
    s.rc.s_valid_mask = 0x03u;

    s.mouse.x = d->mx;
    s.mouse.y = d->my;
    s.mouse.z = d->mz;
    s.mouse.press_l = (uint8_t)(d->ml ? 1u : 0u);
    s.mouse.press_r = (uint8_t)(d->mr ? 1u : 0u);
    s.key.v = SRNOS_DR16KeysToCanonical(d->keys);
    s.key.v_ext = 0u;

    SRNOS_RC_UpdateFromSource(SRNOS_RC_SRC_DR16, &s, now_ms);
}

static void update_from_vt03(const SRNOS_VT03Dec* v, uint32_t now_ms)
{
    if (v == NULL) {
        return;
    }

    SRNOS_RC_State s;
    memset(&s, 0, sizeof(s));

    s.rc.ch[SRNOS_RC_CH_RIGHT_X] = (int16_t)(v->ch[0] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_RIGHT_Y] = (int16_t)(v->ch[1] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_LEFT_Y] = (int16_t)(v->ch[2] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_LEFT_X] = (int16_t)(v->ch[3] - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.ch[SRNOS_RC_CH_AUX] = (int16_t)(v->wheel - SRNOS_RC_CH_VALUE_OFFSET);
    s.rc.s[0] = SRNOS_VT03GearToRcSw(v->gear);
    s.rc.s[1] = SRNOS_RC_SW_UNAVAILABLE;
    s.rc.s_valid_mask = 0x01u;

    s.mouse.x = v->mx;
    s.mouse.y = v->my;
    s.mouse.z = v->mz;
    s.mouse.press_l = (uint8_t)(v->ml ? 1u : 0u);
    s.mouse.press_r = (uint8_t)(v->mr ? 1u : 0u);
    s.mouse.press_m = (uint8_t)(v->mm ? 1u : 0u);

    s.key.v = v->keys;
    if (v->pause)   s.key.v_ext |= SRNOS_RC_KEY_EXT_VT03_PAUSE;
    if (v->fn1)     s.key.v_ext |= SRNOS_RC_KEY_EXT_VT03_BTN_L;
    if (v->fn2)     s.key.v_ext |= SRNOS_RC_KEY_EXT_VT03_BTN_R;
    if (v->trigger) s.key.v_ext |= SRNOS_RC_KEY_EXT_VT03_TRIGGER;

    SRNOS_RC_UpdateFromSource(SRNOS_RC_SRC_VT03, &s, now_ms);
}


int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *cfg) {
 if(configured||!cfg)return -1;
 if(cfg->primary<SRNOS_RC_SRC_NONE||cfg->primary>=SRNOS_RC_SOURCE_COUNT||cfg->secondary<SRNOS_RC_SRC_NONE||cfg->secondary>=SRNOS_RC_SOURCE_COUNT||
 !cfg->idle_ms||cfg->offline_ms<=cfg->idle_ms)return -1;
 if(cfg->dr16_port){SRNOS_DR16Config c={SRNOS_SerialGetChannel(cfg->dr16_port),NULL,NULL};if(!SRNOS_DR16Init(&dr16,&c))return -1;}
 if(cfg->vt03_port){SRNOS_VT03Config c={SRNOS_SerialGetChannel(cfg->vt03_port),NULL,NULL};if(!SRNOS_VT03Init(&vt03,&c))return -1;}
 SRNOS_RC_InputInit();SRNOS_RC_SetSourcePriority(cfg->primary,cfg->secondary);SRNOS_RC_SetTimeouts(cfg->idle_ms,cfg->offline_ms);
 SRNOS_RC_SetStickDeadzone(cfg->stick_deadzone);SRNOS_RC_RegisterPostMapHook(cfg->post_map_hook);
 SRNOS_RemoteControlFreeze();configured=true;return 0;
}
static void remote_step(void *arg) {
 (void)arg;uint32_t now=(uint32_t)((uint64_t)osKernelGetTickCount()*1000/osKernelGetTickFreq());
 SRNOS_DR16Dec d;if(dr16.channel&&SRNOS_DR16PollDec(&dr16,&d,now))update_from_dr16(&d,now);
 SRNOS_VT03Dec v;if(vt03.channel&&SRNOS_VT03PollDec(&vt03,&v,now))update_from_vt03(&v,now);
 SRNOS_RC_Tick(now);
}
static bool remote_wait(void *arg){(void)arg;osDelay(1);return true;}
static void remote_worker(void *arg) {
 (void)arg;if(dr16.channel)SRNOS_DR16Start(&dr16);if(vt03.channel)SRNOS_VT03Start(&vt03);
 const SRNOS_InternalLoop loop={remote_step,remote_wait,NULL,0,false};SRNOS_KernelRunLoop(&loop);
}
int SRNOS_RemoteControlStart(void) {
 if(started||!configured||osKernelGetState()!=osKernelReady)return -1;started=true;
 if(!dr16.channel&&!vt03.channel)return 0;
 const osThreadAttr_t attr={.name="SRNOS_RC",.stack_size=3072,.priority=osPriorityHigh7};
 return SRNOS_KernelThreadNew(remote_worker,NULL,&attr)?0:-1;
}
