#pragma once
#include <stdint.h>
#define FDCAN_EXTENDED_ID 1u
#define FDCAN_REMOTE_FRAME 1u
#define FDCAN_DATA_FRAME 0u
#define FDCAN_CLASSIC_CAN 0u
#define FDCAN_FD_CAN 1u
#define FDCAN_BRS_ON 1u
#define FDCAN_ESI_PASSIVE 1u
#ifdef __cplusplus
extern "C" {
#endif
uint32_t HAL_GetTick(void);
#ifdef __cplusplus
}
#endif
