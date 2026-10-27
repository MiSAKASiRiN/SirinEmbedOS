#pragma once
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_IMU_STREAM_BMI 0x0100u
#define SRNOS_IMU_STREAM_LSM 0x0101u
#define SRNOS_IMU_RECORD_BYTES 72u
#define SRNOS_IMU_BATCH_MAX 6u
#define SRNOS_IMU_TELEMETRY_STATUS_MESSAGE 0x08u
/* Use as sirin_link_setup and sensors.imu_observer respectively. Both bindings
 * are declared before freeze. Storage: two static 64-sample queues in D1. */
SRNOS_SirinLinkResult SRNOS_IMUTelemetryRegister(SRNOS_SirinLinkRegistration *link, void *user);
void SRNOS_IMUTelemetryObserve(SRNOS_SensorId, const SRNOS_IMUSample *, void *user);
bool SRNOS_IMUTelemetryEncode(const SRNOS_IMUSample *, uint8_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
