#ifndef SRNOS_CAN_FD_INTERNAL_H
#define SRNOS_CAN_FD_INTERNAL_H
#include "SirinEmbedOS/IO/CAN/FD/CANFD.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t (*get_tx_free)(SRNOS_CANChannel channel);
    bool (*try_push)(SRNOS_CANChannel channel, const SRNOS_CANFDFrame *frame);
    void (*signal_tx)(SRNOS_CANChannel channel);
    uint32_t (*now_ms)(void);
    uint32_t (*retry_wait_ms)(SRNOS_CANChannel channel, uint32_t now_ms);
} SRNOS_CANFDBackendOps;
bool SRNOS_CANFDCoreHasRegistrations(SRNOS_CANChannel channel);
void SRNOS_CANFDCoreFreeze(void);
void SRNOS_CANFDCorePrepareResources(void);
void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel channel, const SRNOS_CANFDBackendOps *ops);
void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel channel, const SRNOS_CANFDFrame *frame, uint32_t now_ms);
void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel channel);
void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel channel, bool by_isr);
void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel channel);
uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel channel, uint32_t now_ms);
bool SRNOS_CANFDCoreStepTx(SRNOS_CANChannel channel, uint32_t now_ms);
void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel channel, uint32_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
