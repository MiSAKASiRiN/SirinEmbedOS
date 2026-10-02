#ifndef SRNOS_DEVICES_SENSORS_H
#define SRNOS_DEVICES_SENSORS_H
#include <stdbool.h>
#include <stdint.h>
#include "SirinEmbedOS/IO/SPI/SPI.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { SRNOS_SENSOR_BMI088, SRNOS_SENSOR_LSM6DS3, SRNOS_SENSOR_LIS2MDL, SRNOS_SENSOR_SPA06, SRNOS_SENSOR_COUNT } SRNOS_SensorId;
typedef enum { SRNOS_SENSOR_DISABLED, SRNOS_SENSOR_INITIALIZING, SRNOS_SENSOR_READY, SRNOS_SENSOR_OFFLINE, SRNOS_SENSOR_UNSUPPORTED, SRNOS_SENSOR_CONFIG_ERROR, SRNOS_SENSOR_STALE } SRNOS_SensorState;
typedef struct SRNOS_IMUSample SRNOS_IMUSample;
/* Called after a coherent publication, from a high-priority task (including
 * the SPI dispatcher). Copy only; no blocking, encoding, logging or bus I/O.
 * Binding/user lifetime is static and frozen with system configuration. */
typedef void (*SRNOS_IMUSampleObserver)(SRNOS_SensorId, const SRNOS_IMUSample *, void *);
typedef struct {
    bool lsm_enabled, lis_enabled, spa_enabled;
    bool heater_enabled; /* Frozen. One INS/control worker owns PWM. */
    float heater_target_c; /* 20..75 C; default 60. LSM preferred, BMI fallback. */
    SRNOS_IMUSampleObserver imu_observer;
    void *imu_observer_user;
    uint16_t bmi_period_ms, lsm_period_ms, environment_period_ms;
    /* Fixed first-version ranges/ODR: LSM 4g/2000dps/1660Hz;
     * LIS 100Hz; SPA P/T 16Hz, oversampling P8/T1. */
} SRNOS_SensorConfig;
enum { SRNOS_SENSOR_ACCEL=1, SRNOS_SENSOR_GYRO=2, SRNOS_SENSOR_MAG=4, SRNOS_SENSOR_PRESSURE=8, SRNOS_SENSOR_TEMPERATURE=16 };
typedef struct {
    uint32_t sequence, valid_fields;
    uint64_t acquisition_start_us, acquisition_end_us;
    SRNOS_SensorState state;
} SRNOS_SampleHeader;
struct SRNOS_IMUSample {
    SRNOS_SampleHeader header;
    int16_t raw_accel[3], raw_gyro[3], raw_temperature;
    float accel_m_s2[3], gyro_rad_s[3], temperature_c;
};
typedef struct {
    SRNOS_SampleHeader header;
    int16_t raw_magnetic[3], raw_temperature;
    float magnetic_uT[3], temperature_c;
} SRNOS_MagneticSample;
typedef struct {
    SRNOS_SampleHeader header;
    int32_t raw_pressure, raw_temperature;
    float pressure_pa, temperature_c;
} SRNOS_PressureSample;
typedef struct {
    uint32_t accepted, completed, queue_full, queue_expired, errors, timeouts, recoveries;
    uint32_t max_queue_us, max_transfer_us, queue_us[6], transfer_us[6];
} SRNOS_SensorBusStatistics;
typedef struct {
    SRNOS_SensorState state;
    uint8_t identity, revision, product, config_readback[8];
    uint32_t init_attempts, initializations;
    uint32_t polls, samples, not_ready, overruns, errors, recoveries;
    uint32_t max_acquisition_us, max_period_us;
    uint32_t period_us[6]; /* <=500, <=1000, <=1500, <=10000, <=20000, larger */
    uint64_t last_sample_us, last_poll_us;
} SRNOS_SensorStatus;
/* Task-context, coherent copy. Returns false for wrong ID/output pointer.
 * SI units in SENSOR axes, no board rotation/user calibration. Times are DWT
 * host acquisition times, not internal conversion timestamps. Disabled/offline
 * and stale samples retain old values/times, with valid_fields cleared. */
/* SPI transaction statistics; BMI combines accel and gyro CS devices. */
bool SRNOS_SensorGetBusStatistics(SRNOS_SensorId, SRNOS_SensorBusStatistics *);
bool SRNOS_SensorGetStatus(SRNOS_SensorId, SRNOS_SensorStatus *);
bool SRNOS_SensorGetIMU(SRNOS_SensorId, SRNOS_IMUSample *);
bool SRNOS_SensorGetMagnetic(SRNOS_MagneticSample *);
bool SRNOS_SensorGetPressure(SRNOS_PressureSample *);
/* Task-context coherent copy. source=-1 none, 0 BMI, 1 LSM.
 * Target applies to the active source. duty is commanded fraction [0,1]. */
typedef struct {
    bool enabled, faulted;
    int8_t source;
    uint32_t source_selections;
    float target_c, feedback_c, duty;
    bool thermal_ready, overtemperature, hardware_fault, feedback_missing;
    uint8_t hot_sensor_mask;
    uint32_t protection_count, protection_recoveries;
} SRNOS_HeaterStatus;
bool SRNOS_SensorGetHeaterStatus(SRNOS_HeaterStatus *);
/* Copies task-context statistics; no driver or hardware handle escapes. */
void SRNOS_SensorGetBMI088SPIStatistics(SRNOS_SPIStatistics *accel, SRNOS_SPIStatistics *gyro);
#ifdef __cplusplus
}
#endif
#endif
