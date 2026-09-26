#include "SirinEmbedOS/Devices/IMU/lib_ins.h"
#include "SirinEmbedOS/Devices/IMU/mahony_filter.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEST_G (9.80665f)
#define TEST_EPS (1.0e-4f)

static uint32_t s_test_tick_ms = 0u;

uint32_t HAL_GetTick(void)
{
    return s_test_tick_ms;
}

static void fail(const char *message, const char *file, int line)
{
    (void)fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    exit(EXIT_FAILURE);
}

#define TEST_ASSERT(condition, message) \
    do { if (!(condition)) fail((message), __FILE__, __LINE__); } while (0)

static float wrap_pi_test(float angle)
{
    while (angle > (float)M_PI) angle -= 2.0f * (float)M_PI;
    while (angle <= -(float)M_PI) angle += 2.0f * (float)M_PI;
    return angle;
}

static Axis3f gravity_from_roll_pitch(float roll, float pitch)
{
    Axis3f gravity = {
        .x = -sinf(pitch) * TEST_G,
        .y = sinf(roll) * cosf(pitch) * TEST_G,
        .z = cosf(roll) * cosf(pitch) * TEST_G,
    };
    return gravity;
}

static float gravity_alignment(const struct MAHONY_FILTER_t *filter, Axis3f accel)
{
    const float norm = sqrtf(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
    TEST_ASSERT(norm > 1.0e-6f, "test acceleration must be non-zero");
    return filter->rMat[2][0] * accel.x / norm +
           filter->rMat[2][1] * accel.y / norm +
           filter->rMat[2][2] * accel.z / norm;
}

static void set_filter_from_euler(struct MAHONY_FILTER_t *filter,
                                  float roll,
                                  float pitch,
                                  float yaw)
{
    const float cr = cosf(0.5f * roll);
    const float sr = sinf(0.5f * roll);
    const float cp = cosf(0.5f * pitch);
    const float sp = sinf(0.5f * pitch);
    const float cy = cosf(0.5f * yaw);
    const float sy = sinf(0.5f * yaw);

    filter->q0 = cr * cp * cy + sr * sp * sy;
    filter->q1 = sr * cp * cy - cr * sp * sy;
    filter->q2 = cr * sp * cy + sr * cp * sy;
    filter->q3 = cr * cp * sy - sr * sp * cy;
    RotationMatrix_update(filter);
    mahony_output(filter);
}

static void test_seed_pose(float roll, float pitch)
{
    struct MAHONY_FILTER_t filter;
    mahony_init(&filter, 1.0f, 0.0f, 0.001f);
    const Axis3f gravity = gravity_from_roll_pitch(roll, pitch);

    TEST_ASSERT(mahony_seed_from_accel(&filter, gravity) != 0u,
                "gravity seed should succeed");
    TEST_ASSERT(gravity_alignment(&filter, gravity) > 0.9999f,
                "seeded quaternion must align predicted and measured gravity");

    const float q_norm = sqrtf(filter.q0 * filter.q0 + filter.q1 * filter.q1 +
                               filter.q2 * filter.q2 + filter.q3 * filter.q3);
    TEST_ASSERT(fabsf(q_norm - 1.0f) < TEST_EPS,
                "seeded quaternion must be normalized");
    TEST_ASSERT(isfinite(filter.roll) && isfinite(filter.pitch) && isfinite(filter.yaw),
                "seeded Euler angles must remain finite");
    TEST_ASSERT(fabsf(wrap_pi_test(filter.yaw)) < TEST_EPS,
                "gravity seed must choose yaw zero");
}

static void test_seed_large_poses(void)
{
    test_seed_pose(0.0f, 0.0f);
    test_seed_pose(60.0f / 180.0f * (float)M_PI, 0.0f);
    test_seed_pose(-60.0f / 180.0f * (float)M_PI, 0.0f);
    test_seed_pose(0.0f, 70.0f / 180.0f * (float)M_PI);
    test_seed_pose(0.0f, -70.0f / 180.0f * (float)M_PI);
    test_seed_pose(35.0f / 180.0f * (float)M_PI,
                   -55.0f / 180.0f * (float)M_PI);
    test_seed_pose(0.0f, 89.0f / 180.0f * (float)M_PI);
    test_seed_pose((float)M_PI, 0.0f);

    struct MAHONY_FILTER_t filter;
    mahony_init(&filter, 1.0f, 0.0f, 0.001f);
    Axis3f zero = {0.0f, 0.0f, 0.0f};
    TEST_ASSERT(mahony_seed_from_accel(&filter, zero) == 0u,
                "zero acceleration seed must fail");
}

static void test_yaw_remains_unobservable(void)
{
    struct MAHONY_FILTER_t filter;
    mahony_init(&filter, 1.0f, 0.0f, 0.001f);
    const float roll = 40.0f / 180.0f * (float)M_PI;
    const float pitch = -25.0f / 180.0f * (float)M_PI;
    const float yaw = 75.0f / 180.0f * (float)M_PI;
    const Axis3f gravity = gravity_from_roll_pitch(roll, pitch);
    const Axis3f zero_gyro = {0.0f, 0.0f, 0.0f};
    set_filter_from_euler(&filter, roll, pitch, yaw);

    for (int i = 0; i < 1000; ++i)
    {
        mahony_input(&filter, zero_gyro, gravity);
        mahony_update(&filter);
        mahony_output(&filter);
    }

    TEST_ASSERT(fabsf(wrap_pi_test(filter.yaw - yaw)) < 1.0e-3f,
                "accelerometer feedback must not create an absolute yaw reference");
}

static void test_three_axis_gravity_correction(void)
{
    struct MAHONY_FILTER_t filter;
    mahony_init(&filter, 2.0f, 0.0f, 0.001f);
    set_filter_from_euler(&filter,
                          70.0f / 180.0f * (float)M_PI,
                          25.0f / 180.0f * (float)M_PI,
                          0.0f);

    const Axis3f target_gravity = gravity_from_roll_pitch(
        20.0f / 180.0f * (float)M_PI,
        -35.0f / 180.0f * (float)M_PI);
    const Axis3f zero_gyro = {0.0f, 0.0f, 0.0f};
    const float initial_alignment = gravity_alignment(&filter, target_gravity);

    const float ax = target_gravity.x / TEST_G;
    const float ay = target_gravity.y / TEST_G;
    const float predicted_x = filter.rMat[2][0];
    const float predicted_y = filter.rMat[2][1];
    const float ez = ax * predicted_y - ay * predicted_x;
    TEST_ASSERT(fabsf(ez) > 0.05f,
                "test pose must exercise a non-zero body-Z correction component");

    for (int i = 0; i < 2000; ++i)
    {
        mahony_input(&filter, zero_gyro, target_gravity);
        mahony_update(&filter);
        mahony_output(&filter);
    }

    TEST_ASSERT(gravity_alignment(&filter, target_gravity) > initial_alignment + 0.2f,
                "full three-axis correction must reduce large-attitude gravity error");
    TEST_ASSERT(gravity_alignment(&filter, target_gravity) > 0.995f,
                "full three-axis correction must converge to gravity");
    TEST_ASSERT(isfinite(filter.q0) && isfinite(filter.q1) &&
                isfinite(filter.q2) && isfinite(filter.q3),
                "large-attitude correction must keep quaternion finite");
}

static imu_sample_t make_static_sample(float roll, float pitch)
{
    const Axis3f gravity = gravity_from_roll_pitch(roll, pitch);
    imu_sample_t sample = {
        .gyro = {0.0f, 0.0f, 0.0f},
        .accel = {gravity.x, gravity.y, gravity.z},
        .temperature = 25.0f,
    };
    return sample;
}

static void test_ins_seed_window_and_relative_output(void)
{
    const float roll = 30.0f / 180.0f * (float)M_PI;
    const float pitch = -50.0f / 180.0f * (float)M_PI;
    const imu_sample_t sample = make_static_sample(roll, pitch);

    s_test_tick_ms = 0u;
    LIB_INS_Init();
    for (int i = 0; i < 19; ++i)
    {
        s_test_tick_ms++;
        LIB_INS_Update(&sample, 0.001f);
    }

    const lib_ins_state_t *state = LIB_INS_GetState();
    TEST_ASSERT(g_omx_dbg_ins.attitude_seeded == 0u,
                "INS must not seed before 20 valid samples");
    TEST_ASSERT(g_omx_dbg_ins.seed_sample_count == 19u,
                "INS seed counter must track valid samples");
    TEST_ASSERT(state->app_state == LIB_INS_APP_STATE_CONV,
                "INS must remain in convergence before seeding");
    TEST_ASSERT(state->roll_out == 0.0f && state->pitch_out == 0.0f,
                "relative outputs must remain zero during convergence");

    s_test_tick_ms++;
    LIB_INS_Update(&sample, 0.001f);
    state = LIB_INS_GetState();
    TEST_ASSERT(g_omx_dbg_ins.attitude_seeded == 1u,
                "INS must seed on the twentieth valid sample");
    TEST_ASSERT(g_omx_dbg_ins.seed_sample_count == 20u,
                "successful seed must retain the completed sample count");
    TEST_ASSERT(fabsf(state->roll - roll) < 1.0e-3f,
                "raw roll must jump directly to the gravity tilt");
    TEST_ASSERT(fabsf(state->pitch - pitch) < 1.0e-3f,
                "raw pitch must jump directly to the gravity tilt");
    const float seeded_pitch = state->pitch;

    for (int i = 0; i < 2200; ++i)
    {
        s_test_tick_ms++;
        LIB_INS_Update(&sample, 0.001f);
    }

    state = LIB_INS_GetState();
    TEST_ASSERT(state->app_state == LIB_INS_APP_STATE_RUN,
                "INS must retain the existing convergence-to-run transition");
    TEST_ASSERT(fabsf(state->pitch - seeded_pitch) < 1.0e-3f,
                "static raw pitch must not slowly crawl after gravity seeding");
    TEST_ASSERT(fabsf(state->roll_out) < 1.0e-3f &&
                fabsf(state->pitch_out) < 1.0e-3f &&
                fabsf(state->yaw_out) < 1.0e-3f,
                "RUN outputs must retain relative recenter semantics");
    TEST_ASSERT(fabsf(state->q_relative[0] - 1.0f) < 1.0e-4f &&
                fabsf(state->q_relative[1]) < 1.0e-4f &&
                fabsf(state->q_relative[2]) < 1.0e-4f &&
                fabsf(state->q_relative[3]) < 1.0e-4f,
                "RUN transition must publish an identity relative quaternion");
    TEST_ASSERT(fabsf(INS.Pitch - state->pitch) < 1.0e-5f &&
                fabsf(INS.PitchRelative - state->pitch_out) < 1.0e-5f,
                "legacy INS mirror must separate absolute and relative Euler semantics");

    const imu_sample_t moved_sample = make_static_sample(
        42.0f / 180.0f * (float)M_PI,
        -25.0f / 180.0f * (float)M_PI);
    for (int i = 0; i < 5000; ++i)
    {
        s_test_tick_ms++;
        LIB_INS_Update(&moved_sample, 0.001f);
    }
    state = LIB_INS_GetState();
    TEST_ASSERT(fabsf(state->roll - 42.0f / 180.0f * (float)M_PI) < 0.02f &&
                fabsf(state->pitch - (-25.0f / 180.0f * (float)M_PI)) < 0.02f,
                "absolute Euler attitude must follow the new gravity direction");
    TEST_ASSERT(fabsf(state->roll_out) > 0.05f || fabsf(state->pitch_out) > 0.05f,
                "relative Euler attitude must retain motion from the RUN reference");
    TEST_ASSERT(fabsf(state->q_relative[0] - 1.0f) > 0.01f,
                "relative quaternion must retain motion from the RUN reference");

    const float absolute_pitch_before_recenter = state->pitch;
    LIB_INS_Recenter();
    state = LIB_INS_GetState();
    TEST_ASSERT(fabsf(state->pitch - absolute_pitch_before_recenter) < 1.0e-6f,
                "recenter must not change absolute gravity attitude");
    TEST_ASSERT(fabsf(state->roll_out) < 1.0e-6f &&
                fabsf(state->pitch_out) < 1.0e-6f &&
                fabsf(state->yaw_out) < 1.0e-6f,
                "recenter must clear relative Euler attitude");
    TEST_ASSERT(fabsf(state->q_relative[0] - 1.0f) < 1.0e-6f &&
                fabsf(state->q_relative[1]) < 1.0e-6f &&
                fabsf(state->q_relative[2]) < 1.0e-6f &&
                fabsf(state->q_relative[3]) < 1.0e-6f,
                "recenter must clear relative quaternion attitude");

    LIB_INS_Reset();
    TEST_ASSERT(g_omx_dbg_ins.attitude_seeded == 0u &&
                g_omx_dbg_ins.seed_sample_count == 0u,
                "INS reset must restart gravity seed collection");

    imu_sample_t cancelling_sample = make_static_sample(0.0f, 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        cancelling_sample.accel[2] = ((i & 1) == 0) ? TEST_G : -TEST_G;
        s_test_tick_ms++;
        LIB_INS_Update(&cancelling_sample, 0.001f);
    }
    TEST_ASSERT(g_omx_dbg_ins.attitude_seeded == 0u &&
                g_omx_dbg_ins.seed_sample_count == 0u,
                "failed zero-mean gravity seed must clear its collection window");

    for (int i = 0; i < 20; ++i)
    {
        s_test_tick_ms++;
        LIB_INS_Update(&sample, 0.001f);
    }
    TEST_ASSERT(g_omx_dbg_ins.attitude_seeded == 1u,
                "INS must seed normally after a failed window is recollected");
}

int main(void)
{
    test_seed_large_poses();
    test_yaw_remains_unobservable();
    test_three_axis_gravity_correction();
    test_ins_seed_window_and_relative_output();
    (void)printf("IMU attitude host tests passed.\n");
    return EXIT_SUCCESS;
}
