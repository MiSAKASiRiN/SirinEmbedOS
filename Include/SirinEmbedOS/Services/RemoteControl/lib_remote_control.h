#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Unified remote-control contract.
 *
 * Channel order is physical and source-independent:
 *   ch0 right stick horizontal, ch1 right stick vertical,
 *   ch2 left stick vertical,   ch3 left stick horizontal,
 *   ch4 auxiliary dial/wheel when available.
 */

#define RC_CH_RIGHT_X            0u
#define RC_CH_RIGHT_Y            1u
#define RC_CH_LEFT_Y             2u
#define RC_CH_LEFT_X             3u
#define RC_CH_AUX                4u
#define RC_CH_COUNT              5u

#define RC_SW_UP                 ((uint8_t)1)
#define RC_SW_MID                ((uint8_t)3)
#define RC_SW_DOWN               ((uint8_t)2)
#define RC_SW_UNAVAILABLE        ((uint8_t)0)
#define switch_is_up(s)          ((s) == RC_SW_UP)
#define switch_is_mid(s)         ((s) == RC_SW_MID)
#define switch_is_down(s)        ((s) == RC_SW_DOWN)

#define RC_CH_VALUE_MIN          ((uint16_t)364)
#define RC_CH_VALUE_OFFSET       ((uint16_t)1024)
#define RC_CH_VALUE_MAX          ((uint16_t)1684)

/* Canonical keyboard order follows VT03/VT13 and normal PC layout. */
#define KEY_PRESSED_OFFSET_W      ((uint16_t)1 << 0)
#define KEY_PRESSED_OFFSET_S      ((uint16_t)1 << 1)
#define KEY_PRESSED_OFFSET_A      ((uint16_t)1 << 2)
#define KEY_PRESSED_OFFSET_D      ((uint16_t)1 << 3)
#define KEY_PRESSED_OFFSET_SHIFT  ((uint16_t)1 << 4)
#define KEY_PRESSED_OFFSET_CTRL   ((uint16_t)1 << 5)
#define KEY_PRESSED_OFFSET_Q      ((uint16_t)1 << 6)
#define KEY_PRESSED_OFFSET_E      ((uint16_t)1 << 7)
#define KEY_PRESSED_OFFSET_R      ((uint16_t)1 << 8)
#define KEY_PRESSED_OFFSET_F      ((uint16_t)1 << 9)
#define KEY_PRESSED_OFFSET_G      ((uint16_t)1 << 10)
#define KEY_PRESSED_OFFSET_Z      ((uint16_t)1 << 11)
#define KEY_PRESSED_OFFSET_X      ((uint16_t)1 << 12)
#define KEY_PRESSED_OFFSET_C      ((uint16_t)1 << 13)
#define KEY_PRESSED_OFFSET_V      ((uint16_t)1 << 14)
#define KEY_PRESSED_OFFSET_B      ((uint16_t)1 << 15)

#define KEY_EXT_VT03_PAUSE        ((uint16_t)1 << 0)
#define KEY_EXT_VT03_BTN_L        ((uint16_t)1 << 1)
#define KEY_EXT_VT03_BTN_R        ((uint16_t)1 << 2)
#define KEY_EXT_VT03_TRIGGER      ((uint16_t)1 << 3)

typedef struct {
    struct {
        int16_t ch[RC_CH_COUNT];
        uint8_t s[2];
        uint8_t s_valid_mask;   /* bit0 -> s[0], bit1 -> s[1] */
        uint8_t reserved;
    } rc;
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
        uint8_t press_l;
        uint8_t press_r;
        uint8_t press_m;
        uint8_t reserved;
    } mouse;
    struct {
        uint16_t v;
        uint16_t v_ext;
    } key;
} RC_State;

#if defined(__cplusplus)
static_assert(offsetof(RC_State, key) % alignof(uint16_t) == 0, "RC_State key must be halfword aligned");
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(offsetof(RC_State, key) % _Alignof(uint16_t) == 0, "RC_State key must be halfword aligned");
_Static_assert(offsetof(RC_State, mouse) % _Alignof(int16_t) == 0, "RC_State mouse must be halfword aligned");
#endif

typedef enum {
    RC_SRC_NONE = 0,
    RC_SRC_DR16 = 1,
    RC_SRC_VT03 = 2,
    RC_SOURCE_COUNT = 3,
} rc_source_t;

#define RC_PRI_DEFAULT_PRIMARY    RC_SRC_DR16
#define RC_PRI_DEFAULT_SECONDARY  RC_SRC_VT03

typedef struct {
    uint8_t online;
    uint8_t idle;
    uint8_t blocked;
    rc_source_t active_src;
    uint32_t last_ok_ms;
    uint8_t sticks_idle;
    uint8_t dr16_online;
    uint8_t vt03_online;
} RC_Status;

typedef struct {
    uint8_t seen;
    uint32_t last_ok_ms;
} RC_SourceInfo;

extern volatile RC_State g_rc_unified;

void RC_InputInit(void);
void RC_SetSourcePriority(rc_source_t primary, rc_source_t secondary);
void RC_SetTimeouts(uint16_t idle_ms, uint16_t offline_ms);
void RC_UpdateFromSource(rc_source_t src, const RC_State* s, uint32_t now_ms);
void RC_Tick(uint32_t now_ms);
void RC_GetSnapshot(RC_State* out);
RC_Status RC_GetStatus(void);
int  RC_GetSourceInfo(rc_source_t src, RC_SourceInfo* out);
void RC_SetStickDeadzone(uint16_t dz);
int  RC_IsSticksIdle(void);

typedef void (*rc_post_map_hook_t)(RC_State* s);
void RC_RegisterPostMapHook(rc_post_map_hook_t hook);

#ifdef __cplusplus
}
#endif
