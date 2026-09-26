#include "tskptt_imu_internal.h"

OMX_DEBUG_WATCH_BSS volatile omx_dbg_imu_t g_omx_dbg_imu;

void tskptt_imu_debug_init(void)
{
    omx_dbg_zero(&g_omx_dbg_imu, sizeof(g_omx_dbg_imu));
    omx_dbg_init(&g_omx_dbg_imu.h,
                 OMX_DBG_MODULE_IMU,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_imu));
}

void tskptt_imu_debug_sync(uint32_t now_ms, uint32_t status)
{
    (void)now_ms;
#if (CONF_IMU_HEATER_ENABLE != 0u)
    const uint32_t temp_stable =
        (uint32_t)(g_tskptt_imu_temp_ctrl_ready ? IMU_TempCtrl_IsStable() : 0u);
    imu_temp_pid_debug_t pid_debug;
    IMU_TempCtrl_GetPidDebug(&pid_debug);
    g_omx_dbg_imu.heater_pid_status = pid_debug.status;
    g_omx_dbg_imu.heater_pid_saturated = pid_debug.saturated;
    g_omx_dbg_imu.heater_pid_slew_limited = pid_debug.slew_limited;
    g_omx_dbg_imu.heater_fault_latched = pid_debug.fault_latched;
    g_omx_dbg_imu.heater_pid_error = pid_debug.error;
    g_omx_dbg_imu.heater_pid_p = pid_debug.p;
    g_omx_dbg_imu.heater_pid_i = pid_debug.i;
    g_omx_dbg_imu.heater_pid_d = pid_debug.d;
    g_omx_dbg_imu.heater_pid_raw_output = pid_debug.raw_output;
    g_omx_dbg_imu.heater_pid_output = pid_debug.output;
    g_omx_dbg_imu.heater_pid_dt_s = pid_debug.dt_s;
#else
    const uint32_t temp_stable = 0u;
#endif

    g_omx_dbg_imu.app_state = (uint32_t)g_imu_app_state;
    g_omx_dbg_imu.recovery_active = tskptt_imu_recovery_is_active();
    g_omx_dbg_imu.h.flags =
        ((uint32_t)g_imu_calib_mode) |
        (temp_stable << 8) |
        ((uint32_t)g_tskptt_imu_calib_done << 16);
    omx_dbg_end(&g_omx_dbg_imu.h, HAL_GetTick(), status);
}
