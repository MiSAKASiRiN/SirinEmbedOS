#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void SRNOS_LogCaptureBoot(const void *data, size_t length);
void omx_boot_diag_replay_reset(void);
bool omx_boot_diag_replay_peek(const uint8_t **data, size_t *length);
bool omx_boot_diag_replay_commit(size_t length);
