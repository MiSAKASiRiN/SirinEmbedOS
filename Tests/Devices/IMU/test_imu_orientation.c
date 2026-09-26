#include "SirinEmbedOS/Devices/IMU/lib_imu_orientation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *name, float actual, float expected)
{
    (void)fprintf(stderr,
                  "FAIL orientation %d: %s expected %.1f, got %.1f\n",
                  IMU_ORIENTATION_SEL,
                  name,
                  (double)expected,
                  (double)actual);
    exit(EXIT_FAILURE);
}

static void require_equal(float actual, float expected, const char *name)
{
    if (fabsf(actual - expected) > 1.0e-6f)
    {
        fail(name, actual, expected);
    }
}

int main(void)
{
    static const float expected[7][3] = {
        { 1.0f,  2.0f,  3.0f},
        { 1.0f, -2.0f, -3.0f},
        { 2.0f, -1.0f,  3.0f},
        {-2.0f,  1.0f,  3.0f},
        { 3.0f,  2.0f, -1.0f},
        {-3.0f,  2.0f,  1.0f},
        {-2.0f, -1.0f, -3.0f},
    };
    const float sensor[3] = {1.0f, 2.0f, 3.0f};
    float body[3] = {0.0f, 0.0f, 0.0f};

    imu_apply_orientation(&g_imu_R_SB, sensor, body);
    require_equal(body[0], expected[IMU_ORIENTATION_SEL][0], "body.x");
    require_equal(body[1], expected[IMU_ORIENTATION_SEL][1], "body.y");
    require_equal(body[2], expected[IMU_ORIENTATION_SEL][2], "body.z");
    (void)printf("IMU orientation preset %d host test passed.\n",
                 IMU_ORIENTATION_SEL);
    return EXIT_SUCCESS;
}
