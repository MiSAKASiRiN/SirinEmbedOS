//
// lib_adp_omximu.h
// OMX IMU CAN adapter: quat/euler/gyro/accel (0x220..0x223, little-endian)
//
#ifndef H723VG_V2_FREERTOS_LIB_ADP_OMXIMU_H
#define H723VG_V2_FREERTOS_LIB_ADP_OMXIMU_H
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"

#ifndef ADP_CANFRAME_DEFINED
#define ADP_CANFRAME_DEFINED
typedef struct CanFrame {
    uint32_t id;
    uint8_t  dlc;
    bool     is_ext;
    uint8_t  _pad[2];
    uint8_t  data[8];
} CanFrame;
#if defined(__cplusplus)
static_assert(offsetof(CanFrame, data) == 8u, "CanFrame.data must be word-aligned");
static_assert(sizeof(CanFrame) == 16u, "CanFrame must remain 16 bytes");
#else
_Static_assert(offsetof(CanFrame, data) == 8u, "CanFrame.data must be word-aligned");
_Static_assert(sizeof(CanFrame) == 16u, "CanFrame must remain 16 bytes");
#endif
#else
typedef struct CanFrame CanFrame;
#endif

#ifndef OMXIMU_QUAT_STD_ID
#define OMXIMU_QUAT_STD_ID 0x020u
#endif
#define OMXIMU_EULER_STD_ID (OMXIMU_QUAT_STD_ID + 1u)
#define OMXIMU_GYRO_STD_ID  (OMXIMU_QUAT_STD_ID + 2u)
#define OMXIMU_ACCEL_STD_ID (OMXIMU_QUAT_STD_ID + 3u)

typedef struct OmxImuState {
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
} OmxImuState;

typedef struct OmxImu {
    uint16_t quat_id;
    uint16_t euler_id;
    uint16_t gyro_id;
    uint16_t accel_id;
    uint32_t offline_ms;
    volatile uint32_t state_seq;
    OmxImuState state;
} OmxImu;

#ifdef __cplusplus
extern "C" {
#endif

void OmxImu_Init(OmxImu* imu, uint16_t quat_id);
void OmxImu_SetOfflineTimeout(OmxImu* imu, uint32_t ms);
uint16_t OmxImu_QuatId(const OmxImu* imu);
uint16_t OmxImu_EulerId(const OmxImu* imu);
uint16_t OmxImu_GyroId(const OmxImu* imu);
uint16_t OmxImu_AccelId(const OmxImu* imu);

void OmxImu_OnRxFeedback(OmxImu* imu, const CanFrame* f, uint32_t now_ms);
void OmxImu_Tick(OmxImu* imu, uint32_t now_ms);
bool OmxImu_Snapshot(const OmxImu* imu, OmxImuState* out);

const can_adapter_ops_t* OmxImu_CanOps(void);
int OmxImu_AttachCan(can_channel_t ch, OmxImu* imu);
int OmxImu_DetachCan(can_channel_t ch, OmxImu* imu);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_LIB_ADP_OMXIMU_H
