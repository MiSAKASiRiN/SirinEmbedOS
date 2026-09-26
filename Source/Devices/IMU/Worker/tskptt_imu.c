#include "tskptt_imu_internal.h"
#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

volatile imu_app_state_t g_imu_app_state = IMU_APP_STATE_BOOT;
const uint8_t g_imu_calib_mode = CONF_IMU_CALIBRATION_MODE;

uint8_t g_tskptt_imu_calib_done = 0u;
uint8_t g_tskptt_imu_calib_started = 0u;
uint8_t g_tskptt_imu_temp_ctrl_ready = 0u;

#if (CONF_IMU_HEATER_ENABLE != 0u)
extern TIM_HandleTypeDef CONF_IMU_HEATER_TIMER_HANDLE;
#endif

static OmxImu s_omx_imu;
static imu_calib_storage_status_t s_eeprom_status = IMU_CALIB_STORAGE_ERROR;

static void tskptt_imu_init_internal_sensor(void)
{
    imu_status_t st = IMU_InternalInit(CONF_IMU_INTERNAL_SPI_BUS_ID,
                                       CONF_IMU_INTERNAL_ACC_CS_GPIO_PORT,
                                       CONF_IMU_INTERNAL_ACC_CS_PIN,
                                       CONF_IMU_INTERNAL_GYRO_CS_GPIO_PORT,
                                       CONF_IMU_INTERNAL_GYRO_CS_PIN,
                                       CONF_IMU_INTERNAL_RUN_BOOT_CALIBRATION);
    if (st != IMU_STATUS_OK)
    {
        LOGE("BMI088 init failed, st=%d", (int)st);
        g_omx_dbg_imu.sample_error++;
        tskptt_imu_debug_sync(HAL_GetTick(), (uint32_t)st);
    }
}

bool SRNOS_IMU_Prepare(void)
{
    OmxImu_Init(&s_omx_imu, CONF_IMU_EXTERNAL_QUAT_STD_ID);
    OmxImu_SetOfflineTimeout(&s_omx_imu, CONF_IMU_EXTERNAL_OFFLINE_MS);
    if ((CONF_IMU_EXTERNAL_CAN_BUS != 0u) &&
        OmxImu_AttachCan(CONF_IMU_EXTERNAL_CAN_BUS, &s_omx_imu) != 0)
    {
        LOGW("OMX IMU register failed (bus=%u)",
             (unsigned)CONF_IMU_EXTERNAL_CAN_BUS);
        return false;
    }
    return true;
}

static void tskptt_imu_init_temp_ctrl(void)
{
#if (CONF_IMU_HEATER_ENABLE != 0u)
    imu_temp_ctrl_hw_t hw_cfg = {
        .htim = &CONF_IMU_HEATER_TIMER_HANDLE,
        .channel = CONF_IMU_HEATER_TIMER_CHANNEL,
        .max_ccr = (float)__HAL_TIM_GET_AUTORELOAD(
            &CONF_IMU_HEATER_TIMER_HANDLE),
    };
    const float pid_param[3] = {
        CONF_IMU_HEATER_PID_KP,
        CONF_IMU_HEATER_PID_KI,
        CONF_IMU_HEATER_PID_KD,
    };

    IMU_TempCtrl_Init(&hw_cfg,
                      CONF_IMU_HEATER_TARGET_C,
                      pid_param,
                      CONF_IMU_HEATER_PID_MAX_OUT,
                      CONF_IMU_HEATER_PID_MAX_IOUT,
                      CONF_IMU_HEATER_STABLE_DELTA_C,
                      CONF_IMU_HEATER_STABLE_TIME_MS);
    g_tskptt_imu_temp_ctrl_ready = 1u;
#else
    g_tskptt_imu_temp_ctrl_ready = 0u;
    LOGI("IMU heater disabled; timer/PWM and temperature gate bypassed.");
#endif
}

static void tskptt_imu_init_calib_storage(void)
{
    s_eeprom_status = IMU_CalibStorage_Init(CONF_IMU_EEPROM_I2C_BUS_ID,
                                            CONF_IMU_EEPROM_I2C_ADDR_7BIT);
    if (s_eeprom_status != IMU_CALIB_STORAGE_OK)
    {
        LOGE("EEPROM calib storage init failed, st=%d", (int)s_eeprom_status);
    }
    else
    {
        LOGI("EEPROM calib storage init OK (addr=0x%02X).",
             CONF_IMU_EEPROM_I2C_ADDR_7BIT);
    }
}

static void tskptt_imu_load_calib_or_default(void)
{
    imu_calib_result_t calib;
    IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
    g_tskptt_imu_calib_started = 1u;

    imu_calib_storage_status_t st =
        IMU_CalibStorage_Load(CONF_IMU_EEPROM_CALIB_ADDR, &calib);
    if (st == IMU_CALIB_STORAGE_OK)
    {
        IMU_ActiveCalib_Set(&calib);
        LOGI("Loaded IMU calib from EEPROM at boot (bias=%.4f,%.4f,%.4f scale=%.6f)",
             (double)calib.gyro_bias[0],
             (double)calib.gyro_bias[1],
             (double)calib.gyro_bias[2],
             (double)calib.accel_scale);
    }
    else
    {
        LOGW("No valid IMU calib in EEPROM at boot, using default calib.");
        IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
    }

    g_tskptt_imu_calib_done = 1u;
}

static void tskptt_imu_save_calib_if_possible(const imu_calib_result_t *calib)
{
    if ((calib == NULL) || (s_eeprom_status != IMU_CALIB_STORAGE_OK))
    {
        return;
    }

    imu_calib_storage_status_t st =
        IMU_CalibStorage_Save(CONF_IMU_EEPROM_CALIB_ADDR, calib);
    if (st != IMU_CALIB_STORAGE_OK)
    {
        LOGE("EEPROM write calib failed, st=%d", (int)st);
    }
}

static void tskptt_imu_tick_external(uint32_t now_ms)
{
    OmxImu_Tick(&s_omx_imu, now_ms);
    OmxImuState omx_state;
    if (OmxImu_Snapshot(&s_omx_imu, &omx_state))
    {
        tskptt_imu_omx_publish(&omx_state);
        tskptt_imu_snapshot_update_external(&omx_state, now_ms);
    }
}

void TSKPTT_IMU_Task(void *argument)
{
    (void)argument;

    tskptt_imu_debug_init();
    tskptt_imu_debug_sync(HAL_GetTick(), IMU_STATUS_BUSY);

    tskptt_imu_init_internal_sensor();
    (void)IMU_Init();
    LIB_INS_Init();
    LIB_INS_SetWarmupTime(CONF_IMU_INS_WARMUP_S);
    tskptt_imu_dt_init();
    tskptt_imu_init_temp_ctrl();
    tskptt_imu_init_calib_storage();

    IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
    g_tskptt_imu_calib_done = 0u;
    g_tskptt_imu_calib_started = 0u;
    tskptt_imu_dt_reset();

#if (CONF_IMU_CALIBRATION_MODE == 0u)
    tskptt_imu_load_calib_or_default();
#endif

#if (CONF_IMU_HEATER_ENABLE != 0u)
    g_imu_app_state = IMU_APP_STATE_WARMUP;
#else
    g_imu_app_state = IMU_APP_STATE_BOOT;
#endif

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t counter = 0u;

    LOGI("IMU Task Start (non-blocking, heater=%u, calib_mode=%u)",
         (unsigned)CONF_IMU_HEATER_ENABLE,
         (unsigned)CONF_IMU_CALIBRATION_MODE);

    for (;;)
    {
        const uint32_t now_ms = HAL_GetTick();
        tskptt_imu_tick_external(now_ms);

        imu_sample_t samp;
        imu_status_t st = IMU_SampleOnce(&samp);

        if (st == IMU_STATUS_OK)
        {
            g_omx_dbg_imu.sample_ok++;
            const float sample_dt_s = tskptt_imu_dt_compute(now_ms);
#if (CONF_IMU_HEATER_ENABLE != 0u)
            IMU_TempCtrl_StepWithTemp(samp.temperature, sample_dt_s);
#endif
            g_omx_dbg_imu.temperature_c = samp.temperature;
            tskptt_imu_recovery_on_fresh_sample();

#if (CONF_IMU_HEATER_ENABLE != 0u)
            uint8_t temp_stable = IMU_TempCtrl_IsStable();

            if (temp_stable == 0u)
            {
                if ((counter++ % CONF_IMU_LOG_DIV) == 0u)
                {
                    LOGI("AWAIT  T[C]=%6.2f  target=%6.2f",
                         samp.temperature,
                         IMU_TempCtrl_GetTargetTemp());
                }
            }
#else
            const uint8_t temp_stable = 1u;
#endif

            if (g_tskptt_imu_calib_done == 0u)
            {
#if (CONF_IMU_CALIBRATION_MODE == 0u)
                g_tskptt_imu_calib_done = 1u;
#elif (CONF_IMU_CALIBRATION_MODE == 1u)
                if (temp_stable == 0u)
                {
                    g_imu_app_state = IMU_APP_STATE_WARMUP;
                }
                else
                {
                    if (g_tskptt_imu_calib_started == 0u)
                    {
                        g_tskptt_imu_calib_started = 1u;
                        LOGI("Starting static IMU calibration; keep robot still.");
                        IMU_Calib_Start();
                    }

                    g_imu_app_state = IMU_APP_STATE_CALIB;
                    IMU_Calib_AddSample(&samp);

                    imu_calib_state_t cst = IMU_Calib_GetState();
                    if (cst == IMU_CALIB_DONE)
                    {
                        imu_calib_result_t calib;
                        IMU_Calib_GetResult(&calib);

                        if (calib.success != 0u)
                        {
                            IMU_ActiveCalib_Set(&calib);
                            LOGI("Static calibration OK: bias=%.4f,%.4f,%.4f scale=%.6f g=%.4f T=%.2f",
                                 (double)calib.gyro_bias[0],
                                 (double)calib.gyro_bias[1],
                                 (double)calib.gyro_bias[2],
                                 (double)calib.accel_scale,
                                 (double)calib.g_norm,
                                 (double)calib.temp_c);
                            tskptt_imu_save_calib_if_possible(&calib);
                        }
                        else
                        {
                            LOGW("Static calibration result not marked success, using default calib.");
                            IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
                        }

                        g_tskptt_imu_calib_done = 1u;
                        g_imu_app_state = IMU_APP_STATE_RUNNING;
                        LOGI("IMU calib stage finished (mode=1), start publishing data.");
                    }
                    else if (cst == IMU_CALIB_FAILED)
                    {
                        LOGE("Static calibration FAILED (timeout or motion), using default calib.");
                        IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
                        g_tskptt_imu_calib_done = 1u;
                        g_imu_app_state = IMU_APP_STATE_RUNNING;
                    }
                }
#elif (CONF_IMU_CALIBRATION_MODE == 2u)
                if (temp_stable == 0u)
                {
                    g_imu_app_state = IMU_APP_STATE_WARMUP;
                }
                else if (g_tskptt_imu_calib_started == 0u)
                {
                    g_tskptt_imu_calib_started = 1u;
                    LOGW("Turntable calibration mode (2) NOT implemented, using default calib.");
                    IMU_ActiveCalib_SetDefault(CONF_IMU_HEATER_TARGET_C);
                    g_tskptt_imu_calib_done = 1u;
                    g_imu_app_state = IMU_APP_STATE_RUNNING;
                    LOGI("IMU calib stage finished (mode=2 placeholder), start publishing data.");
                }
#else
#error "Unsupported CONF_IMU_CALIBRATION_MODE"
#endif
            }

            if (g_tskptt_imu_calib_done != 0u)
            {
                IMU_ApplyActiveCalibration(&samp);

                LIB_INS_Update(&samp, sample_dt_s);
                const lib_ins_state_t *ins = LIB_INS_GetState();

                switch (ins->app_state)
                {
                case LIB_INS_APP_STATE_CONV:
                    g_imu_app_state = IMU_APP_STATE_CONV;
                    break;
                case LIB_INS_APP_STATE_RUN:
                    g_imu_app_state = IMU_APP_STATE_RUNNING;
                    break;
                default:
                    g_imu_app_state = (temp_stable != 0u) ? IMU_APP_STATE_BOOT : IMU_APP_STATE_WARMUP;
                    break;
                }

                tskptt_imu_snapshot_update_internal(&samp, ins, now_ms, 1u);

                if ((counter++ % CONF_IMU_LOG_DIV) == 0u)
                {
                    if (ins->app_state == LIB_INS_APP_STATE_CONV)
                    {
                        LOGD("INS R/P/Y[deg]: %7.2f %7.2f %7.2f  "
                             "YawTotal[deg]: %8.2f  "
                             "Acc_b[m/s2]: %8.3f %8.3f %8.3f  "
                             "Acc_n[m/s2]: %8.3f %8.3f %8.3f [CONV]",
                             ins->roll * 57.2958f,
                             ins->pitch * 57.2958f,
                             ins->yaw * 57.2958f,
                             ins->yaw_total * 57.2958f,
                             ins->motion_accel_b[0],
                             ins->motion_accel_b[1],
                             ins->motion_accel_b[2],
                             ins->motion_accel_n[0],
                             ins->motion_accel_n[1],
                             ins->motion_accel_n[2]);
                    }
                }
            }
            tskptt_imu_debug_sync(now_ms, st);
        }
        else if ((st == IMU_STATUS_STALE) || (st == IMU_STATUS_ERR_BMI))
        {
            if (st == IMU_STATUS_STALE)
            {
                g_omx_dbg_imu.sample_stale++;
            }
            else
            {
                g_omx_dbg_imu.sample_error++;
            }
            tskptt_imu_handle_sample_fault(st, now_ms);
            LIB_INS_InvalidatePublic();
            tskptt_imu_dt_reset();
            tskptt_imu_snapshot_set_internal_offline(now_ms);
            tskptt_imu_debug_sync(now_ms, st);
        }
        else
        {
            g_omx_dbg_imu.sample_busy++;
            tskptt_imu_debug_sync(now_ms, st);
        }

        if (st != IMU_STATUS_OK)
        {
            tskptt_imu_recovery_tick(now_ms);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONF_IMU_TASK_PERIOD_MS));
    }
}
