#pragma once
#include "SirinEmbedOS/Services/Blackbox.h"
#define SRNOS_BLACKBOX_STACK_BYTES 4096u
#define SRNOS_BLACKBOX_STATE_BUDGET 2048u
#define SRNOS_BLACKBOX_RTOS_RESERVE 1024u
bool SRNOS_BlackboxConfigValid(const SRNOS_BlackboxConfig *config);
uint32_t SRNOS_BlackboxHeapBudget(const SRNOS_BlackboxConfig *config);
bool SRNOS_BlackboxPrepare(const SRNOS_BlackboxConfig *config);
bool SRNOS_BlackboxStart(void);
