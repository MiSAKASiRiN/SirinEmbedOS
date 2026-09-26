#ifndef H723VG_V2_FREERTOS_NTFDCAN_BACKEND_INTERNAL_H
#define H723VG_V2_FREERTOS_NTFDCAN_BACKEND_INTERNAL_H

#include "can_router_core_internal.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NTFDCAN_EVT_RX (1u << 0)
#define NTFDCAN_EVT_TX (1u << 1)

typedef struct {
    osEventFlagsId_t rx_evt;
    osEventFlagsId_t tx_evt;
    volatile uint32_t pending_rx;
    volatile uint32_t pending_tx;
} ntfdcan_task_ctx_t;

ntfdcan_task_ctx_t *ntfdcan_task_ctx_of(can_channel_t ch);
void ntfdcan_backend_ensure_bound(can_channel_t ch);
void ntfdcan_backend_signal_rx(can_channel_t ch);
void ntfdcan_backend_signal_tx(can_channel_t ch);
uint32_t ntfdcan_backend_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* H723VG_V2_FREERTOS_NTFDCAN_BACKEND_INTERNAL_H */
