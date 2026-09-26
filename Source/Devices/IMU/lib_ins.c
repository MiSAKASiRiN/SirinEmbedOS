//
// lib_ins.c  —— IMU-based INS + Mahony wrapper
//

//特殊标记除外 默认使用rad进行计算

#include "SirinEmbedOS/Devices/IMU/lib_ins.h"
#include "SirinEmbedOS/Devices/IMU/lib_imu_project_config.h"
#include "SirinEmbedOS/Devices/IMU/mahony_filter.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "stm32h7xx.h"
#include <math.h>
#include <string.h>

#define INS_G                         CONF_IMU_INS_GRAVITY_MPS2
#define INS_DT_MIN_S                  CONF_IMU_INS_DT_MIN_S
#define INS_DT_MAX_S                  CONF_IMU_INS_DT_MAX_S
#define INS_STABLE_HOLD_S             CONF_IMU_INS_STABLE_HOLD_S
#define INS_STABLE_GYRO_MAX_RAD_S     CONF_IMU_INS_STABLE_GYRO_MAX_RAD_S
#define INS_MOTION_GYRO_FULL_RAD_S    CONF_IMU_INS_MOTION_GYRO_FULL_RAD_S
#define INS_MOTION_KP_MIN_WEIGHT      CONF_IMU_INS_MOTION_KP_MIN_WEIGHT
#define INS_MAHONY_KP_BASE            CONF_IMU_INS_MAHONY_KP
#define INS_MAHONY_KI_BASE            CONF_IMU_INS_MAHONY_KI
#define INS_ATTITUDE_SEED_SAMPLES     CONF_IMU_INS_ATTITUDE_SEED_SAMPLES

INS_t INS = {0};
OMX_DEBUG_WATCH_BSS volatile omx_dbg_ins_t g_omx_dbg_ins;

/* ---------- Local helpers ---------- */

static inline float clampf_local(float x, float lo, float hi)
{
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

static float ins_vec_norm3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float ins_acc_weight(float acc_norm)
{
    const float off_lo = 0.70f * INS_G;
    const float full_lo = 0.90f * INS_G;
    const float full_hi = 1.10f * INS_G;
    const float off_hi = 1.30f * INS_G;

    if (!isfinite(acc_norm) || (acc_norm <= off_lo) || (acc_norm >= off_hi))
    {
        return 0.0f;
    }
    if (acc_norm < full_lo)
    {
        return clampf_local((acc_norm - off_lo) / (full_lo - off_lo), 0.0f, 1.0f);
    }
    if (acc_norm <= full_hi)
    {
        return 1.0f;
    }
    return clampf_local((off_hi - acc_norm) / (off_hi - full_hi), 0.0f, 1.0f);
}

static float ins_motion_weight(float gyro_norm)
{
    if (!isfinite(gyro_norm))
    {
        return 0.0f;
    }
    if (gyro_norm <= INS_STABLE_GYRO_MAX_RAD_S)
    {
        return 1.0f;
    }
    if (gyro_norm >= INS_MOTION_GYRO_FULL_RAD_S)
    {
        return INS_MOTION_KP_MIN_WEIGHT;
    }

    const float span = INS_MOTION_GYRO_FULL_RAD_S - INS_STABLE_GYRO_MAX_RAD_S;
    const float t = (gyro_norm - INS_STABLE_GYRO_MAX_RAD_S) / span;
    return 1.0f + (INS_MOTION_KP_MIN_WEIGHT - 1.0f) * clampf_local(t, 0.0f, 1.0f);
}

static float ins_compute_mahony_kp(float acc_norm, float gyro_norm)
{
    return INS_MAHONY_KP_BASE * ins_acc_weight(acc_norm) * ins_motion_weight(gyro_norm);
}

static uint8_t ins_is_stationary(float acc_norm, float gyro_norm)
{
    return (uint8_t)((acc_norm >= (0.90f * INS_G)) &&
                     (acc_norm <= (1.10f * INS_G)) &&
                     (gyro_norm <= INS_STABLE_GYRO_MAX_RAD_S));
}

/* ---------- Internal static state ---------- */

/* INS state exposed via getter */
static lib_ins_state_t s_ins;

/* Mahony filter internal state */
static struct MAHONY_FILTER_t s_mahony;

/* Axis3f type is usually defined in mahony_filter.h */
static Axis3f s_gyro_axis;
static Axis3f s_accel_axis;

/* Startup gravity seed: average the first valid, calibrated body-frame samples. */
static uint8_t s_attitude_seeded = 0u;
static uint32_t s_seed_sample_count = 0u;
static float s_seed_accel_sum[3] = {0.0f, 0.0f, 0.0f};

/* Gravity vector in earth frame (这里简化为 +Z 方向重力) */
static float s_gravity_ef[3] = {0.0f, 0.0f, INS_G};

/* Accel low-pass parameter (same as your old INS.AccelLPF) */
static float s_accel_lpf = 0.0089f;

/* 收敛时间（秒）：在这段时间内处于 CONV 状态，不输出有效 R/P/Y_out，不积分 v/x */
static float s_warmup_time_s = CONF_IMU_INS_WARMUP_S;

/* Internal time accumulator */
static float s_time_acc_s = 0.0f;
static float s_stable_acc_s = 0.0f;

/* For yaw multi-turn integration */
static int   s_yaw_round_count = 0;
static float s_yaw_last_raw    = 0.0f;

/* 从 CONV -> RUN 或 Recenter 时记录，用于生成带 bias 的相对姿态。 */
static float s_q_reference[4] = {1.0f, 0.0f, 0.0f, 0.0f};

static float s_debug_gyro_int[3] = {0.0f, 0.0f, 0.0f};

static void ins_dbg_sync(float dt_s,
                         float acc_norm,
                         float gyro_norm,
                         float mahony_kp_eff)
{
    g_omx_dbg_ins.app_state = (uint32_t)s_ins.app_state;
    g_omx_dbg_ins.attitude_seeded = s_attitude_seeded;
    g_omx_dbg_ins.seed_sample_count = s_seed_sample_count;
    g_omx_dbg_ins.dt_s = dt_s;
    g_omx_dbg_ins.acc_norm = acc_norm;
    g_omx_dbg_ins.gyro_norm = gyro_norm;
    g_omx_dbg_ins.mahony_kp_eff = mahony_kp_eff;
    g_omx_dbg_ins.raw_euler_rad[0] = s_ins.roll;
    g_omx_dbg_ins.raw_euler_rad[1] = s_ins.pitch;
    g_omx_dbg_ins.raw_euler_rad[2] = s_ins.yaw;
    g_omx_dbg_ins.euler_rad[0] = s_ins.roll_out;
    g_omx_dbg_ins.euler_rad[1] = s_ins.pitch_out;
    g_omx_dbg_ins.euler_rad[2] = s_ins.yaw_out;
    g_omx_dbg_ins.gyro_only_rad[0] = s_debug_gyro_int[0];
    g_omx_dbg_ins.gyro_only_rad[1] = s_debug_gyro_int[1];
    g_omx_dbg_ins.gyro_only_rad[2] = s_debug_gyro_int[2];
    omx_dbg_end(&g_omx_dbg_ins.h, HAL_GetTick(), (uint32_t)s_ins.app_state);
}

static void ins_reset_attitude_seed(void)
{
    s_attitude_seeded = 0u;
    s_seed_sample_count = 0u;
    s_seed_accel_sum[0] = 0.0f;
    s_seed_accel_sum[1] = 0.0f;
    s_seed_accel_sum[2] = 0.0f;
}

static uint8_t ins_accumulate_attitude_seed(const imu_sample_t *imu)
{
    if ((imu == NULL) || (s_attitude_seeded != 0u))
    {
        return s_attitude_seeded;
    }

    s_seed_accel_sum[0] += imu->accel[0];
    s_seed_accel_sum[1] += imu->accel[1];
    s_seed_accel_sum[2] += imu->accel[2];
    s_seed_sample_count++;

    if (s_seed_sample_count < INS_ATTITUDE_SEED_SAMPLES)
    {
        return 0u;
    }

    const float inv_count = 1.0f / (float)s_seed_sample_count;
    Axis3f mean_accel = {
        .x = s_seed_accel_sum[0] * inv_count,
        .y = s_seed_accel_sum[1] * inv_count,
        .z = s_seed_accel_sum[2] * inv_count,
    };

    if (mahony_seed_from_accel(&s_mahony, mean_accel) == 0u)
    {
        ins_reset_attitude_seed();
        return 0u;
    }

    s_attitude_seeded = 1u;
    s_time_acc_s = 0.0f;
    s_stable_acc_s = 0.0f;
    s_yaw_round_count = 0;
    s_yaw_last_raw = s_mahony.yaw;
    s_debug_gyro_int[0] = 0.0f;
    s_debug_gyro_int[1] = 0.0f;
    s_debug_gyro_int[2] = 0.0f;
    return 1u;
}

static void ins_sync_public(const imu_sample_t *imu)
{
    if (imu == NULL)
    {
        return;
    }

    INS.Accel[0] = imu->accel[0];
    INS.Accel[1] = imu->accel[1];
    INS.Accel[2] = imu->accel[2];

    INS.Gyro[0] = imu->gyro[0];
    INS.Gyro[1] = imu->gyro[1];
    INS.Gyro[2] = imu->gyro[2];

    INS.Roll = s_ins.roll;
    INS.Pitch = s_ins.pitch;
    INS.Yaw = s_ins.yaw;
    INS.RollRelative = s_ins.roll_out;
    INS.PitchRelative = s_ins.pitch_out;
    INS.YawRelative = s_ins.yaw_out;
    INS.YawTotalAngle = s_ins.yaw_total;

    const float two_pi = 2.0f * (float)M_PI;
    INS.YawRoundCount = (int32_t)(INS.YawTotalAngle / two_pi);
    INS.YawAngleLast = INS.Yaw;

    INS.MotionAccel_b[0] = s_ins.motion_accel_b[0];
    INS.MotionAccel_b[1] = s_ins.motion_accel_b[1];
    INS.MotionAccel_b[2] = s_ins.motion_accel_b[2];

    INS.MotionAccel_n[0] = s_ins.motion_accel_n[0];
    INS.MotionAccel_n[1] = s_ins.motion_accel_n[1];
    INS.MotionAccel_n[2] = s_ins.motion_accel_n[2];

    INS.v_n = s_ins.v_n;
    INS.x_n = s_ins.x_n;
    INS.ins_flag = (s_ins.app_state == LIB_INS_APP_STATE_RUN) ? 1u : 0u;

    INS.q[0] = s_ins.q[0];
    INS.q[1] = s_ins.q[1];
    INS.q[2] = s_ins.q[2];
    INS.q[3] = s_ins.q[3];
    INS.q_relative[0] = s_ins.q_relative[0];
    INS.q_relative[1] = s_ins.q_relative[1];
    INS.q_relative[2] = s_ins.q_relative[2];
    INS.q_relative[3] = s_ins.q_relative[3];
}

/* ---------- Quaternion frame transforms ---------- */

/**
 * @brief Transform 3D vector from BodyFrame to EarthFrame using quaternion.
 *
 * @param vecBF  Input vector in body frame.
 * @param vecEF  Output vector in earth frame.
 * @param q      Quaternion [w, x, y, z].
 */
static void BodyFrameToEarthFrame(const float *vecBF, float *vecEF, const float *q)
{
    vecEF[0] = 2.0f * ((0.5f - q[2] * q[2] - q[3] * q[3]) * vecBF[0] +
                       (q[1] * q[2] + q[0] * q[3]) * vecBF[1] +
                       (q[1] * q[3] - q[0] * q[2]) * vecBF[2]);

    vecEF[1] = 2.0f * ((q[1] * q[2] - q[0] * q[3]) * vecBF[0] +
                       (0.5f - q[1] * q[1] - q[3] * q[3]) * vecBF[1] +
                       (q[2] * q[3] + q[0] * q[1]) * vecBF[2]);

    vecEF[2] = 2.0f * ((q[1] * q[3] + q[0] * q[2]) * vecBF[0] +
                       (q[2] * q[3] - q[0] * q[1]) * vecBF[1] +
                       (0.5f - q[1] * q[1] - q[2] * q[2]) * vecBF[2]);
}

/**
 * @brief Transform 3D vector from EarthFrame to BodyFrame using quaternion.
 *
 * @param vecEF  Input vector in earth frame.
 * @param vecBF  Output vector in body frame.
 * @param q      Quaternion [w, x, y, z].
 */
static void EarthFrameToBodyFrame(const float *vecEF, float *vecBF, const float *q)
{
    vecBF[0] = 2.0f * ((0.5f - q[2] * q[2] - q[3] * q[3]) * vecEF[0] +
                       (q[1] * q[2] - q[0] * q[3]) * vecEF[1] +
                       (q[1] * q[3] + q[0] * q[2]) * vecEF[2]);

    vecBF[1] = 2.0f * ((q[1] * q[2] + q[0] * q[3]) * vecEF[0] +
                       (0.5f - q[1] * q[1] - q[3] * q[3]) * vecEF[1] +
                       (q[2] * q[3] - q[0] * q[1]) * vecEF[2]);

    vecBF[2] = 2.0f * ((q[1] * q[3] - q[0] * q[2]) * vecEF[0] +
                       (q[2] * q[3] + q[0] * q[1]) * vecEF[1] +
                       (0.5f - q[1] * q[1] - q[2] * q[2]) * vecEF[2]);
}

static void ins_set_identity_quat(float q[4])
{
    q[0] = 1.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 0.0f;
}

static void ins_capture_attitude_reference(void)
{
    s_q_reference[0] = s_ins.q[0];
    s_q_reference[1] = s_ins.q[1];
    s_q_reference[2] = s_ins.q[2];
    s_q_reference[3] = s_ins.q[3];
}

static void ins_update_relative_attitude(void)
{
    /* q_relative = conjugate(q_reference) * q_current */
    const float aw = s_q_reference[0];
    const float ax = -s_q_reference[1];
    const float ay = -s_q_reference[2];
    const float az = -s_q_reference[3];
    const float bw = s_ins.q[0];
    const float bx = s_ins.q[1];
    const float by = s_ins.q[2];
    const float bz = s_ins.q[3];

    float q[4] = {
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    };
    const float norm = sqrtf(q[0] * q[0] + q[1] * q[1] +
                             q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(norm) || (norm <= 1.0e-6f))
    {
        ins_set_identity_quat(s_ins.q_relative);
        s_ins.roll_out = 0.0f;
        s_ins.pitch_out = 0.0f;
        s_ins.yaw_out = 0.0f;
        return;
    }

    const float inv_norm = 1.0f / norm;
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        s_ins.q_relative[i] = q[i] * inv_norm;
    }

    const float qw = s_ins.q_relative[0];
    const float qx = s_ins.q_relative[1];
    const float qy = s_ins.q_relative[2];
    const float qz = s_ins.q_relative[3];
    float sin_pitch = -2.0f * (qx * qz - qw * qy);
    sin_pitch = clampf_local(sin_pitch, -1.0f, 1.0f);
    s_ins.roll_out = atan2f(2.0f * (qy * qz + qw * qx),
                            1.0f - 2.0f * (qx * qx + qy * qy));
    s_ins.pitch_out = asinf(sin_pitch);
    s_ins.yaw_out = atan2f(2.0f * (qx * qy + qw * qz),
                           1.0f - 2.0f * (qy * qy + qz * qz));
}

static void ins_clear_relative_attitude(void)
{
    ins_set_identity_quat(s_ins.q_relative);
    s_ins.roll_out = 0.0f;
    s_ins.pitch_out = 0.0f;
    s_ins.yaw_out = 0.0f;
}

/* ---------- Public APIs ---------- */

void LIB_INS_Init(void)
{
    memset(&s_ins, 0, sizeof(s_ins));
    omx_dbg_zero(&g_omx_dbg_ins, sizeof(g_omx_dbg_ins));
    omx_dbg_init(&g_omx_dbg_ins.h,
                 OMX_DBG_MODULE_INS,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_ins));

    s_ins.q[0] = 1.0f;
    s_ins.q[1] = 0.0f;
    s_ins.q[2] = 0.0f;
    s_ins.q[3] = 0.0f;
    ins_set_identity_quat(s_ins.q_relative);
    ins_set_identity_quat(s_q_reference);

    s_ins.app_state = LIB_INS_APP_STATE_CONV;   /* 一上来就认为在收敛阶段 */

    s_time_acc_s       = 0.0f;
    s_stable_acc_s     = 0.0f;
    s_yaw_round_count  = 0;
    s_yaw_last_raw     = 0.0f;

    ins_reset_attitude_seed();
    s_debug_gyro_int[0] = 0.0f;
    s_debug_gyro_int[1] = 0.0f;
    s_debug_gyro_int[2] = 0.0f;

    mahony_init(&s_mahony, INS_MAHONY_KP_BASE, INS_MAHONY_KI_BASE, 0.001f);

    /* 预设输出姿态为 0，避免上层看到垃圾值 */
    ins_clear_relative_attitude();
    s_ins.yaw_total = 0.0f;
    ins_dbg_sync(0.0f, 0.0f, 0.0f, INS_MAHONY_KP_BASE);
}

void LIB_INS_Reset(void)
{
    LIB_INS_Init();
}

void LIB_INS_InvalidatePublic(void)
{
    memset((void *)&INS, 0, sizeof(INS));
}

void LIB_INS_SetWarmupTime(float warmup_s)
{
    if (warmup_s < 0.1f)
        warmup_s = 0.1f;
    if (warmup_s > 30.0f)
        warmup_s = 30.0f;
    s_warmup_time_s = warmup_s;
}

/* 在当前姿态上重新定义零点 */
void LIB_INS_Recenter(void)
{
    /* 将当前原始四元数当作新的 reference */
    ins_capture_attitude_reference();
    ins_clear_relative_attitude();

    /* 同时重置 yaw 多圈、速度、位移 */
    s_yaw_round_count = 0;
    s_yaw_last_raw    = s_ins.yaw;
    s_ins.v_n         = 0.0f;
    s_ins.x_n         = 0.0f;
    s_ins.yaw_total   = 0.0f;
    s_debug_gyro_int[0] = 0.0f;
    s_debug_gyro_int[1] = 0.0f;
    s_debug_gyro_int[2] = 0.0f;
    g_omx_dbg_ins.gyro_only_rad[0] = 0.0f;
    g_omx_dbg_ins.gyro_only_rad[1] = 0.0f;
    g_omx_dbg_ins.gyro_only_rad[2] = 0.0f;
    ins_dbg_sync(g_omx_dbg_ins.dt_s,
                 g_omx_dbg_ins.acc_norm,
                 g_omx_dbg_ins.gyro_norm,
                 g_omx_dbg_ins.mahony_kp_eff);
}

void LIB_INS_Update(const imu_sample_t *imu, float dt_s)
{
    if (!imu || dt_s <= 0.0f)
    {
        return;
    }

    dt_s = clampf_local(dt_s, INS_DT_MIN_S, INS_DT_MAX_S);

    for (uint8_t i = 0; i < 3; ++i)
    {
        if (!isfinite(imu->accel[i]) || !isfinite(imu->gyro[i]))
        {
            return;
        }
    }

    /* 1. Copy IMU data into internal state (body frame) */

    s_ins.accel_b[0] = imu->accel[0];
    s_ins.accel_b[1] = imu->accel[1];
    s_ins.accel_b[2] = imu->accel[2];

    s_ins.gyro_b[0] = imu->gyro[0];
    s_ins.gyro_b[1] = imu->gyro[1];
    s_ins.gyro_b[2] = imu->gyro[2];

    float acc_norm = ins_vec_norm3(s_ins.accel_b);
    float gyro_norm = ins_vec_norm3(s_ins.gyro_b);

    if ((s_attitude_seeded == 0u) && (ins_accumulate_attitude_seed(imu) == 0u))
    {
        g_omx_dbg_ins.update_count++;
        ins_sync_public(imu);
        ins_dbg_sync(dt_s, acc_norm, gyro_norm, 0.0f);
        return;
    }

    s_time_acc_s += dt_s;

    s_debug_gyro_int[0] += s_ins.gyro_b[0] * dt_s;
    s_debug_gyro_int[1] += s_ins.gyro_b[1] * dt_s;
    s_debug_gyro_int[2] += s_ins.gyro_b[2] * dt_s;

    /* 2. Feed Mahony filter (gyro is rad/s) */

    s_gyro_axis.x = s_ins.gyro_b[0];
    s_gyro_axis.y = s_ins.gyro_b[1];
    s_gyro_axis.z = s_ins.gyro_b[2];

    s_accel_axis.x = s_ins.accel_b[0];
    s_accel_axis.y = s_ins.accel_b[1];
    s_accel_axis.z = s_ins.accel_b[2];

    s_mahony.dt = dt_s;
    float mahony_kp_eff = ins_compute_mahony_kp(acc_norm, gyro_norm);
    mahony_set_gain(&s_mahony, mahony_kp_eff, INS_MAHONY_KI_BASE);
    mahony_input(&s_mahony, s_gyro_axis, s_accel_axis);
    mahony_update(&s_mahony);
    mahony_output(&s_mahony);

    /* 3. Update quaternion & raw euler (rad) */

    s_ins.q[0] = s_mahony.q0;
    s_ins.q[1] = s_mahony.q1;
    s_ins.q[2] = s_mahony.q2;
    s_ins.q[3] = s_mahony.q3;

    s_ins.roll  = s_mahony.roll;
    s_ins.pitch = s_mahony.pitch;
    s_ins.yaw   = s_mahony.yaw;

    /* 4. Gravity compensation and accel low-pass in body frame */

    float gravity_b[3];
    EarthFrameToBodyFrame(s_gravity_ef, gravity_b, s_ins.q);

    for (uint8_t i = 0; i < 3; i++)
    {
        float acc_no_g = s_ins.accel_b[i] - gravity_b[i];
        float alpha    = dt_s / (s_accel_lpf + dt_s);

        s_ins.motion_accel_b[i] =
            acc_no_g * alpha +
            s_ins.motion_accel_b[i] * (1.0f - alpha);
    }

    /* 5. Transform motion accel to earth frame */

    BodyFrameToEarthFrame(s_ins.motion_accel_b, s_ins.motion_accel_n, s_ins.q);

    /* 6. Deadzones */

    if (fabsf(s_ins.motion_accel_n[0]) < 0.02f)
    {
        s_ins.motion_accel_n[0] = 0.0f;
    }
    if (fabsf(s_ins.motion_accel_n[1]) < 0.02f)
    {
        s_ins.motion_accel_n[1] = 0.0f;
    }
    if (fabsf(s_ins.motion_accel_n[2]) < 0.04f)
    {
        s_ins.motion_accel_n[2] = 0.0f;
    }

    /* 7. 状态机：从 CONV -> RUN 的切换逻辑 */

    if (s_ins.app_state == LIB_INS_APP_STATE_CONV)
    {
        if (ins_is_stationary(acc_norm, gyro_norm))
        {
            s_stable_acc_s += dt_s;
        }
        else
        {
            s_stable_acc_s = 0.0f;
        }

        if ((s_time_acc_s >= s_warmup_time_s) &&
            (s_stable_acc_s >= INS_STABLE_HOLD_S))
        {
            /* 在进入 RUN 的瞬间，将当前四元数视作相对零点 */
            ins_capture_attitude_reference();

            s_yaw_round_count = 0;
            s_yaw_last_raw    = s_ins.yaw;

            s_ins.v_n       = 0.0f;
            s_ins.x_n       = 0.0f;
            s_ins.yaw_total = 0.0f;
            s_debug_gyro_int[0] = 0.0f;
            s_debug_gyro_int[1] = 0.0f;
            s_debug_gyro_int[2] = 0.0f;

            s_ins.app_state = LIB_INS_APP_STATE_RUN;
        }
    }

    /* 8. 根据当前 app_state 决定对外 R/P/Y_out & v/x/yaw_total 的行为 */

    if (s_ins.app_state == LIB_INS_APP_STATE_RUN)
    {
        ins_update_relative_attitude();

        /* Multi-turn tracking in raw yaw space */
        float dyaw_raw = s_ins.yaw - s_yaw_last_raw;
        if (dyaw_raw > (float)M_PI)
        {
            s_yaw_round_count--;
        }
        else if (dyaw_raw < -(float)M_PI)
        {
            s_yaw_round_count++;
        }
        s_yaw_last_raw = s_ins.yaw;

        float yaw_total_phys =
            2.0f * (float)M_PI * (float)s_yaw_round_count + s_ins.yaw_out;

        /* Directly export in rad */
        s_ins.yaw_total = yaw_total_phys;

        /* Velocity / position integration (unchanged) */
        s_ins.v_n += s_ins.motion_accel_n[1] * dt_s;
        s_ins.x_n += s_ins.v_n * dt_s;
    }
    else
    {
        ins_clear_relative_attitude();
    }

    g_omx_dbg_ins.update_count++;
    ins_sync_public(imu);
    ins_dbg_sync(dt_s, acc_norm, gyro_norm, mahony_kp_eff);
}

const lib_ins_state_t *LIB_INS_GetState(void)
{
    return &s_ins;
}
