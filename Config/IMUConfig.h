#ifndef SRNOS_IMU_CONFIG_H
#define SRNOS_IMU_CONFIG_H
#include "SirinEmbedOSConfig.h"
/* Fixed board wiring and startup-only IMU defaults. Override allowed values
 * in SIRINEMBEDOS_CONFIG_HEADER; none are HostLink writable parameters. */

/* Sensor-frame to body-frame preset. Default restores the former preset 6. */
#ifndef IMU_ORIENTATION_SEL
#define IMU_ORIENTATION_SEL                     (6)
#endif

/* IMU task timing and diagnostics. */
#ifndef CONF_IMU_TASK_PERIOD_MS
#define CONF_IMU_TASK_PERIOD_MS                 (1u)
#endif
#ifndef CONF_IMU_LOG_DIV
#define CONF_IMU_LOG_DIV                        (250u)
#endif

/* Internal BMI088 board wiring. Bus 0 maps to BSP_SPI_BUS_4 in this project. */
#define CONF_IMU_INTERNAL_SPI_BUS_ID            (0u)
#define CONF_IMU_INTERNAL_ACC_CS_GPIO_PORT      BMI_ACC_CS_GPIO_Port
#define CONF_IMU_INTERNAL_ACC_CS_PIN            BMI_ACC_CS_Pin
#define CONF_IMU_INTERNAL_GYRO_CS_GPIO_PORT     BMI_CS_GYRO_GPIO_Port
#define CONF_IMU_INTERNAL_GYRO_CS_PIN           BMI_CS_GYRO_Pin
#ifndef CONF_IMU_INTERNAL_RUN_BOOT_CALIBRATION
#define CONF_IMU_INTERNAL_RUN_BOOT_CALIBRATION  (0u)
#endif

/* External OMX IMU transport. */
#ifndef CONF_IMU_EXTERNAL_CAN_BUS
#define CONF_IMU_EXTERNAL_CAN_BUS               (0u) /* no external IMU by default */
#endif
#ifndef CONF_IMU_EXTERNAL_QUAT_STD_ID
#define CONF_IMU_EXTERNAL_QUAT_STD_ID           (0x020u)
#endif
#ifndef CONF_IMU_EXTERNAL_OFFLINE_MS
#define CONF_IMU_EXTERNAL_OFFLINE_MS            (100u)
#endif

/* Calibration persistence on the board EEPROM. */
#define CONF_IMU_EEPROM_I2C_BUS_ID              (0u)
#define CONF_IMU_EEPROM_I2C_ADDR_7BIT           (0x50u)
#define CONF_IMU_EEPROM_CALIB_ADDR              (0x20u)
#ifndef CONF_IMU_EEPROM_XFER_TIMEOUT_MS
#define CONF_IMU_EEPROM_XFER_TIMEOUT_MS         (100u)
#endif
#ifndef CONF_IMU_EEPROM_WRITE_CYCLE_MS
#define CONF_IMU_EEPROM_WRITE_CYCLE_MS          (5u)
#endif

/* 0: load saved calibration; 1: static calibration; 2: reserved. */
#ifndef CONF_IMU_CALIBRATION_MODE
#define CONF_IMU_CALIBRATION_MODE               (0u)
#endif
#ifndef CONF_IMU_CALIB_SAMPLES
#define CONF_IMU_CALIB_SAMPLES                  (2000u)
#endif
#ifndef CONF_IMU_CALIB_TIMEOUT_MS
#define CONF_IMU_CALIB_TIMEOUT_MS               (15000u)
#endif
#ifndef CONF_IMU_CALIB_GRAVITY_MPS2
#define CONF_IMU_CALIB_GRAVITY_MPS2             (9.81f)
#endif

/*
 * Heater selection. This project defaults to the complete heated path.
 * Set CONF_IMU_HEATER_ENABLE to 0 for a build that does not reference or
 * start the heater timer/PWM path.
 */
#ifndef CONF_IMU_HEATER_ENABLE
#define CONF_IMU_HEATER_ENABLE                  (1u)
#endif

#if (CONF_IMU_HEATER_ENABLE != 0u)
#define CONF_IMU_HEATER_TIMER_HANDLE            htim3
#define CONF_IMU_HEATER_TIMER_CHANNEL           TIM_CHANNEL_4
#endif

#ifndef CONF_IMU_HEATER_TARGET_C
#define CONF_IMU_HEATER_TARGET_C                (55.0f)
#endif
/*
 * Continuous-time PID units used by omx::control::Pid:
 *   Kp [output/degC], Ki [output/(degC*s)], Kd [output*s/degC].
 * Initial values are the mathematical 1 ms conversion of the former
 * discrete gains {100, 50, 10}: Ki=50/0.001, Kd=10*0.001.
 * They are not a claim of completed hardware tuning.
 */
#ifndef CONF_IMU_HEATER_PID_KP
#define CONF_IMU_HEATER_PID_KP                  (100.0f)
#endif
#ifndef CONF_IMU_HEATER_PID_KI
#define CONF_IMU_HEATER_PID_KI                  (50000.0f)
#endif
#ifndef CONF_IMU_HEATER_PID_KD
#define CONF_IMU_HEATER_PID_KD                  (0.01f)
#endif
#ifndef CONF_IMU_HEATER_PID_MAX_OUT
#define CONF_IMU_HEATER_PID_MAX_OUT             (500.0f)
#endif
#ifndef CONF_IMU_HEATER_PID_MAX_IOUT
#define CONF_IMU_HEATER_PID_MAX_IOUT            (300.0f)
#endif
#ifndef CONF_IMU_HEATER_STABLE_DELTA_C
#define CONF_IMU_HEATER_STABLE_DELTA_C          (5.0f)
#endif
#ifndef CONF_IMU_HEATER_STABLE_TIME_MS
#define CONF_IMU_HEATER_STABLE_TIME_MS          (2500u)
#endif

/* Cooperative recovery policy. */
#ifndef CONF_IMU_RECOVERY_RETRY_MS
#define CONF_IMU_RECOVERY_RETRY_MS              (10u)
#endif
#ifndef CONF_IMU_RECOVERY_TIMEOUT_MS
#define CONF_IMU_RECOVERY_TIMEOUT_MS            (1000u)
#endif
#ifndef CONF_IMU_RECOVERY_LOG_MS
#define CONF_IMU_RECOVERY_LOG_MS                (250u)
#endif
#ifndef CONF_IMU_STALE_FATAL_COUNT
#define CONF_IMU_STALE_FATAL_COUNT              (100u)
#endif
#ifndef CONF_IMU_ERROR_FATAL_COUNT
#define CONF_IMU_ERROR_FATAL_COUNT              (50u)
#endif

/* INS startup, validity and Mahony tuning. */
#ifndef CONF_IMU_INS_GRAVITY_MPS2
#define CONF_IMU_INS_GRAVITY_MPS2               (9.80665f)
#endif
#ifndef CONF_IMU_INS_DT_MIN_S
#define CONF_IMU_INS_DT_MIN_S                    (0.0002f)
#endif
#ifndef CONF_IMU_INS_DT_MAX_S
#define CONF_IMU_INS_DT_MAX_S                    (0.0200f)
#endif
#ifndef CONF_IMU_INS_STABLE_HOLD_S
#define CONF_IMU_INS_STABLE_HOLD_S               (0.2000f)
#endif
#ifndef CONF_IMU_INS_STABLE_GYRO_MAX_RAD_S
#define CONF_IMU_INS_STABLE_GYRO_MAX_RAD_S       (0.0872664626f)
#endif
#ifndef CONF_IMU_INS_MOTION_GYRO_FULL_RAD_S
#define CONF_IMU_INS_MOTION_GYRO_FULL_RAD_S      (1.396263402f)
#endif
#ifndef CONF_IMU_INS_MOTION_KP_MIN_WEIGHT
#define CONF_IMU_INS_MOTION_KP_MIN_WEIGHT        (0.08f)
#endif
#ifndef CONF_IMU_INS_MAHONY_KP
#define CONF_IMU_INS_MAHONY_KP                   (1.0f)
#endif
#ifndef CONF_IMU_INS_MAHONY_KI
#define CONF_IMU_INS_MAHONY_KI                   (0.0f)
#endif
#ifndef CONF_IMU_INS_ATTITUDE_SEED_SAMPLES
#define CONF_IMU_INS_ATTITUDE_SEED_SAMPLES       (20u)
#endif
#ifndef CONF_IMU_INS_WARMUP_S
#define CONF_IMU_INS_WARMUP_S                    (2.0f)
#endif

#endif /* SRNOS_IMU_CONFIG_H */
