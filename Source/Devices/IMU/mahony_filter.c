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

/*计算旋转矩阵*///大地坐标系 R 转换到机体坐标系 b 的坐标转换矩阵
void RotationMatrix_update(struct MAHONY_FILTER_t *mahony_filter)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    float q1q1 = mahony_filter->q1 * mahony_filter->q1;
    float q2q2 = mahony_filter->q2 * mahony_filter->q2;
    float q3q3 = mahony_filter->q3 * mahony_filter->q3;

    float q0q1 = mahony_filter->q0 * mahony_filter->q1;
    float q0q2 = mahony_filter->q0 * mahony_filter->q2;
    float q0q3 = mahony_filter->q0 * mahony_filter->q3;
    float q1q2 = mahony_filter->q1 * mahony_filter->q2;
    float q1q3 = mahony_filter->q1 * mahony_filter->q3;
    float q2q3 = mahony_filter->q2 * mahony_filter->q3;

    mahony_filter->rMat[0][0] = 1.0f - 2.0f * q2q2 - 2.0f * q3q3;
    mahony_filter->rMat[0][1] = 2.0f * (q1q2  -q0q3);
    mahony_filter->rMat[0][2] = 2.0f * (q1q3 +q0q2);

    mahony_filter->rMat[1][0] = 2.0f * (q1q2 +q0q3);
    mahony_filter->rMat[1][1] = 1.0f - 2.0f * q1q1 - 2.0f * q3q3;
    mahony_filter->rMat[1][2] = 2.0f * (q2q3  -q0q1);

    mahony_filter->rMat[2][0] = 2.0f * (q1q3  -q0q2);
    mahony_filter->rMat[2][1] = 2.0f * (q2q3  +q0q1);
    mahony_filter->rMat[2][2] = 1.0f - 2.0f * q1q1 - 2.0f * q2q2;
}

// 将陀螺仪和加速度计的输入值赋予滤波器结构体
void mahony_input(struct MAHONY_FILTER_t *mahony_filter,Axis3f gyro,Axis3f acc)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    mahony_filter->gyro = gyro;
    mahony_filter->acc = acc;
}

// 使用Mahony算法更新滤波器状态
void mahony_update(struct MAHONY_FILTER_t *mahony_filter)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    if (!isfinite(mahony_filter->dt) || (mahony_filter->dt <= 0.0f))
    {
        return;
    }

    float normalise;
    float ex = 0.0f;
    float ey = 0.0f;
    float ez = 0.0f;

    /*角速度，度转弧度*/
//    mahony_filter->gyro.x *= DEG2RAD;        /* 度转弧度 */
//    mahony_filter->gyro.y *= DEG2RAD;
//    mahony_filter->gyro.z *= DEG2RAD;;

    /*单位化加速计测量值*/
    normalise = sqrtf(mahony_filter->acc.x * mahony_filter->acc.x
                    + mahony_filter->acc.y * mahony_filter->acc.y
                    + mahony_filter->acc.z * mahony_filter->acc.z);

    uint8_t acc_valid = (uint8_t)(isfinite(normalise) && (normalise > 1.0e-6f));
    if (acc_valid != 0u)
    {
        float inv_acc_norm = 1.0f / normalise;
        mahony_filter->acc.x *= inv_acc_norm;
        mahony_filter->acc.y *= inv_acc_norm;
        mahony_filter->acc.z *= inv_acc_norm;
    }

    /*加速计读取的方向与重力加速计方向的差值，用向量叉乘计算*/
	//vx=mahony_filter->rMat[2][0]=2.0f * (q1q3 + -q0q2)
	//vy=mahony_filter->rMat[2][1]=2.0f * (q2q3 - -q0q1)
	//vz=mahony_filter->rMat[2][2]=1.0f - 2.0f * q1q1 - 2.0f * q2q2
    if (acc_valid != 0u)
    {
        ex = (mahony_filter->acc.y * mahony_filter->rMat[2][2] - mahony_filter->acc.z * mahony_filter->rMat[2][1]);
        ey = (mahony_filter->acc.z * mahony_filter->rMat[2][0] - mahony_filter->acc.x * mahony_filter->rMat[2][2]);
        ez = (mahony_filter->acc.x * mahony_filter->rMat[2][1] - mahony_filter->acc.y * mahony_filter->rMat[2][0]);

        /*
         * 重力只提供两个可观测自由度，但这个可观测平面在任意大姿态下
         * 并不固定等于机体 X/Y 平面。因此必须保留完整的 ex/ey/ez；
         * 误差向量本身与重力方向正交，不会因此获得绝对 yaw。
         */
        if (mahony_filter->Ki > 0.0f)
        {
            mahony_filter->exInt += mahony_filter->Ki * ex * mahony_filter->dt;
            mahony_filter->eyInt += mahony_filter->Ki * ey * mahony_filter->dt;
            mahony_filter->ezInt += mahony_filter->Ki * ez * mahony_filter->dt;
        }
        else
        {
            mahony_filter->exInt = 0.0f;
            mahony_filter->eyInt = 0.0f;
            mahony_filter->ezInt = 0.0f;
        }

        /* 用叉积误差做完整三维 PI 修正 */
        mahony_filter->gyro.x += mahony_filter->Kp * ex + mahony_filter->exInt;
        mahony_filter->gyro.y += mahony_filter->Kp * ey + mahony_filter->eyInt;
        mahony_filter->gyro.z += mahony_filter->Kp * ez + mahony_filter->ezInt;
    }
    else
    {
        mahony_filter->exInt = 0.0f;
        mahony_filter->eyInt = 0.0f;
        mahony_filter->ezInt = 0.0f;
    }

    /* 一阶近似算法，四元数运动学方程的离散化形式和积分 */
    float q0Last = mahony_filter->q0;
    float q1Last = mahony_filter->q1;
    float q2Last = mahony_filter->q2;
    float q3Last = mahony_filter->q3;
    float halfT = mahony_filter->dt * 0.5f;
    mahony_filter->q0 += (-q1Last * mahony_filter->gyro.x - q2Last * mahony_filter->gyro.y - q3Last * mahony_filter->gyro.z) * halfT;
    mahony_filter->q1 += ( q0Last * mahony_filter->gyro.x + q2Last * mahony_filter->gyro.z - q3Last * mahony_filter->gyro.y) * halfT;
    mahony_filter->q2 += ( q0Last * mahony_filter->gyro.y - q1Last * mahony_filter->gyro.z + q3Last * mahony_filter->gyro.x) * halfT;
    mahony_filter->q3 += ( q0Last * mahony_filter->gyro.z + q1Last * mahony_filter->gyro.y - q2Last * mahony_filter->gyro.x) * halfT;

    /*单位化四元数*/
    normalise = sqrtf(mahony_filter->q0 * mahony_filter->q0
                    + mahony_filter->q1 * mahony_filter->q1
                    + mahony_filter->q2 * mahony_filter->q2
                    + mahony_filter->q3 * mahony_filter->q3);

    if (!isfinite(normalise) || (normalise <= 1.0e-6f))
    {
        mahony_init(mahony_filter, mahony_filter->Kp, mahony_filter->Ki, mahony_filter->dt);
        return;
    }

    float inv_q_norm = 1.0f / normalise;
    mahony_filter->q0 *= inv_q_norm;
    mahony_filter->q1 *= inv_q_norm;
    mahony_filter->q2 *= inv_q_norm;
    mahony_filter->q3 *= inv_q_norm;

    /*计算旋转矩阵*/
    mahony_filter->RotationMatrix_update(mahony_filter);
}

// 从旋转矩阵中提取姿态角
void mahony_output(struct MAHONY_FILTER_t *mahony_filter)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    /*计算roll pitch yaw 欧拉角*/
    float sin_pitch = -mahony_filter->rMat[2][0];
    if (sin_pitch > 1.0f)
    {
        sin_pitch = 1.0f;
    }
    else if (sin_pitch < -1.0f)
    {
        sin_pitch = -1.0f;
    }
    mahony_filter->pitch = asinf(sin_pitch);
    mahony_filter->roll = atan2f(mahony_filter->rMat[2][1], mahony_filter->rMat[2][2]) ;
    mahony_filter->yaw = atan2f(mahony_filter->rMat[1][0], mahony_filter->rMat[0][0]) ;
}

// 初始化Mahony滤波器的参数和函数指针
void mahony_init(struct MAHONY_FILTER_t *mahony_filter,float Kp,float Ki,float dt)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    memset(mahony_filter, 0, sizeof(*mahony_filter));
    mahony_filter->Kp = Kp;
    mahony_filter->Ki = Ki;
    mahony_filter->dt = dt;
    mahony_filter->q0 = 1.0f;
    mahony_filter->q1 = 0.0f;
    mahony_filter->q2 = 0.0f;
    mahony_filter->q3 = 0.0f;
    mahony_filter->mahony_input = mahony_input;
    mahony_filter->mahony_update = mahony_update;
    mahony_filter->mahony_output = mahony_output;
    mahony_filter->RotationMatrix_update = RotationMatrix_update;
    mahony_filter->RotationMatrix_update(mahony_filter);
}

uint8_t mahony_seed_from_accel(struct MAHONY_FILTER_t *mahony_filter, Axis3f acc)
{
    if (mahony_filter == NULL)
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
     * 与 RotationMatrix_update() 的 ZYX 约定一致：将预测重力
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
    mahony_filter->q0 = q0 * inv_q_norm;
    mahony_filter->q1 = q1 * inv_q_norm;
    mahony_filter->q2 = q2 * inv_q_norm;
    mahony_filter->q3 = q3 * inv_q_norm;
    mahony_filter->exInt = 0.0f;
    mahony_filter->eyInt = 0.0f;
    mahony_filter->ezInt = 0.0f;
    mahony_filter->acc = acc;
    mahony_filter->RotationMatrix_update(mahony_filter);
    mahony_output(mahony_filter);
    return 1u;
}

void mahony_set_gain(struct MAHONY_FILTER_t *mahony_filter, float Kp, float Ki)
{
    if (mahony_filter == NULL)
    {
        return;
    }

    mahony_filter->Kp = Kp;
    mahony_filter->Ki = Ki;
    if (Ki <= 0.0f)
    {
        mahony_filter->exInt = 0.0f;
        mahony_filter->eyInt = 0.0f;
        mahony_filter->ezInt = 0.0f;
    }
}
