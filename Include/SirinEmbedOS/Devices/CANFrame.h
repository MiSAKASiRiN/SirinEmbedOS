#ifndef SRNOS_DEVICE_CAN_FRAME_H
#define SRNOS_DEVICE_CAN_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool is_ext;
    uint8_t reserved[2];
    uint8_t data[8];
} SRNOS_CANDeviceFrame;
#ifdef __cplusplus
static_assert(offsetof(SRNOS_CANDeviceFrame, data) == 8u);
static_assert(sizeof(SRNOS_CANDeviceFrame) == 16u);
namespace SRNOS::Devices { using CANFrame = SRNOS_CANDeviceFrame; }
#else
_Static_assert(offsetof(SRNOS_CANDeviceFrame, data) == 8u, "frame data alignment");
_Static_assert(sizeof(SRNOS_CANDeviceFrame) == 16u, "frame size");
#endif
#endif
