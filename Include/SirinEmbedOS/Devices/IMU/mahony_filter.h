//
// Created by sirin on 2025/11/29.
//

#ifndef SRNOS_IMU_MAHONY_FILTER_H
#define SRNOS_IMU_MAHONY_FILTER_H
#include <stdint.h>
#include <math.h>

/*************************************
完成时间：2023年09月02日
功能介绍：实现mahony姿态角解算算法的模块封装
知乎账号：龙胆也
B站账号：华南小虎队
***************************************/

#define SRNOS_MAHONY_RAD_TO_DEG 57.295671f

typedef struct SRNOS_Axis3f
{
    float x;
    float y;
    float z;
}SRNOS_Axis3f;


// 定义 SRNOS_MahonyState 结构体，用于封装 Mahony 滤波器的数据和函数
struct SRNOS_MahonyState
{
    // 输入参数
    float Kp, Ki;          // 比例和积分增益
    float dt;              // 采样时间间隔
    SRNOS_Axis3f  gyro, acc;     // 陀螺仪和加速度计数据

    // 过程参数
    float exInt, eyInt, ezInt;                // 积分误差累计
    float q0, q1, q2, q3;            // 四元数
    float rMat[3][3];               // 旋转矩阵

    // 输出参数
    float pitch, roll, yaw;         // 姿态角：俯仰角，滚转角，偏航角

    // 函数指针
    void (*SRNOS_MahonyInit)(struct SRNOS_MahonyState *SRNOS_MahonyFilter, float Kp, float Ki, float dt);
    void (*SRNOS_MahonyInput)(struct SRNOS_MahonyState *SRNOS_MahonyFilter, SRNOS_Axis3f gyro, SRNOS_Axis3f acc);
    void (*SRNOS_MahonyUpdate)(struct SRNOS_MahonyState *SRNOS_MahonyFilter);
    void (*SRNOS_MahonyOutput)(struct SRNOS_MahonyState *SRNOS_MahonyFilter);
    void (*SRNOS_MahonyRotationMatrixUpdate)(struct SRNOS_MahonyState *SRNOS_MahonyFilter);
};

// 函数声明
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_MahonyInit(struct SRNOS_MahonyState *SRNOS_MahonyFilter, float Kp, float Ki, float dt);          // 初始化函数
uint8_t SRNOS_MahonySeedFromAccel(struct SRNOS_MahonyState *SRNOS_MahonyFilter, SRNOS_Axis3f acc);               // 用重力方向初始化倾角，yaw=0
void SRNOS_MahonySetGain(struct SRNOS_MahonyState *SRNOS_MahonyFilter, float Kp, float Ki);                // 更新 PI 增益
void SRNOS_MahonyInput(struct SRNOS_MahonyState *SRNOS_MahonyFilter, SRNOS_Axis3f gyro, SRNOS_Axis3f acc);              // 输入数据函数
void SRNOS_MahonyUpdate(struct SRNOS_MahonyState *SRNOS_MahonyFilter);                                      // 更新滤波器函数
void SRNOS_MahonyOutput(struct SRNOS_MahonyState *SRNOS_MahonyFilter);                                      // 输出姿态角函数
void SRNOS_MahonyRotationMatrixUpdate(struct SRNOS_MahonyState *SRNOS_MahonyFilter);                              // 更新旋转矩阵函数

#ifdef __cplusplus
}
#endif

#endif // SRNOS_IMU_MAHONY_FILTER_H
