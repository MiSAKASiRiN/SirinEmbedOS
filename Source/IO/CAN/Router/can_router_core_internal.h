#ifndef H723VG_V2_FREERTOS_CAN_ROUTER_CORE_INTERNAL_H
#define H723VG_V2_FREERTOS_CAN_ROUTER_CORE_INTERNAL_H

#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAN_ROUTER_TX_CLASS_URGENT = 0,
    CAN_ROUTER_TX_CLASS_PERIODIC = 1,
} can_router_tx_class_t;

typedef struct {
    bool (*start)(can_channel_t ch);
    uint8_t (*get_tx_free)(can_channel_t ch);
    bool (*try_push)(can_channel_t ch, const can_frame_t *frame, can_router_tx_class_t tx_class);
    void (*signal_tx)(can_channel_t ch);
    uint32_t (*now_ms)(void);
} can_router_backend_ops_t;

void can_router_core_set_backend(can_channel_t ch, const can_router_backend_ops_t *ops);
void can_router_core_on_rx_frame(can_channel_t ch, const can_frame_t *frame, uint32_t now_ms);
void can_router_core_drain_tx(can_channel_t ch, uint32_t now_ms);
void can_router_core_note_rx_wake(can_channel_t ch, bool by_isr);
void can_router_core_note_tx_complete(can_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* H723VG_V2_FREERTOS_CAN_ROUTER_CORE_INTERNAL_H */
