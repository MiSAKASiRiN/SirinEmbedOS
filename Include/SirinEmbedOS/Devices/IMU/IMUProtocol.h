//
// IMUProtocol.h
// OMX IMU CAN adapter: quat/euler/gyro/accel (0x220..0x223, little-endian)
//
#ifndef SRNOS_IMU_PROTOCOL_H
#define SRNOS_IMU_PROTOCOL_H
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"

#include "SirinEmbedOS/Devices/CANFrame.h"

#ifndef SRNOS_IMU_PROTOCOL_QUAT_STD_ID
#define SRNOS_IMU_PROTOCOL_QUAT_STD_ID 0x020u
#endif
#define SRNOS_IMU_PROTOCOL_EULER_STD_ID (SRNOS_IMU_PROTOCOL_QUAT_STD_ID + 1u)
#define SRNOS_IMU_PROTOCOL_GYRO_STD_ID  (SRNOS_IMU_PROTOCOL_QUAT_STD_ID + 2u)
#define SRNOS_IMU_PROTOCOL_ACCEL_STD_ID (SRNOS_IMU_PROTOCOL_QUAT_STD_ID + 3u)

typedef struct SRNOS_IMUProtocolState {
    float    quat[4];      // q0..q3
    float    euler[3];     // roll/pitch/yaw (rad)
    float    gyro[3];      // rad/s
    float    accel[3];     // m/s^2
    uint8_t  euler_seq;
    uint8_t  gyro_seq;
    uint8_t  accel_seq;
    uint32_t last_rx_ms;
    bool     is_online;
    uint32_t rx_count;
} SRNOS_IMUProtocolState;

typedef struct SRNOS_IMUProtocol {
    uint16_t quat_id;
    uint16_t euler_id;
    uint16_t gyro_id;
    uint16_t accel_id;
    uint32_t offline_ms;
    volatile uint32_t state_seq;
    SRNOS_IMUProtocolState state;
} SRNOS_IMUProtocol;

#ifdef __cplusplus
extern "C" {
#endif

void SRNOS_IMUProtocolInit(SRNOS_IMUProtocol* imu, uint16_t quat_id);
void SRNOS_IMUProtocolSetOfflineTimeout(SRNOS_IMUProtocol* imu, uint32_t ms);
uint16_t SRNOS_IMUProtocolQuatId(const SRNOS_IMUProtocol* imu);
uint16_t SRNOS_IMUProtocolEulerId(const SRNOS_IMUProtocol* imu);
uint16_t SRNOS_IMUProtocolGyroId(const SRNOS_IMUProtocol* imu);
uint16_t SRNOS_IMUProtocolAccelId(const SRNOS_IMUProtocol* imu);

void SRNOS_IMUProtocolOnRxFeedback(SRNOS_IMUProtocol* imu, const SRNOS_CANDeviceFrame* f, uint32_t now_ms);
void SRNOS_IMUProtocolTick(SRNOS_IMUProtocol* imu, uint32_t now_ms);
bool SRNOS_IMUProtocolSnapshot(const SRNOS_IMUProtocol* imu, SRNOS_IMUProtocolState* out);

const SRNOS_CANAdapterOps* SRNOS_IMUProtocolCanOps(void);
int SRNOS_IMUProtocolAttachCan(SRNOS_CANChannel ch, SRNOS_IMUProtocol* imu);
int SRNOS_IMUProtocolDetachCan(SRNOS_CANChannel ch, SRNOS_IMUProtocol* imu);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_IMU_PROTOCOL_H
