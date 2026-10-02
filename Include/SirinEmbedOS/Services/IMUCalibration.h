#ifndef SRNOS_SERVICES_IMU_CALIBRATION_H
#define SRNOS_SERVICES_IMU_CALIBRATION_H
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Services/Storage.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_IMU_CALIBRATION_MODEL_VERSION 1u
#define SRNOS_IMU_CALIBRATION_SENSOR_AXES_VERSION 1u
/* SENSOR axes, SI units. v1 is diagonal acceleration correction, not a
 * cross-axis matrix or temperature compensation model. No packed ABI on disk. */
typedef struct {
    SRNOS_SensorId sensor;
    uint16_t model_version, axes_version;
    float gyro_bias_rad_s[3], accel_bias_m_s2[3], accel_scale[3];
    float temperature_c;
} SRNOS_IMUCalibration;
typedef struct {
    SRNOS_StorageStatus load_status[2], save_status[2];
    uint32_t active_revision[2], saved_revision[2];
    uint8_t active_mask, pending_restart_mask;
    bool startup_resolved;
} SRNOS_IMUCalibrationStatus;
/* Pure validation: finite values, supported sensor/model and documented bounds. */
SRNOS_StorageStatus SRNOS_IMUCalibrationValidate(const SRNOS_IMUCalibration *value);
/* Task-context copies. Get returns false for an uncalibrated sensor; no I/O.
 * Saving is cooperative blocking (0 => 5s); it never changes active INS input.
 * Caller-owned value must remain valid until return; SDK snapshots it first. */
bool SRNOS_IMUCalibrationGet(SRNOS_SensorId sensor, SRNOS_IMUCalibration *out);
bool SRNOS_IMUCalibrationGetStatus(SRNOS_IMUCalibrationStatus *out);
SRNOS_StorageStatus SRNOS_IMUCalibrationSave(const SRNOS_IMUCalibration *value,
                                           uint32_t timeout_ms);
/* Arbitrary-pose stationary calibration estimates GYRO BIAS ONLY. The caller
 * must physically keep the board still; IMUs cannot distinguish a sufficiently
 * slow constant rotation from bias. Existing accel bias/scale are preserved.
 * Task context; bounded cooperative call (20 s total, 5 s observation at 100 Hz).
 * No new worker, no activation until reboot. Each sensor is a separate record;
 * partial persistence returns an error with saved_mask, never overall success. */
#define SRNOS_IMU_CALIBRATION_BMI_MASK 1u
#define SRNOS_IMU_CALIBRATION_LSM_MASK 2u
#define SRNOS_IMU_CALIBRATION_BOTH_MASK 3u
typedef enum {
    SRNOS_IMU_STATIC_OK=0, SRNOS_IMU_STATIC_BUSY, SRNOS_IMU_STATIC_INVALID,
    SRNOS_IMU_STATIC_CONTEXT, SRNOS_IMU_STATIC_NOT_READY, SRNOS_IMU_STATIC_SENSOR_ERROR,
    SRNOS_IMU_STATIC_STALE, SRNOS_IMU_STATIC_MOVING, SRNOS_IMU_STATIC_TEMPERATURE,
    SRNOS_IMU_STATIC_TIMEOUT, SRNOS_IMU_STATIC_STORAGE_ERROR
} SRNOS_IMUStaticResult;
typedef enum {
    SRNOS_IMU_STATIC_IDLE, SRNOS_IMU_STATIC_PREPARING, SRNOS_IMU_STATIC_SAMPLING,
    SRNOS_IMU_STATIC_SAVING, SRNOS_IMU_STATIC_DONE, SRNOS_IMU_STATIC_FAILED
} SRNOS_IMUStaticPhase;
typedef struct {
    uint32_t job_id, elapsed_ms, samples[2], saved_revision[2];
    float gyro_bias_rad_s[2][3], temperature_c[2];
    SRNOS_StorageStatus storage_status[2];
    SRNOS_IMUStaticResult result;
    SRNOS_IMUStaticPhase phase;
    uint8_t requested_mask, saved_mask, failed_sensor; /* 0/1 or UINT8_MAX */
} SRNOS_IMUStaticStatus;
SRNOS_IMUStaticResult SRNOS_IMUCalibrationRunStatic(uint8_t sensor_mask,
                                                 SRNOS_IMUStaticStatus *out);
/* Last job/progress snapshot, including partial save after a disconnected host.
 * IDLE means no job has run. Task context, no I/O. */
bool SRNOS_IMUCalibrationGetStaticStatus(SRNOS_IMUStaticStatus *out);
#ifdef __cplusplus
}
#endif
#endif
