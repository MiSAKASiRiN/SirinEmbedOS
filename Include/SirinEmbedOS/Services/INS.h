#ifndef SRNOS_SERVICES_INS_H
#define SRNOS_SERVICES_INS_H
#include "SirinEmbedOS/Devices/Sensors.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Startup-only: q rotates BOARD vectors into ROBOT axes. Board mounting is
 * fixed privately; only this installation rotation is application-owned. */
typedef struct {
    bool enabled; /* false keeps sampling/heating, e.g. SensorBringUp. */
    float board_to_robot_wxyz[4];
} SRNOS_INSConfig;
typedef enum {
    SRNOS_INS_DISABLED, SRNOS_INS_PROBING, SRNOS_INS_WARMUP,
    SRNOS_INS_OBSERVING, SRNOS_INS_CONVERGING, SRNOS_INS_RUNNING,
    SRNOS_INS_DEGRADED, SRNOS_INS_OFFLINE, SRNOS_INS_FATAL
} SRNOS_INSPhase;
enum {
    SRNOS_INS_VALID=1u, SRNOS_INS_PREDICTED=2u,
    SRNOS_INS_SINGLE=4u, SRNOS_INS_TIME_SKEW=8u,
    SRNOS_INS_THERMAL_PENDING=16u, SRNOS_INS_WARMUP_TIMEOUT=32u,
    SRNOS_INS_HEATER_PROTECTION=64u, SRNOS_INS_HEATER_FAULT=128u,
    SRNOS_INS_UNCALIBRATED=256u, SRNOS_INS_RELATIVE_HEADING=512u
};
typedef struct {
    uint32_t sequence, epoch, flags;
    uint64_t attitude_us, published_us;
    uint32_t input_sequence[2], input_age_us[2];
    uint8_t source_mask, new_sample_mask;
    float weights[2];
    /* Hamilton w,x,y,z: ROBOT -> WORLD, WORLD Z up. Euler radians, Rz*Ry*Rx.
     * Yaw starts at zero, is not an absolute heading; quaternion authoritative
     * near Euler pitch +/- pi/2. Acceleration is specific force INCLUDING g. */
    float quaternion_wxyz[4], euler_rad[3];
    float accel_m_s2[3], gyro_rad_s[3];
} SRNOS_INSAttitude;
typedef struct {
    SRNOS_INSPhase phase;
    uint32_t flags, epoch;
    uint8_t healthy_imu_mask;
    bool calibrated, absolute_heading;
    uint64_t updated_us;
    uint32_t cycles, publications, dual_cycles, single_cycles, predicted_cycles;
    uint32_t missing_cycles, recoveries, missed_deadlines;
    uint32_t max_execution_us, max_period_us, max_input_skew_us, max_copy_us;
    SRNOS_HeaterStatus heater;
    SRNOS_SensorState sensors[SRNOS_SENSOR_COUNT];
} SRNOS_INSStatus;
typedef struct {
    SRNOS_INSAttitude attitude;
    SRNOS_INSStatus status;
    /* SI vectors below are ROBOT axes; raw integers remain SENSOR axes.
     * Per-device sequence/time remains the actual source publication, not the
     * 1 kHz INS clock. This is coherent storage, not simultaneous acquisition. */
    SRNOS_IMUSample imu[2];
    SRNOS_MagneticSample magnetic;
    SRNOS_PressureSample pressure;
} SRNOS_INSSnapshot;
/* Non-consuming task-context copies. true means a valid output pointer, NOT
 * that attitude is ready: test SRNOS_INS_VALID. Each consumer tracks sequence
 * and epoch independently. No mutable internal pointers are exported. */
bool SRNOS_INSGetSnapshot(SRNOS_INSSnapshot *out);
bool SRNOS_INSGetAttitude(SRNOS_INSAttitude *out);
bool SRNOS_INSGetStatus(SRNOS_INSStatus *out);
#ifdef __cplusplus
}
#endif
#endif
