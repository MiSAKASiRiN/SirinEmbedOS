//
// Created by sirin on 2025/11/29.
//
#include "SirinEmbedOS/Devices/IMU/mahony_filter.h"
#include <string.h>

/*************************************
完成时间：2023年09月02日
功能介绍：实现mahony姿态角解算算法的模块封装
知乎账号：龙胆也
B站账号：华南小虎队
***************************************/

/* Hamilton robot/body -> world rotation. Row 2 is world-up in body axes. */
void SRNOS_MahonyRotationMatrixUpdate(struct SRNOS_MahonyState *SRNOS_MahonyFilter)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    float q1q1 = SRNOS_MahonyFilter->q1 * SRNOS_MahonyFilter->q1;
    float q2q2 = SRNOS_MahonyFilter->q2 * SRNOS_MahonyFilter->q2;
    float q3q3 = SRNOS_MahonyFilter->q3 * SRNOS_MahonyFilter->q3;

    float q0q1 = SRNOS_MahonyFilter->q0 * SRNOS_MahonyFilter->q1;
    float q0q2 = SRNOS_MahonyFilter->q0 * SRNOS_MahonyFilter->q2;
    float q0q3 = SRNOS_MahonyFilter->q0 * SRNOS_MahonyFilter->q3;
    float q1q2 = SRNOS_MahonyFilter->q1 * SRNOS_MahonyFilter->q2;
    float q1q3 = SRNOS_MahonyFilter->q1 * SRNOS_MahonyFilter->q3;
    float q2q3 = SRNOS_MahonyFilter->q2 * SRNOS_MahonyFilter->q3;

    SRNOS_MahonyFilter->rMat[0][0] = 1.0f - 2.0f * q2q2 - 2.0f * q3q3;
    SRNOS_MahonyFilter->rMat[0][1] = 2.0f * (q1q2  -q0q3);
    SRNOS_MahonyFilter->rMat[0][2] = 2.0f * (q1q3 +q0q2);

    SRNOS_MahonyFilter->rMat[1][0] = 2.0f * (q1q2 +q0q3);
    SRNOS_MahonyFilter->rMat[1][1] = 1.0f - 2.0f * q1q1 - 2.0f * q3q3;
    SRNOS_MahonyFilter->rMat[1][2] = 2.0f * (q2q3  -q0q1);

    SRNOS_MahonyFilter->rMat[2][0] = 2.0f * (q1q3  -q0q2);
    SRNOS_MahonyFilter->rMat[2][1] = 2.0f * (q2q3  +q0q1);
    SRNOS_MahonyFilter->rMat[2][2] = 1.0f - 2.0f * q1q1 - 2.0f * q2q2;
}

// 将陀螺仪和加速度计的输入值赋予滤波器结构体
void SRNOS_MahonyInput(struct SRNOS_MahonyState *SRNOS_MahonyFilter,SRNOS_Axis3f gyro,SRNOS_Axis3f acc)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    SRNOS_MahonyFilter->gyro = gyro;
    SRNOS_MahonyFilter->acc = acc;
}

// 使用Mahony算法更新滤波器状态
void SRNOS_MahonyUpdate(struct SRNOS_MahonyState *SRNOS_MahonyFilter)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    if (!isfinite(SRNOS_MahonyFilter->dt) || (SRNOS_MahonyFilter->dt <= 0.0f))
    {
        return;
    }

    float normalise;
    float ex = 0.0f;
    float ey = 0.0f;
    float ez = 0.0f;

    /*角速度，度转弧度*/
//    SRNOS_MahonyFilter->gyro.x *= DEG2RAD;        /* 度转弧度 */
//    SRNOS_MahonyFilter->gyro.y *= DEG2RAD;
//    SRNOS_MahonyFilter->gyro.z *= DEG2RAD;;

    /*单位化加速计测量值*/
    normalise = sqrtf(SRNOS_MahonyFilter->acc.x * SRNOS_MahonyFilter->acc.x
                    + SRNOS_MahonyFilter->acc.y * SRNOS_MahonyFilter->acc.y
                    + SRNOS_MahonyFilter->acc.z * SRNOS_MahonyFilter->acc.z);

    uint8_t acc_valid = (uint8_t)(isfinite(normalise) && (normalise > 1.0e-6f));
    if (acc_valid != 0u)
    {
        float inv_acc_norm = 1.0f / normalise;
        SRNOS_MahonyFilter->acc.x *= inv_acc_norm;
        SRNOS_MahonyFilter->acc.y *= inv_acc_norm;
        SRNOS_MahonyFilter->acc.z *= inv_acc_norm;
    }

    /*加速计读取的方向与重力加速计方向的差值，用向量叉乘计算*/
	//vx=SRNOS_MahonyFilter->rMat[2][0]=2.0f * (q1q3 + -q0q2)
	//vy=SRNOS_MahonyFilter->rMat[2][1]=2.0f * (q2q3 - -q0q1)
	//vz=SRNOS_MahonyFilter->rMat[2][2]=1.0f - 2.0f * q1q1 - 2.0f * q2q2
    if (acc_valid != 0u)
    {
        ex = (SRNOS_MahonyFilter->acc.y * SRNOS_MahonyFilter->rMat[2][2] - SRNOS_MahonyFilter->acc.z * SRNOS_MahonyFilter->rMat[2][1]);
        ey = (SRNOS_MahonyFilter->acc.z * SRNOS_MahonyFilter->rMat[2][0] - SRNOS_MahonyFilter->acc.x * SRNOS_MahonyFilter->rMat[2][2]);
        ez = (SRNOS_MahonyFilter->acc.x * SRNOS_MahonyFilter->rMat[2][1] - SRNOS_MahonyFilter->acc.y * SRNOS_MahonyFilter->rMat[2][0]);

        /*
         * 重力只提供两个可观测自由度，但这个可观测平面在任意大姿态下
         * 并不固定等于机体 X/Y 平面。因此必须保留完整的 ex/ey/ez；
         * 误差向量本身与重力方向正交，不会因此获得绝对 yaw。
         */
        if (SRNOS_MahonyFilter->Ki > 0.0f)
        {
            SRNOS_MahonyFilter->exInt += SRNOS_MahonyFilter->Ki * ex * SRNOS_MahonyFilter->dt;
            SRNOS_MahonyFilter->eyInt += SRNOS_MahonyFilter->Ki * ey * SRNOS_MahonyFilter->dt;
            SRNOS_MahonyFilter->ezInt += SRNOS_MahonyFilter->Ki * ez * SRNOS_MahonyFilter->dt;
        }
        else
        {
            SRNOS_MahonyFilter->exInt = 0.0f;
            SRNOS_MahonyFilter->eyInt = 0.0f;
            SRNOS_MahonyFilter->ezInt = 0.0f;
        }

        /* 用叉积误差做完整三维 PI 修正 */
        SRNOS_MahonyFilter->gyro.x += SRNOS_MahonyFilter->Kp * ex + SRNOS_MahonyFilter->exInt;
        SRNOS_MahonyFilter->gyro.y += SRNOS_MahonyFilter->Kp * ey + SRNOS_MahonyFilter->eyInt;
        SRNOS_MahonyFilter->gyro.z += SRNOS_MahonyFilter->Kp * ez + SRNOS_MahonyFilter->ezInt;
    }
    else
    {
        SRNOS_MahonyFilter->exInt = 0.0f;
        SRNOS_MahonyFilter->eyInt = 0.0f;
        SRNOS_MahonyFilter->ezInt = 0.0f;
    }

    /* 一阶近似算法，四元数运动学方程的离散化形式和积分 */
    float q0Last = SRNOS_MahonyFilter->q0;
    float q1Last = SRNOS_MahonyFilter->q1;
    float q2Last = SRNOS_MahonyFilter->q2;
    float q3Last = SRNOS_MahonyFilter->q3;
    float halfT = SRNOS_MahonyFilter->dt * 0.5f;
    SRNOS_MahonyFilter->q0 += (-q1Last * SRNOS_MahonyFilter->gyro.x - q2Last * SRNOS_MahonyFilter->gyro.y - q3Last * SRNOS_MahonyFilter->gyro.z) * halfT;
    SRNOS_MahonyFilter->q1 += ( q0Last * SRNOS_MahonyFilter->gyro.x + q2Last * SRNOS_MahonyFilter->gyro.z - q3Last * SRNOS_MahonyFilter->gyro.y) * halfT;
    SRNOS_MahonyFilter->q2 += ( q0Last * SRNOS_MahonyFilter->gyro.y - q1Last * SRNOS_MahonyFilter->gyro.z + q3Last * SRNOS_MahonyFilter->gyro.x) * halfT;
    SRNOS_MahonyFilter->q3 += ( q0Last * SRNOS_MahonyFilter->gyro.z + q1Last * SRNOS_MahonyFilter->gyro.y - q2Last * SRNOS_MahonyFilter->gyro.x) * halfT;

    /*单位化四元数*/
    normalise = sqrtf(SRNOS_MahonyFilter->q0 * SRNOS_MahonyFilter->q0
                    + SRNOS_MahonyFilter->q1 * SRNOS_MahonyFilter->q1
                    + SRNOS_MahonyFilter->q2 * SRNOS_MahonyFilter->q2
                    + SRNOS_MahonyFilter->q3 * SRNOS_MahonyFilter->q3);

    if (!isfinite(normalise) || (normalise <= 1.0e-6f))
    {
        SRNOS_MahonyInit(SRNOS_MahonyFilter, SRNOS_MahonyFilter->Kp, SRNOS_MahonyFilter->Ki, SRNOS_MahonyFilter->dt);
        return;
    }

    float inv_q_norm = 1.0f / normalise;
    SRNOS_MahonyFilter->q0 *= inv_q_norm;
    SRNOS_MahonyFilter->q1 *= inv_q_norm;
    SRNOS_MahonyFilter->q2 *= inv_q_norm;
    SRNOS_MahonyFilter->q3 *= inv_q_norm;

    /*计算旋转矩阵*/
    SRNOS_MahonyFilter->SRNOS_MahonyRotationMatrixUpdate(SRNOS_MahonyFilter);
}

// 从旋转矩阵中提取姿态角
void SRNOS_MahonyOutput(struct SRNOS_MahonyState *SRNOS_MahonyFilter)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    /*计算roll pitch yaw 欧拉角*/
    float sin_pitch = -SRNOS_MahonyFilter->rMat[2][0];
    if (sin_pitch > 1.0f)
    {
        sin_pitch = 1.0f;
    }
    else if (sin_pitch < -1.0f)
    {
        sin_pitch = -1.0f;
    }
    SRNOS_MahonyFilter->pitch = asinf(sin_pitch);
    SRNOS_MahonyFilter->roll = atan2f(SRNOS_MahonyFilter->rMat[2][1], SRNOS_MahonyFilter->rMat[2][2]) ;
    SRNOS_MahonyFilter->yaw = atan2f(SRNOS_MahonyFilter->rMat[1][0], SRNOS_MahonyFilter->rMat[0][0]) ;
}

// 初始化Mahony滤波器的参数和函数指针
void SRNOS_MahonyInit(struct SRNOS_MahonyState *SRNOS_MahonyFilter,float Kp,float Ki,float dt)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    memset(SRNOS_MahonyFilter, 0, sizeof(*SRNOS_MahonyFilter));
    SRNOS_MahonyFilter->Kp = Kp;
    SRNOS_MahonyFilter->Ki = Ki;
    SRNOS_MahonyFilter->dt = dt;
    SRNOS_MahonyFilter->q0 = 1.0f;
    SRNOS_MahonyFilter->q1 = 0.0f;
    SRNOS_MahonyFilter->q2 = 0.0f;
    SRNOS_MahonyFilter->q3 = 0.0f;
    SRNOS_MahonyFilter->SRNOS_MahonyInput = SRNOS_MahonyInput;
    SRNOS_MahonyFilter->SRNOS_MahonyUpdate = SRNOS_MahonyUpdate;
    SRNOS_MahonyFilter->SRNOS_MahonyOutput = SRNOS_MahonyOutput;
    SRNOS_MahonyFilter->SRNOS_MahonyRotationMatrixUpdate = SRNOS_MahonyRotationMatrixUpdate;
    SRNOS_MahonyFilter->SRNOS_MahonyRotationMatrixUpdate(SRNOS_MahonyFilter);
}

uint8_t SRNOS_MahonySeedFromAccel(struct SRNOS_MahonyState *SRNOS_MahonyFilter, SRNOS_Axis3f acc)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return 0u;
    }

    const float norm_sq = acc.x * acc.x + acc.y * acc.y + acc.z * acc.z;
    if (!isfinite(norm_sq) || (norm_sq <= 1.0e-12f))
    {
        return 0u;
    }

    const float inv_norm = 1.0f / sqrtf(norm_sq);
    const float ax = acc.x * inv_norm;
    const float ay = acc.y * inv_norm;
    const float az = acc.z * inv_norm;

    /*
     * 与 SRNOS_MahonyRotationMatrixUpdate() 的 ZYX 约定一致：将预测重力
     * rMat[2][] 直接对齐到归一化加速度，并选择 yaw=0 作为航向规范。
     */
    const float roll = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    const float half_roll = 0.5f * roll;
    const float half_pitch = 0.5f * pitch;
    const float cr = cosf(half_roll);
    const float sr = sinf(half_roll);
    const float cp = cosf(half_pitch);
    const float sp = sinf(half_pitch);

    float q0 = cr * cp;
    float q1 = sr * cp;
    float q2 = cr * sp;
    float q3 = -sr * sp;
    const float q_norm = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    if (!isfinite(q_norm) || (q_norm <= 1.0e-6f))
    {
        return 0u;
    }

    const float inv_q_norm = 1.0f / q_norm;
    SRNOS_MahonyFilter->q0 = q0 * inv_q_norm;
    SRNOS_MahonyFilter->q1 = q1 * inv_q_norm;
    SRNOS_MahonyFilter->q2 = q2 * inv_q_norm;
    SRNOS_MahonyFilter->q3 = q3 * inv_q_norm;
    SRNOS_MahonyFilter->exInt = 0.0f;
    SRNOS_MahonyFilter->eyInt = 0.0f;
    SRNOS_MahonyFilter->ezInt = 0.0f;
    SRNOS_MahonyFilter->acc = acc;
    SRNOS_MahonyFilter->SRNOS_MahonyRotationMatrixUpdate(SRNOS_MahonyFilter);
    SRNOS_MahonyOutput(SRNOS_MahonyFilter);
    return 1u;
}

void SRNOS_MahonySetGain(struct SRNOS_MahonyState *SRNOS_MahonyFilter, float Kp, float Ki)
{
    if (SRNOS_MahonyFilter == NULL)
    {
        return;
    }

    SRNOS_MahonyFilter->Kp = Kp;
    SRNOS_MahonyFilter->Ki = Ki;
    if (Ki <= 0.0f)
    {
        SRNOS_MahonyFilter->exInt = 0.0f;
        SRNOS_MahonyFilter->eyInt = 0.0f;
        SRNOS_MahonyFilter->ezInt = 0.0f;
    }
}
