#ifndef LIB_IMU_PROJECT_CONFIG_H
#define LIB_IMU_PROJECT_CONFIG_H

/*
 * The project chooses this header through the build system. Production board
 * configuration must not be supplied by the reusable IMU submodules.
 */
#ifndef SRNOS_IMU_CONFIG_HEADER
#define SRNOS_IMU_CONFIG_HEADER "IMUConfig.h"
#endif

#include SRNOS_IMU_CONFIG_HEADER

#if !defined(IMU_ORIENTATION_SEL) || \
    !defined(CONF_IMU_TASK_PERIOD_MS) || \
    !defined(CONF_IMU_LOG_DIV)
#error "Project IMU config is missing orientation or task timing settings"
#endif

#if !defined(CONF_IMU_INTERNAL_SPI_BUS_ID) || \
    !defined(CONF_IMU_INTERNAL_ACC_CS_GPIO_PORT) || \
    !defined(CONF_IMU_INTERNAL_ACC_CS_PIN) || \
    !defined(CONF_IMU_INTERNAL_GYRO_CS_GPIO_PORT) || \
    !defined(CONF_IMU_INTERNAL_GYRO_CS_PIN) || \
    !defined(CONF_IMU_INTERNAL_RUN_BOOT_CALIBRATION)
#error "Project IMU config is missing internal BMI088 wiring settings"
#endif

#if !defined(CONF_IMU_EXTERNAL_CAN_BUS) || \
    !defined(CONF_IMU_EXTERNAL_QUAT_STD_ID) || \
    !defined(CONF_IMU_EXTERNAL_OFFLINE_MS)
#error "Project IMU config is missing external IMU settings"
#endif

#if !defined(CONF_IMU_EEPROM_I2C_BUS_ID) || \
    !defined(CONF_IMU_EEPROM_I2C_ADDR_7BIT) || \
    !defined(CONF_IMU_EEPROM_CALIB_ADDR) || \
    !defined(CONF_IMU_EEPROM_XFER_TIMEOUT_MS) || \
    !defined(CONF_IMU_EEPROM_WRITE_CYCLE_MS)
#error "Project IMU config is missing EEPROM settings"
#endif

#if !defined(CONF_IMU_CALIBRATION_MODE) || \
    !defined(CONF_IMU_CALIB_SAMPLES) || \
    !defined(CONF_IMU_CALIB_TIMEOUT_MS) || \
    !defined(CONF_IMU_CALIB_GRAVITY_MPS2)
#error "Project IMU config is missing calibration settings"
#endif

#if !defined(CONF_IMU_HEATER_ENABLE) || \
    !defined(CONF_IMU_HEATER_TARGET_C) || \
    !defined(CONF_IMU_HEATER_STABLE_DELTA_C) || \
    !defined(CONF_IMU_HEATER_STABLE_TIME_MS)
#error "Project IMU config is missing heater mode settings"
#endif

#if (IMU_ORIENTATION_SEL < 0) || (IMU_ORIENTATION_SEL > 6)
#error "IMU_ORIENTATION_SEL must be in the range 0..6"
#endif

#if (CONF_IMU_HEATER_ENABLE != 0u) && (CONF_IMU_HEATER_ENABLE != 1u)
#error "CONF_IMU_HEATER_ENABLE must be 0 or 1"
#endif

#if (CONF_IMU_HEATER_ENABLE != 0u)
#if !defined(CONF_IMU_HEATER_TIMER_HANDLE) || \
    !defined(CONF_IMU_HEATER_TIMER_CHANNEL) || \
    !defined(CONF_IMU_HEATER_PID_KP) || \
    !defined(CONF_IMU_HEATER_PID_KI) || \
    !defined(CONF_IMU_HEATER_PID_KD) || \
    !defined(CONF_IMU_HEATER_PID_MAX_OUT) || \
    !defined(CONF_IMU_HEATER_PID_MAX_IOUT)
#error "Heated IMU builds are missing timer or PID settings"
#endif
#endif

#if !defined(CONF_IMU_RECOVERY_RETRY_MS) || \
    !defined(CONF_IMU_RECOVERY_TIMEOUT_MS) || \
    !defined(CONF_IMU_RECOVERY_LOG_MS) || \
    !defined(CONF_IMU_STALE_FATAL_COUNT) || \
    !defined(CONF_IMU_ERROR_FATAL_COUNT)
#error "Project IMU config is missing recovery settings"
#endif

#if !defined(CONF_IMU_INS_GRAVITY_MPS2) || \
    !defined(CONF_IMU_INS_DT_MIN_S) || \
    !defined(CONF_IMU_INS_DT_MAX_S) || \
    !defined(CONF_IMU_INS_STABLE_HOLD_S) || \
    !defined(CONF_IMU_INS_STABLE_GYRO_MAX_RAD_S) || \
    !defined(CONF_IMU_INS_MOTION_GYRO_FULL_RAD_S) || \
    !defined(CONF_IMU_INS_MOTION_KP_MIN_WEIGHT) || \
    !defined(CONF_IMU_INS_MAHONY_KP) || \
    !defined(CONF_IMU_INS_MAHONY_KI) || \
    !defined(CONF_IMU_INS_ATTITUDE_SEED_SAMPLES) || \
    !defined(CONF_IMU_INS_WARMUP_S)
#error "Project IMU config is missing INS settings"
#endif

#if (CONF_IMU_CALIBRATION_MODE > 2u)
#error "CONF_IMU_CALIBRATION_MODE must be 0, 1 or 2"
#endif

#if (CONF_IMU_TASK_PERIOD_MS == 0u)
#error "CONF_IMU_TASK_PERIOD_MS must be non-zero"
#endif

#if (CONF_IMU_LOG_DIV == 0u)
#error "CONF_IMU_LOG_DIV must be non-zero"
#endif

#if (CONF_IMU_CALIB_SAMPLES == 0u)
#error "CONF_IMU_CALIB_SAMPLES must be non-zero"
#endif

#endif /* LIB_IMU_PROJECT_CONFIG_H */
