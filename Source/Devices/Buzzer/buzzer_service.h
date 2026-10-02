#pragma once
#include "SirinEmbedOS/Devices/Buzzer/Buzzer.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t (*SRNOS_BuzzerNowMsHook)(void *ctx);
typedef uint32_t (*SRNOS_BuzzerEnterHook)(void *ctx);
typedef void (*SRNOS_BuzzerExitHook)(void *ctx, uint32_t token);

typedef struct {
    SRNOS_BuzzerNowMsHook now_ms;
    SRNOS_BuzzerEnterHook enter;
    SRNOS_BuzzerExitHook exit;
    SRNOS_BuzzerEnterHook enter_from_isr;
    SRNOS_BuzzerExitHook exit_from_isr;
} SRNOS_BuzzerPortHooks;

typedef struct {
    SRNOS_BuzzerPortHooks hooks;
    void *hook_ctx;
} SRNOS_BuzzerConfig;

/* Board initialization and system worker only; not parent application API. */
SRNOS_BuzzerStatus SRNOS_BuzzerInit(const SRNOS_BuzzerConfig *cfg);
SRNOS_BuzzerStatus SRNOS_BuzzerService(void);
typedef void (*SRNOS_BuzzerWakeFn)(bool from_isr);
void SRNOS_BuzzerSetServiceWake(SRNOS_BuzzerWakeFn wake);
uint32_t SRNOS_BuzzerServiceNextWait(uint32_t now_ms);
#ifdef __cplusplus
}
#endif
