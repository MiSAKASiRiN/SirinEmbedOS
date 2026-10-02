#include "SirinEmbedOS/Services/RemoteControl/RemoteControl.h"
#include "FreeRTOS.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "task.h"
#include <string.h>

/* Startup configuration precedes osKernelInitialize. FreeRTOS task critical
 * nesting starts at a sentinel value, so preserve the CPU mask before startup. */
#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
static uint32_t SRNOS_RemoteControlBootLockDepth,SRNOS_RemoteControlBootLockKey;
static void SRNOS_RemoteControlEnterCritical(void) {
 if(xTaskGetSchedulerState()==taskSCHEDULER_NOT_STARTED){
  if(SRNOS_RemoteControlBootLockDepth++==0){SRNOS_RemoteControlBootLockKey=__get_PRIMASK();__disable_irq();}
 }else taskENTER_CRITICAL();
}
static void SRNOS_RemoteControlExitCritical(void) {
 if(SRNOS_RemoteControlBootLockDepth){if(--SRNOS_RemoteControlBootLockDepth==0)__set_PRIMASK(SRNOS_RemoteControlBootLockKey);}
 else taskEXIT_CRITICAL();
}
#define SRNOS_RC_ENTER_CRITICAL() SRNOS_RemoteControlEnterCritical()
#define SRNOS_RC_EXIT_CRITICAL() SRNOS_RemoteControlExitCritical()
#else
#define SRNOS_RC_ENTER_CRITICAL() taskENTER_CRITICAL()
#define SRNOS_RC_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif

typedef struct {
    SRNOS_RC_State last;
    uint32_t last_ok_ms;
    uint8_t seen;
} src_slot_t;

static src_slot_t g_src[SRNOS_RC_SOURCE_COUNT];
static bool configuration_frozen;
void SRNOS_RemoteControlFreeze(void) { configuration_frozen=true; }
static uint16_t g_stick_deadzone = 10u;
static SRNOS_RemoteControlSource g_pri[2] = {SRNOS_RC_PRI_DEFAULT_PRIMARY, SRNOS_RC_PRI_DEFAULT_SECONDARY};
static uint16_t g_idle_ms = 50u;
static uint16_t g_offline_ms = 300u;
volatile SRNOS_RC_State SRNOS_RemoteControlState;
static SRNOS_RC_Status g_status;
static SRNOS_RemoteControlPostMapHook g_post_map_hook = NULL;
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgRemoteControl SRNOS_DebugDbgRemoteControl;

static void dbg_sync_locked(uint32_t tick, uint32_t status)
{
    volatile SRNOS_DbgRemoteControl* dbg = &SRNOS_DebugDbgRemoteControl;

    SRNOS_DbgBegin(&dbg->h);
    dbg->active_src = (uint32_t)g_status.active_src;
    dbg->online = g_status.online;
    dbg->idle = g_status.idle;
    dbg->blocked = g_status.blocked;
    dbg->sticks_idle = g_status.sticks_idle;
    dbg->SRNOS_DR16Online = g_status.SRNOS_DR16Online;
    dbg->SRNOS_VT03Online = g_status.SRNOS_VT03Online;
    dbg->last_ok_ms = g_status.last_ok_ms;
    dbg->priority_primary = (uint32_t)g_pri[0];
    dbg->priority_secondary = (uint32_t)g_pri[1];
    for (uint32_t i = 0u; i < SRNOS_RC_CH_COUNT; ++i) {
        dbg->ch[i] = SRNOS_RemoteControlState.rc.ch[i];
    }
    dbg->sw[0] = SRNOS_RemoteControlState.rc.s[0];
    dbg->sw[1] = SRNOS_RemoteControlState.rc.s[1];
    dbg->sw_valid_mask = SRNOS_RemoteControlState.rc.s_valid_mask;
    dbg->key_v = SRNOS_RemoteControlState.key.v;
    dbg->key_v_ext = SRNOS_RemoteControlState.key.v_ext;
    SRNOS_DbgEnd(&dbg->h, tick, status);
}

static inline uint8_t source_valid(SRNOS_RemoteControlSource src)
{
    return (src > SRNOS_RC_SRC_NONE && src < SRNOS_RC_SOURCE_COUNT) ? 1u : 0u;
}

static inline SRNOS_RemoteControlSource sanitize_source(SRNOS_RemoteControlSource src)
{
    return source_valid(src) ? src : SRNOS_RC_SRC_NONE;
}

static inline uint8_t online_with_dt(uint32_t dt_ms)
{
    return (dt_ms <= g_offline_ms) ? 1u : 0u;
}

static inline uint8_t idle_with_dt(uint32_t dt_ms)
{
    return (dt_ms > g_idle_ms) ? 1u : 0u;
}

static uint8_t sticks_idle_with(const SRNOS_RC_State* s)
{
    for (uint32_t i = 0u; i < SRNOS_RC_CH_COUNT; ++i) {
        const int16_t ch = s->rc.ch[i];
        if (ch > (int16_t)g_stick_deadzone || ch < -(int16_t)g_stick_deadzone) {
            return 0u;
        }
    }
    return 1u;
}

static SRNOS_RemoteControlSource select_active_locked(uint32_t now_ms)
{
    for (uint32_t i = 0u; i < 2u; ++i) {
        const SRNOS_RemoteControlSource src = sanitize_source(g_pri[i]);
        if (src == SRNOS_RC_SRC_NONE) {
            continue;
        }
        const uint32_t dt = now_ms - g_src[src].last_ok_ms;
        if (g_src[src].seen && online_with_dt(dt)) {
            return src;
        }
    }
    return SRNOS_RC_SRC_NONE;
}

static void update_source_online_locked(uint32_t now_ms)
{
    const uint32_t dt_dr16 = now_ms - g_src[SRNOS_RC_SRC_DR16].last_ok_ms;
    const uint32_t dt_vt03 = now_ms - g_src[SRNOS_RC_SRC_VT03].last_ok_ms;
    g_status.SRNOS_DR16Online = (g_src[SRNOS_RC_SRC_DR16].seen && online_with_dt(dt_dr16)) ? 1u : 0u;
    g_status.SRNOS_VT03Online = (g_src[SRNOS_RC_SRC_VT03].seen && online_with_dt(dt_vt03)) ? 1u : 0u;
}

static void publish_zero_locked(void)
{
    SRNOS_RC_State zero;
    memset(&zero, 0, sizeof(zero));
    SRNOS_RemoteControlState = zero;
}

static void refresh_locked(uint32_t now_ms)
{
    update_source_online_locked(now_ms);

    const SRNOS_RemoteControlSource sel = select_active_locked(now_ms);
    g_status.active_src = sel;

    if (sel == SRNOS_RC_SRC_NONE) {
        publish_zero_locked();
        g_status.online = 0u;
        g_status.idle = 1u;
        g_status.blocked = 1u;
        g_status.last_ok_ms = 0u;
        g_status.sticks_idle = 1u;
        return;
    }

    const uint32_t dt = now_ms - g_src[sel].last_ok_ms;
    SRNOS_RemoteControlState = g_src[sel].last;
    g_status.online = online_with_dt(dt);
    g_status.idle = idle_with_dt(dt);
    g_status.blocked = 0u;
    g_status.last_ok_ms = g_src[sel].last_ok_ms;
    g_status.sticks_idle = sticks_idle_with(&g_src[sel].last);
}

void SRNOS_RC_InputInit(void)
{
    if(configuration_frozen)return;
    SRNOS_RC_ENTER_CRITICAL();
    memset(g_src, 0, sizeof(g_src));
    publish_zero_locked();
    memset(&g_status, 0, sizeof(g_status));
    g_pri[0] = SRNOS_RC_PRI_DEFAULT_PRIMARY;
    g_pri[1] = SRNOS_RC_PRI_DEFAULT_SECONDARY;
    g_idle_ms = 50u;
    g_offline_ms = 300u;
    g_stick_deadzone = 10u;
    g_post_map_hook = NULL;
    g_status.active_src = SRNOS_RC_SRC_NONE;
    g_status.idle = 1u;
    g_status.blocked = 1u;
    g_status.sticks_idle = 1u;
    SRNOS_DbgZero(&SRNOS_DebugDbgRemoteControl, sizeof(SRNOS_DebugDbgRemoteControl));
    SRNOS_DbgInit(&SRNOS_DebugDbgRemoteControl.h,
                 SRNOS_DBG_MODULE_REMOTE_CONTROL,
                 0u,
                 (uint16_t)sizeof(SRNOS_DebugDbgRemoteControl));
    dbg_sync_locked(0u, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

void SRNOS_RC_SetSourcePriority(SRNOS_RemoteControlSource primary, SRNOS_RemoteControlSource secondary)
{
    if(configuration_frozen)return;
    primary = sanitize_source(primary);
    secondary = sanitize_source(secondary);
    if (primary == secondary) {
        secondary = SRNOS_RC_SRC_NONE;
    }

    SRNOS_RC_ENTER_CRITICAL();
    g_pri[0] = primary;
    g_pri[1] = secondary;
    dbg_sync_locked(0u, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

void SRNOS_RC_SetTimeouts(uint16_t idle_ms, uint16_t offline_ms)
{
    if(configuration_frozen)return;
    if (idle_ms == 0u) {
        idle_ms = 1u;
    }
    if (offline_ms <= idle_ms) {
        offline_ms = (uint16_t)(idle_ms + 1u);
    }

    SRNOS_RC_ENTER_CRITICAL();
    g_idle_ms = idle_ms;
    g_offline_ms = offline_ms;
    dbg_sync_locked(0u, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

void SRNOS_RC_UpdateFromSource(SRNOS_RemoteControlSource src, const SRNOS_RC_State* s, uint32_t now_ms)
{
    src = sanitize_source(src);
    if (src == SRNOS_RC_SRC_NONE || s == NULL) {
        return;
    }

    SRNOS_RemoteControlPostMapHook hook;
    SRNOS_RC_ENTER_CRITICAL();
    hook = g_post_map_hook;
    SRNOS_RC_EXIT_CRITICAL();

    SRNOS_RC_State tmp = *s;
    if (hook != NULL) {
        hook(&tmp);
    }

    SRNOS_RC_ENTER_CRITICAL();
    g_src[src].last = tmp;
    g_src[src].last_ok_ms = now_ms;
    g_src[src].seen = 1u;
    SRNOS_DebugDbgRemoteControl.source_update_count++;
    refresh_locked(now_ms);
    dbg_sync_locked(now_ms, (uint32_t)src);
    SRNOS_RC_EXIT_CRITICAL();
}

void SRNOS_RC_Tick(uint32_t now_ms)
{
    SRNOS_RC_ENTER_CRITICAL();
    SRNOS_DebugDbgRemoteControl.tick_count++;
    refresh_locked(now_ms);
    dbg_sync_locked(now_ms, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

void SRNOS_RC_GetSnapshot(SRNOS_RC_State* out)
{
    if (out == NULL) {
        return;
    }

    SRNOS_RC_ENTER_CRITICAL();
    *out = SRNOS_RemoteControlState;
    SRNOS_DebugDbgRemoteControl.snapshot_get_count++;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

SRNOS_RC_Status SRNOS_RC_GetStatus(void)
{
    SRNOS_RC_Status out;
    SRNOS_RC_ENTER_CRITICAL();
    out = g_status;
    SRNOS_DebugDbgRemoteControl.status_get_count++;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    SRNOS_RC_EXIT_CRITICAL();
    return out;
}

int SRNOS_RC_GetSourceInfo(SRNOS_RemoteControlSource src, SRNOS_RC_SourceInfo* out)
{
    src = sanitize_source(src);
    if (src == SRNOS_RC_SRC_NONE || out == NULL) {
        return 0;
    }

    SRNOS_RC_ENTER_CRITICAL();
    out->seen = g_src[src].seen;
    out->last_ok_ms = g_src[src].last_ok_ms;
    SRNOS_RC_EXIT_CRITICAL();
    return 1;
}

void SRNOS_RC_SetStickDeadzone(uint16_t dz)
{
    if(configuration_frozen)return;
    if (dz > 2000u) {
        dz = 2000u;
    }

    SRNOS_RC_ENTER_CRITICAL();
    g_stick_deadzone = dz;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}

int SRNOS_RC_IsSticksIdle(void)
{
    int ret;
    SRNOS_RC_ENTER_CRITICAL();
    ret = g_status.sticks_idle ? 1 : 0;
    SRNOS_RC_EXIT_CRITICAL();
    return ret;
}

void SRNOS_RC_RegisterPostMapHook(SRNOS_RemoteControlPostMapHook hook)
{
    if(configuration_frozen)return;
    SRNOS_RC_ENTER_CRITICAL();
    g_post_map_hook = hook;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    SRNOS_RC_EXIT_CRITICAL();
}
