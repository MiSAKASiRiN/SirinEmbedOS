#include "SirinEmbedOS/Services/RemoteControl/lib_remote_control.h"
#include "FreeRTOS.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "task.h"
#include <string.h>

/* Startup configuration precedes osKernelInitialize. FreeRTOS task critical
 * nesting starts at a sentinel value, so preserve the CPU mask before startup. */
#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
static uint32_t rc_boot_lock_depth,rc_boot_lock_key;
static void rc_enter_critical(void) {
 if(xTaskGetSchedulerState()==taskSCHEDULER_NOT_STARTED){
  if(rc_boot_lock_depth++==0){rc_boot_lock_key=__get_PRIMASK();__disable_irq();}
 }else taskENTER_CRITICAL();
}
static void rc_exit_critical(void) {
 if(rc_boot_lock_depth){if(--rc_boot_lock_depth==0)__set_PRIMASK(rc_boot_lock_key);}
 else taskEXIT_CRITICAL();
}
#define RC_ENTER_CRITICAL() rc_enter_critical()
#define RC_EXIT_CRITICAL() rc_exit_critical()
#else
#define RC_ENTER_CRITICAL() taskENTER_CRITICAL()
#define RC_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif

typedef struct {
    RC_State last;
    uint32_t last_ok_ms;
    uint8_t seen;
} src_slot_t;

static src_slot_t g_src[RC_SOURCE_COUNT];
static bool configuration_frozen;
void SRNOS_RemoteControlFreeze(void) { configuration_frozen=true; }
static uint16_t g_stick_deadzone = 10u;
static rc_source_t g_pri[2] = {RC_PRI_DEFAULT_PRIMARY, RC_PRI_DEFAULT_SECONDARY};
static uint16_t g_idle_ms = 50u;
static uint16_t g_offline_ms = 300u;
volatile RC_State g_rc_unified;
static RC_Status g_status;
static rc_post_map_hook_t g_post_map_hook = NULL;
OMX_DEBUG_WATCH_BSS volatile omx_dbg_remote_control_t g_omx_dbg_remote_control;

static void dbg_sync_locked(uint32_t tick, uint32_t status)
{
    volatile omx_dbg_remote_control_t* dbg = &g_omx_dbg_remote_control;

    omx_dbg_begin(&dbg->h);
    dbg->active_src = (uint32_t)g_status.active_src;
    dbg->online = g_status.online;
    dbg->idle = g_status.idle;
    dbg->blocked = g_status.blocked;
    dbg->sticks_idle = g_status.sticks_idle;
    dbg->dr16_online = g_status.dr16_online;
    dbg->vt03_online = g_status.vt03_online;
    dbg->last_ok_ms = g_status.last_ok_ms;
    dbg->priority_primary = (uint32_t)g_pri[0];
    dbg->priority_secondary = (uint32_t)g_pri[1];
    for (uint32_t i = 0u; i < RC_CH_COUNT; ++i) {
        dbg->ch[i] = g_rc_unified.rc.ch[i];
    }
    dbg->sw[0] = g_rc_unified.rc.s[0];
    dbg->sw[1] = g_rc_unified.rc.s[1];
    dbg->sw_valid_mask = g_rc_unified.rc.s_valid_mask;
    dbg->key_v = g_rc_unified.key.v;
    dbg->key_v_ext = g_rc_unified.key.v_ext;
    omx_dbg_end(&dbg->h, tick, status);
}

static inline uint8_t source_valid(rc_source_t src)
{
    return (src > RC_SRC_NONE && src < RC_SOURCE_COUNT) ? 1u : 0u;
}

static inline rc_source_t sanitize_source(rc_source_t src)
{
    return source_valid(src) ? src : RC_SRC_NONE;
}

static inline uint8_t online_with_dt(uint32_t dt_ms)
{
    return (dt_ms <= g_offline_ms) ? 1u : 0u;
}

static inline uint8_t idle_with_dt(uint32_t dt_ms)
{
    return (dt_ms > g_idle_ms) ? 1u : 0u;
}

static uint8_t sticks_idle_with(const RC_State* s)
{
    for (uint32_t i = 0u; i < RC_CH_COUNT; ++i) {
        const int16_t ch = s->rc.ch[i];
        if (ch > (int16_t)g_stick_deadzone || ch < -(int16_t)g_stick_deadzone) {
            return 0u;
        }
    }
    return 1u;
}

static rc_source_t select_active_locked(uint32_t now_ms)
{
    for (uint32_t i = 0u; i < 2u; ++i) {
        const rc_source_t src = sanitize_source(g_pri[i]);
        if (src == RC_SRC_NONE) {
            continue;
        }
        const uint32_t dt = now_ms - g_src[src].last_ok_ms;
        if (g_src[src].seen && online_with_dt(dt)) {
            return src;
        }
    }
    return RC_SRC_NONE;
}

static void update_source_online_locked(uint32_t now_ms)
{
    const uint32_t dt_dr16 = now_ms - g_src[RC_SRC_DR16].last_ok_ms;
    const uint32_t dt_vt03 = now_ms - g_src[RC_SRC_VT03].last_ok_ms;
    g_status.dr16_online = (g_src[RC_SRC_DR16].seen && online_with_dt(dt_dr16)) ? 1u : 0u;
    g_status.vt03_online = (g_src[RC_SRC_VT03].seen && online_with_dt(dt_vt03)) ? 1u : 0u;
}

static void publish_zero_locked(void)
{
    RC_State zero;
    memset(&zero, 0, sizeof(zero));
    g_rc_unified = zero;
}

static void refresh_locked(uint32_t now_ms)
{
    update_source_online_locked(now_ms);

    const rc_source_t sel = select_active_locked(now_ms);
    g_status.active_src = sel;

    if (sel == RC_SRC_NONE) {
        publish_zero_locked();
        g_status.online = 0u;
        g_status.idle = 1u;
        g_status.blocked = 1u;
        g_status.last_ok_ms = 0u;
        g_status.sticks_idle = 1u;
        return;
    }

    const uint32_t dt = now_ms - g_src[sel].last_ok_ms;
    g_rc_unified = g_src[sel].last;
    g_status.online = online_with_dt(dt);
    g_status.idle = idle_with_dt(dt);
    g_status.blocked = 0u;
    g_status.last_ok_ms = g_src[sel].last_ok_ms;
    g_status.sticks_idle = sticks_idle_with(&g_src[sel].last);
}

void RC_InputInit(void)
{
    if(configuration_frozen)return;
    RC_ENTER_CRITICAL();
    memset(g_src, 0, sizeof(g_src));
    publish_zero_locked();
    memset(&g_status, 0, sizeof(g_status));
    g_pri[0] = RC_PRI_DEFAULT_PRIMARY;
    g_pri[1] = RC_PRI_DEFAULT_SECONDARY;
    g_idle_ms = 50u;
    g_offline_ms = 300u;
    g_stick_deadzone = 10u;
    g_post_map_hook = NULL;
    g_status.active_src = RC_SRC_NONE;
    g_status.idle = 1u;
    g_status.blocked = 1u;
    g_status.sticks_idle = 1u;
    omx_dbg_zero(&g_omx_dbg_remote_control, sizeof(g_omx_dbg_remote_control));
    omx_dbg_init(&g_omx_dbg_remote_control.h,
                 OMX_DBG_MODULE_REMOTE_CONTROL,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_remote_control));
    dbg_sync_locked(0u, 0u);
    RC_EXIT_CRITICAL();
}

void RC_SetSourcePriority(rc_source_t primary, rc_source_t secondary)
{
    if(configuration_frozen)return;
    primary = sanitize_source(primary);
    secondary = sanitize_source(secondary);
    if (primary == secondary) {
        secondary = RC_SRC_NONE;
    }

    RC_ENTER_CRITICAL();
    g_pri[0] = primary;
    g_pri[1] = secondary;
    dbg_sync_locked(0u, 0u);
    RC_EXIT_CRITICAL();
}

void RC_SetTimeouts(uint16_t idle_ms, uint16_t offline_ms)
{
    if(configuration_frozen)return;
    if (idle_ms == 0u) {
        idle_ms = 1u;
    }
    if (offline_ms <= idle_ms) {
        offline_ms = (uint16_t)(idle_ms + 1u);
    }

    RC_ENTER_CRITICAL();
    g_idle_ms = idle_ms;
    g_offline_ms = offline_ms;
    dbg_sync_locked(0u, 0u);
    RC_EXIT_CRITICAL();
}

void RC_UpdateFromSource(rc_source_t src, const RC_State* s, uint32_t now_ms)
{
    src = sanitize_source(src);
    if (src == RC_SRC_NONE || s == NULL) {
        return;
    }

    rc_post_map_hook_t hook;
    RC_ENTER_CRITICAL();
    hook = g_post_map_hook;
    RC_EXIT_CRITICAL();

    RC_State tmp = *s;
    if (hook != NULL) {
        hook(&tmp);
    }

    RC_ENTER_CRITICAL();
    g_src[src].last = tmp;
    g_src[src].last_ok_ms = now_ms;
    g_src[src].seen = 1u;
    g_omx_dbg_remote_control.source_update_count++;
    refresh_locked(now_ms);
    dbg_sync_locked(now_ms, (uint32_t)src);
    RC_EXIT_CRITICAL();
}

void RC_Tick(uint32_t now_ms)
{
    RC_ENTER_CRITICAL();
    g_omx_dbg_remote_control.tick_count++;
    refresh_locked(now_ms);
    dbg_sync_locked(now_ms, 0u);
    RC_EXIT_CRITICAL();
}

void RC_GetSnapshot(RC_State* out)
{
    if (out == NULL) {
        return;
    }

    RC_ENTER_CRITICAL();
    *out = g_rc_unified;
    g_omx_dbg_remote_control.snapshot_get_count++;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    RC_EXIT_CRITICAL();
}

RC_Status RC_GetStatus(void)
{
    RC_Status out;
    RC_ENTER_CRITICAL();
    out = g_status;
    g_omx_dbg_remote_control.status_get_count++;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    RC_EXIT_CRITICAL();
    return out;
}

int RC_GetSourceInfo(rc_source_t src, RC_SourceInfo* out)
{
    src = sanitize_source(src);
    if (src == RC_SRC_NONE || out == NULL) {
        return 0;
    }

    RC_ENTER_CRITICAL();
    out->seen = g_src[src].seen;
    out->last_ok_ms = g_src[src].last_ok_ms;
    RC_EXIT_CRITICAL();
    return 1;
}

void RC_SetStickDeadzone(uint16_t dz)
{
    if(configuration_frozen)return;
    if (dz > 2000u) {
        dz = 2000u;
    }

    RC_ENTER_CRITICAL();
    g_stick_deadzone = dz;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    RC_EXIT_CRITICAL();
}

int RC_IsSticksIdle(void)
{
    int ret;
    RC_ENTER_CRITICAL();
    ret = g_status.sticks_idle ? 1 : 0;
    RC_EXIT_CRITICAL();
    return ret;
}

void RC_RegisterPostMapHook(rc_post_map_hook_t hook)
{
    if(configuration_frozen)return;
    RC_ENTER_CRITICAL();
    g_post_map_hook = hook;
    dbg_sync_locked(g_status.last_ok_ms, 0u);
    RC_EXIT_CRITICAL();
}
