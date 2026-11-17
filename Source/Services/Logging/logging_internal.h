#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"

#define SRNOS_LOG_WORKER_STACK_BYTES 2048u
#define SRNOS_LOG_MINIMUM_HEAP_RESERVE_BYTES (16u * 1024u)
#define SRNOS_LOG_RESOURCE_MARGIN_BYTES 1024u
#define SRNOS_LOG_DEFAULT_QUEUE_DEPTH 20u
#define SRNOS_LOG_DEFAULT_POOL_COUNT 64u

bool SRNOS_LoggingSerialConfigValid(const SRNOS_SerialConfig *config);
uint32_t SRNOS_LoggingHeapBudget(bool enabled);
