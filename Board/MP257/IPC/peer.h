#ifndef SRNOS_MP257_IPC_PEER_H
#define SRNOS_MP257_IPC_PEER_H
#include "abi.h"

void SRNOS_IpcInit(void);
void SRNOS_IpcPoll(unsigned now_ms);
void SRNOS_IpcPublishRadio(const SRNOS_IpcRadio *radio);
void SRNOS_IpcPublishMotor(unsigned now_ms);
void SRNOS_IpcPublishStatus(SRNOS_IpcStatus *status);
#endif
