#pragma once
#include <stdint.h>
#define FDCAN_EXTENDED_ID 1u
#define FDCAN_REMOTE_FRAME 1u
#ifdef __cplusplus
extern "C" {
#endif
uint32_t HAL_GetTick(void);
#ifdef __cplusplus
}
#endif
