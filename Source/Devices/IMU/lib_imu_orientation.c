#include "SirinEmbedOS/Devices/IMU/lib_imu_orientation.h"
#include "SirinEmbedOS/Devices/IMU/lib_imu_project_config.h"

#define IMU_ROT3(r00, r01, r02, \
                 r10, r11, r12, \
                 r20, r21, r22) \
    { .m = { { (int8_t)(r00), (int8_t)(r01), (int8_t)(r02) }, \
             { (int8_t)(r10), (int8_t)(r11), (int8_t)(r12) }, \
             { (int8_t)(r20), (int8_t)(r21), (int8_t)(r22) } } }

/*
 * Every preset defines v_B = R_SB * v_S.
 *
 * 0: identity
 * 1: rotate 180 degrees around X
 * 2: rotate +90 degrees around Z
 * 3: rotate -90 degrees around Z
 * 4: rotate +90 degrees around Y
 * 5: rotate -90 degrees around Y
 * 6: legacy OmniCtrl 2 mounting (XT30 upwards and forwards)
 */
#if (IMU_ORIENTATION_SEL == 0)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
    +1,  0,  0,
     0, +1,  0,
     0,  0, +1);

#elif (IMU_ORIENTATION_SEL == 1)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
    +1,  0,  0,
     0, -1,  0,
     0,  0, -1);

#elif (IMU_ORIENTATION_SEL == 2)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
     0, +1,  0,
    -1,  0,  0,
     0,  0, +1);

#elif (IMU_ORIENTATION_SEL == 3)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
     0, -1,  0,
    +1,  0,  0,
     0,  0, +1);

#elif (IMU_ORIENTATION_SEL == 4)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
     0,  0, +1,
     0, +1,  0,
    -1,  0,  0);

#elif (IMU_ORIENTATION_SEL == 5)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
     0,  0, -1,
     0, +1,  0,
    +1,  0,  0);

#elif (IMU_ORIENTATION_SEL == 6)

const imu_rot3_t g_imu_R_SB = IMU_ROT3(
     0, -1,  0,
    -1,  0,  0,
     0,  0, -1);

#else
#error "Unsupported IMU_ORIENTATION_SEL"
#endif

#undef IMU_ROT3
