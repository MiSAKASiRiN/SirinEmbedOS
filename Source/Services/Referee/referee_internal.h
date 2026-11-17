#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"

#define SRNOS_REFEREE_WORKER_STACK_BYTES 3072u
#define SRNOS_REFEREE_WORKER_RESERVE_BYTES 1024u

bool SRNOS_RefereeSerialConfigValid(const SRNOS_SerialConfig *config);
uint32_t SRNOS_RefereeHeapBudget(bool enabled);
