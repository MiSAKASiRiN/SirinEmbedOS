#pragma once
#include "SirinEmbedOS/Kernel/Thread.h"
bool SRNOS_KernelThreadConfigValid(const SRNOS_ThreadConfig *);
uint32_t SRNOS_KernelThreadHeapBudget(bool cycle_statistics);
