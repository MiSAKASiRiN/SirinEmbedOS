#include "tskptt_imu_internal.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include <math.h>

static uint32_t s_last_ins_tick = 0u;
static DWT_DeltaTimer_t s_ins_dwt_timer;
static uint8_t s_last_ins_dwt_seeded = 0u;
static uint8_t s_dwt_fallback_logged = 0u;

static float tskptt_imu_clamp_dt_s(float dt_s)
{
    if (!isfinite(dt_s) || (dt_s <= 0.0f))
    {
        return CONF_IMU_TASK_PERIOD_MS / 1000.0f;
    }
    if (dt_s < CONF_IMU_INS_DT_MIN_S)
    {
        return CONF_IMU_INS_DT_MIN_S;
    }
    if (dt_s > CONF_IMU_INS_DT_MAX_S)
    {
        return CONF_IMU_INS_DT_MAX_S;
    }
    return dt_s;
}

void tskptt_imu_dt_init(void)
{
    DWT_Init(0u);
    if (!DWT_IsReady())
    {
        LOGW("DWT init failed, IMU INS dt will use HAL tick fallback.");
    }
    DWT_DeltaTimerInit(&s_ins_dwt_timer);
}

void tskptt_imu_dt_reset(void)
{
    s_last_ins_tick = 0u;
    DWT_DeltaTimerSeed(&s_ins_dwt_timer);
    s_last_ins_dwt_seeded = 0u;
}

float tskptt_imu_dt_compute(uint32_t now_ms)
{
    if (DWT_IsReady())
    {
        if (s_last_ins_dwt_seeded == 0u)
        {
            DWT_DeltaTimerSeed(&s_ins_dwt_timer);
            s_last_ins_dwt_seeded = 1u;
            s_last_ins_tick = now_ms;
            return CONF_IMU_TASK_PERIOD_MS / 1000.0f;
        }

        float dt_s = DWT_DeltaTimerElapsedS(&s_ins_dwt_timer);
        s_last_ins_tick = now_ms;
        return tskptt_imu_clamp_dt_s(dt_s);
    }

    if (s_dwt_fallback_logged == 0u)
    {
        s_dwt_fallback_logged = 1u;
        LOGW("DWT not ready, IMU INS dt falls back to HAL tick.");
    }

    if (s_last_ins_tick == 0u)
    {
        s_last_ins_tick = now_ms;
        return CONF_IMU_TASK_PERIOD_MS / 1000.0f;
    }

    uint32_t delta_ms = now_ms - s_last_ins_tick;
    if (delta_ms == 0u)
    {
        delta_ms = CONF_IMU_TASK_PERIOD_MS;
    }

    s_last_ins_tick = now_ms;
    return tskptt_imu_clamp_dt_s(delta_ms / 1000.0f);
}
