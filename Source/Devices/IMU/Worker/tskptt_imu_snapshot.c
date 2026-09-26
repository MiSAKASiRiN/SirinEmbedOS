#include "tskptt_imu_internal.h"
#include <string.h>

static OmxImuState s_omx_cache;
static volatile uint32_t s_omx_cache_seq = 0u;

static imu_task_snapshot_t s_imu_snapshot;
static volatile uint32_t s_imu_snapshot_seq = 0u;

void tskptt_imu_omx_publish(const OmxImuState *in)
{
    if (in == NULL)
    {
        return;
    }

    __atomic_fetch_add(&s_omx_cache_seq, 1u, __ATOMIC_ACQ_REL);
    s_omx_cache = *in;
    __atomic_fetch_add(&s_omx_cache_seq, 1u, __ATOMIC_RELEASE);
}

bool IMU_GetOmxImuState(OmxImuState *out)
{
    if (out == NULL)
    {
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint32_t s1 = __atomic_load_n(&s_omx_cache_seq, __ATOMIC_ACQUIRE);
        if ((s1 & 1u) != 0u)
        {
            continue;
        }

        OmxImuState tmp = s_omx_cache;

        uint32_t s2 = __atomic_load_n(&s_omx_cache_seq, __ATOMIC_ACQUIRE);
        if (s1 == s2)
        {
            *out = tmp;
            return true;
        }
    }

    return false;
}

void tskptt_imu_snapshot_update_internal(const imu_sample_t *samp,
                                         const lib_ins_state_t *ins,
                                         uint32_t now_ms,
                                         uint8_t online)
{
    if ((samp == NULL) || (ins == NULL))
    {
        return;
    }

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_ACQ_REL);

    imu_task_snapshot_t s = s_imu_snapshot;
    for (int i = 0; i < 3; ++i)
    {
        s.internal.accel[i] = samp->accel[i];
        s.internal.gyro[i] = samp->gyro[i];
        s.internal.euler[i] = (i == 0) ? ins->roll :
                              (i == 1) ? ins->pitch : ins->yaw;
        s.internal.euler_relative[i] = (i == 0) ? ins->roll_out :
                                       (i == 1) ? ins->pitch_out : ins->yaw_out;
    }
    for (int i = 0; i < 4; ++i)
    {
        s.internal.quat[i] = ins->q[i];
        s.internal.quat_relative[i] = ins->q_relative[i];
    }
    s.internal.last_update_ms = now_ms;
    s.internal.online = online;
    s.internal.valid = 1u;
    s_imu_snapshot = s;

    g_omx_dbg_imu.internal_euler_rad[0] = ins->roll;
    g_omx_dbg_imu.internal_euler_rad[1] = ins->pitch;
    g_omx_dbg_imu.internal_euler_rad[2] = ins->yaw;
    g_omx_dbg_imu.internal_online = online;

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_RELEASE);
}

void tskptt_imu_snapshot_set_internal_offline(uint32_t now_ms)
{
    (void)now_ms;

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_ACQ_REL);

    imu_task_snapshot_t s = s_imu_snapshot;
    memset(s.internal.accel, 0, sizeof(s.internal.accel));
    memset(s.internal.gyro, 0, sizeof(s.internal.gyro));
    memset(s.internal.quat, 0, sizeof(s.internal.quat));
    memset(s.internal.euler, 0, sizeof(s.internal.euler));
    memset(s.internal.quat_relative, 0, sizeof(s.internal.quat_relative));
    memset(s.internal.euler_relative, 0, sizeof(s.internal.euler_relative));
    s.internal.online = 0u;
    s.internal.valid = 0u;
    s_imu_snapshot = s;

    g_omx_dbg_imu.internal_euler_rad[0] = 0.0f;
    g_omx_dbg_imu.internal_euler_rad[1] = 0.0f;
    g_omx_dbg_imu.internal_euler_rad[2] = 0.0f;
    g_omx_dbg_imu.internal_online = 0u;

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_RELEASE);
}

void tskptt_imu_snapshot_update_external(const OmxImuState *ext, uint32_t now_ms)
{
    if (ext == NULL)
    {
        return;
    }

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_ACQ_REL);

    imu_task_snapshot_t s = s_imu_snapshot;
    for (int i = 0; i < 3; ++i)
    {
        s.external.accel[i] = ext->accel[i];
        s.external.gyro[i] = ext->gyro[i];
        s.external.euler[i] = ext->euler[i];
        s.external.euler_relative[i] = 0.0f;
    }
    for (int i = 0; i < 4; ++i)
    {
        s.external.quat[i] = ext->quat[i];
        s.external.quat_relative[i] = (i == 0) ? 1.0f : 0.0f;
    }
    s.external.last_update_ms = (ext->last_rx_ms != 0u) ? ext->last_rx_ms : now_ms;
    s.external.online = ext->is_online ? 1u : 0u;
    s.external.valid = 1u;
    s_imu_snapshot = s;

    g_omx_dbg_imu.external_euler_rad[0] = ext->euler[0];
    g_omx_dbg_imu.external_euler_rad[1] = ext->euler[1];
    g_omx_dbg_imu.external_euler_rad[2] = ext->euler[2];
    g_omx_dbg_imu.external_online = ext->is_online ? 1u : 0u;

    __atomic_fetch_add(&s_imu_snapshot_seq, 1u, __ATOMIC_RELEASE);
}

bool IMU_GetSnapshot(imu_task_snapshot_t *out)
{
    if (out == NULL)
    {
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint32_t s1 = __atomic_load_n(&s_imu_snapshot_seq, __ATOMIC_ACQUIRE);
        if ((s1 & 1u) != 0u)
        {
            continue;
        }

        imu_task_snapshot_t tmp = s_imu_snapshot;

        uint32_t s2 = __atomic_load_n(&s_imu_snapshot_seq, __ATOMIC_ACQUIRE);
        if (s1 == s2)
        {
            *out = tmp;
            return true;
        }
    }

    return false;
}
