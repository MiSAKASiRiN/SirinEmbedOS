#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"

#define SRNOS_REMOTE_WORKER_STACK_BYTES 3072u
#define SRNOS_REMOTE_WORKER_RESERVE_BYTES 1024u

/* Pure protocol requirements, shared by startup validation and binding. */
bool SRNOS_DR16SerialConfigValid(const SRNOS_SerialConfig *config);
bool SRNOS_VT03SerialConfigValid(const SRNOS_SerialConfig *config);
bool SRNOS_WBusSerialConfigValid(const SRNOS_SerialConfig *config);
bool SRNOS_VT02SerialConfigValid(const SRNOS_SerialConfig *config);
uint32_t SRNOS_RemoteControlHeapBudget(bool worker_enabled);
