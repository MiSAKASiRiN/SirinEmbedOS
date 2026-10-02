#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void SRNOS_LogCaptureBoot(const void *data, size_t length);
void SRNOS_BootDiagReplayReset(void);
bool SRNOS_BootDiagReplayPeek(const uint8_t **data, size_t *length);
bool SRNOS_BootDiagReplayCommit(size_t length);
