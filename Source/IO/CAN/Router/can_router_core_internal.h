#ifndef SRNOS_CAN_ROUTER_CORE_INTERNAL_H
#define SRNOS_CAN_ROUTER_CORE_INTERNAL_H

#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRNOS_CAN_ROUTER_TX_CLASS_URGENT = 0,
    SRNOS_CAN_ROUTER_TX_CLASS_PERIODIC = 1,
} SRNOS_CANRouterTxClass;

typedef struct {
    bool (*start)(SRNOS_CANChannel ch);
    uint8_t (*get_tx_free)(SRNOS_CANChannel ch);
    bool (*try_push)(SRNOS_CANChannel ch, const SRNOS_CANFrame *frame, SRNOS_CANRouterTxClass tx_class);
    void (*signal_tx)(SRNOS_CANChannel ch);
    uint32_t (*now_ms)(void);
    /* Optional pure/short read: retry deadline after transient backend I/O
     * rejection, UINT32_MAX otherwise. Called only with pending oneshots. */
    uint32_t (*retry_wait_ms)(SRNOS_CANChannel ch, uint32_t now_ms);
} SRNOS_CANRouterBackendOps;

bool SRNOS_CANRouterCoreConfigurationCompatible(SRNOS_CANChannel ch, const SRNOS_CANControllerConfig *config);
void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel ch);
void SRNOS_CAN_FreezeConfiguration(void);
bool SRNOS_CAN_ConfigurationFrozen(void);
void SRNOS_CAN_PrepareResources(void);
void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel ch, const SRNOS_CANRouterBackendOps *ops);
void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel ch, const SRNOS_CANFrame *frame, uint32_t now_ms);
/* Remaining ms until work, UINT32_MAX when idle. Adapter urgent poll/tick
 * retains its existing 1 ms contract; TX-complete and enqueue wake earlier. */
uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel ch, uint32_t now_ms);
bool SRNOS_CANRouterCoreStepTx(SRNOS_CANChannel ch, uint32_t now_ms);
void SRNOS_CANRouterCoreDrainTx(SRNOS_CANChannel ch, uint32_t now_ms);
void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel ch, bool by_isr);
void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel ch);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_CAN_ROUTER_CORE_INTERNAL_H */
