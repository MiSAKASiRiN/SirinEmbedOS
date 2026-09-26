#include "tskptt_imu_internal.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include <stdint.h>

static volatile uint8_t s_recovery_request = 0u;
static uint8_t s_recovery_active = 0u;
static uint8_t s_recovery_reset_done = 0u;
static uint8_t s_recovery_timeout_logged = 0u;
static uint32_t s_recovery_start_ms = 0u;
static uint32_t s_recovery_last_try_ms = 0u;
static uint32_t s_recovery_last_log_ms = 0u;
static uint16_t s_imu_stale_count = 0u;
static uint16_t s_imu_err_count = 0u;

void IMU_RequestRecovery(void)
{
    __atomic_store_n(&s_recovery_request, 1u, __ATOMIC_RELEASE);
}

uint8_t tskptt_imu_recovery_is_active(void)
{
    return s_recovery_active;
}

void tskptt_imu_recovery_on_fresh_sample(void)
{
    if (s_recovery_active != 0u)
    {
        LOGI("IMU recovery cleared by fresh sample.");
    }

    __atomic_store_n(&s_recovery_request, 0u, __ATOMIC_RELEASE);
    s_recovery_active = 0u;
    s_recovery_reset_done = 0u;
    s_recovery_timeout_logged = 0u;
    s_imu_stale_count = 0u;
    s_imu_err_count = 0u;
#if (CONF_IMU_HEATER_ENABLE != 0u)
    IMU_TempCtrl_SetEnable(1u);
#endif
    tskptt_imu_debug_sync(HAL_GetTick(), IMU_STATUS_OK);
}

void tskptt_imu_recovery_tick(uint32_t now_ms)
{
    uint8_t requested = __atomic_load_n(&s_recovery_request, __ATOMIC_ACQUIRE);
    if ((requested == 0u) && (s_recovery_active == 0u))
    {
        return;
    }

    if (s_recovery_active == 0u)
    {
        s_recovery_active = 1u;
        s_recovery_reset_done = 0u;
        s_recovery_timeout_logged = 0u;
        s_recovery_start_ms = now_ms;
        s_recovery_last_try_ms = 0u;
        s_recovery_last_log_ms = now_ms;
        g_omx_dbg_imu.recovery_count++;
        g_imu_app_state = IMU_APP_STATE_FAULT;
#if (CONF_IMU_HEATER_ENABLE != 0u)
        IMU_TempCtrl_SetEnable(0u);
#endif
        LIB_INS_InvalidatePublic();
        tskptt_imu_snapshot_set_internal_offline(now_ms);
        LOGW("IMU recovery requested.");
    }

    g_imu_app_state = IMU_APP_STATE_FAULT;

    if (((now_ms - s_recovery_start_ms) >= CONF_IMU_RECOVERY_TIMEOUT_MS) &&
        (s_recovery_timeout_logged == 0u))
    {
        s_recovery_timeout_logged = 1u;
        LOGE("IMU recovery still active after %lu ms.",
             (unsigned long)CONF_IMU_RECOVERY_TIMEOUT_MS);
    }

    if ((s_recovery_reset_done != 0u) &&
        ((now_ms - s_recovery_last_try_ms) < CONF_IMU_RECOVERY_RETRY_MS))
    {
        return;
    }
    s_recovery_last_try_ms = now_ms;

    imu_status_t rst = IMU_ResetRuntimeState();
    if (rst == IMU_STATUS_BUSY)
    {
        return;
    }
    if (rst != IMU_STATUS_OK)
    {
        LOGW("IMU recovery reset failed, st=%d", (int)rst);
        return;
    }

    LIB_INS_Reset();
    tskptt_imu_dt_reset();
    LIB_INS_InvalidatePublic();
    tskptt_imu_snapshot_set_internal_offline(now_ms);
    s_recovery_reset_done = 1u;
}

void tskptt_imu_handle_sample_fault(imu_status_t st, uint32_t now_ms)
{
    if (st == IMU_STATUS_STALE)
    {
        if (s_imu_stale_count < UINT16_MAX)
        {
            s_imu_stale_count++;
        }
        s_imu_err_count = 0u;
        IMU_RequestRecovery();

        if ((now_ms - s_recovery_last_log_ms) >= CONF_IMU_RECOVERY_LOG_MS)
        {
            s_recovery_last_log_ms = now_ms;
            LOGW("IMU sample stale, count=%u",
                 (unsigned)s_imu_stale_count);
        }

        if (s_imu_stale_count >= CONF_IMU_STALE_FATAL_COUNT)
        {
#if (CONF_IMU_HEATER_ENABLE != 0u)
            IMU_TempCtrl_Fault();
#endif
            g_imu_app_state = IMU_APP_STATE_FAULT;
        }
        return;
    }

    if (st == IMU_STATUS_ERR_BMI)
    {
        if (s_imu_err_count < UINT16_MAX)
        {
            s_imu_err_count++;
        }
        s_imu_stale_count = 0u;
        IMU_RequestRecovery();

        if ((now_ms - s_recovery_last_log_ms) >= CONF_IMU_RECOVERY_LOG_MS)
        {
            s_recovery_last_log_ms = now_ms;
            LOGW("IMU sample error, count=%u",
                 (unsigned)s_imu_err_count);
        }

        if (s_imu_err_count >= CONF_IMU_ERROR_FATAL_COUNT)
        {
#if (CONF_IMU_HEATER_ENABLE != 0u)
            IMU_TempCtrl_Fault();
#endif
            g_imu_app_state = IMU_APP_STATE_FAULT;
        }
    }
}
