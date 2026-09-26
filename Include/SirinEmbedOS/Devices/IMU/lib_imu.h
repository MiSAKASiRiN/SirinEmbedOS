//
// lib_imu.h
//
#pragma once

#include <stdint.h>
#include "tim.h"   // 使用 HAL 提供的 TIM_HandleTypeDef

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 基础类型 ===================== */

/* IMU 采样返回状态 */
typedef enum {
    IMU_STATUS_OK = 0,
    IMU_STATUS_BUSY,
    IMU_STATUS_ERR_BMI,
    IMU_STATUS_STALE,
    IMU_STATUS_INVALID_ARG,
} imu_status_t;

/* 单帧 IMU 数据 */
typedef struct {
    float gyro[3];        /* rad/s, body frame */
    float accel[3];       /* m/s^2, body frame */
    float temperature;    /* 摄氏度 */
} imu_sample_t;

/* ===================== 温度控制相关 ===================== */

/* 温度状态机状态 */
typedef enum {
    IMU_TEMP_STATE_COLD = 0,
    IMU_TEMP_STATE_WARMUP,
    IMU_TEMP_STATE_STABLE,
} imu_temp_state_t;

/* 直接使用 HAL 的 TIM_HandleTypeDef */
typedef struct {
    TIM_HandleTypeDef *htim;  /* PWM 用的 TIM 句柄 */
    uint32_t           channel;
    float              max_ccr; /* 对应 ARR（autoreload）的最大计数值 */
} imu_temp_ctrl_hw_t;

typedef struct {
    uint32_t status;
    float error;
    float p;
    float i;
    float d;
    float raw_output;
    float output;
    float dt_s;
    uint8_t saturated;
    uint8_t slew_limited;
    uint8_t fault_latched;
} imu_temp_pid_debug_t;

/* ===================== 校准相关类型 ===================== */

/**
 * @brief IMU 静态校准状态
 */
typedef enum
{
    IMU_CALIB_IDLE = 0,
    IMU_CALIB_RUNNING,
    IMU_CALIB_DONE,
    IMU_CALIB_FAILED,
} imu_calib_state_t;

/**
 * @brief IMU 静态校准结果（与姿态无关，只要求静止）
 *
 * gyro_bias : 静止条件下的陀螺零偏（rad/s）
 * accel_scale : 使用 |accel|≈9.81 得到的比例因子
 * g_norm    : 校准期间测得的平均 |accel|
 * temp_c    : 校准时温度
 * success   : 1=通过质量检查，0=不通过
 */
typedef struct
{
    float gyro_bias[3];
    float accel_scale;
    float g_norm;
    float temp_c;
    uint8_t success;
} imu_calib_result_t;

/* ===================== IMU 基础接口 ===================== */

/**
 * @brief 初始化 IMU 抽象层
 *
 * 注意：底层 BMI088 的硬件初始化（如 BSP_BMI088_Init）
 *       建议在 BSP 或 main 中完成，这里只做状态复位。
 */
imu_status_t IMU_Init(void);

/**
 * @brief Initialize the internal BMI088 hardware path.
 *
 * The bus id is passed as a small integer on purpose so upper task prototypes
 * do not need to include lower-level BSP headers. For the current project,
 * bus id 0 maps to BSP_SPI_BUS_4.
 */
imu_status_t IMU_InternalInit(uint8_t spi_bus_id,
                              GPIO_TypeDef *acc_cs_port,
                              uint16_t acc_cs_pin,
                              GPIO_TypeDef *gyro_cs_port,
                              uint16_t gyro_cs_pin,
                              uint8_t run_boot_calibration);

/**
 * @brief 阻塞版采样（直接调用 blocking BMI088 读）
 */
imu_status_t IMU_SampleOnceBlocking(imu_sample_t *out);

/**
 * @brief 仅发起一次采样（一般不直接在 Task 用）
 */
imu_status_t IMU_BeginSample(void);

/**
 * @brief 若底层采样就绪则取出一帧（一般不直接在 Task 用）
 */
imu_status_t IMU_GetSampleIfReady(imu_sample_t *out);

/**
 * @brief 推荐 Task 使用的非阻塞单次采样接口
 *
 * 使用方式：
 *  - 周期调用（例如 1kHz）
 *  - 返回 BUSY：本周期无新数据，直接略过
 *  - 返回 OK：out 中为新数据
 *  - 返回 ERR_BMI：SPI/传感器错误，需故障处理
 */
imu_status_t IMU_SampleOnce(imu_sample_t *out);

/**
 * @brief 仅重置 IMU 运行态的软件采样状态
 *
 * 不会触发共享 SPI 总线 abort/reset，只清理上层采样锁存和
 * BMI088 异步采样状态机，供协作式恢复使用。
 */
imu_status_t IMU_ResetRuntimeState(void);

/* ===================== 温度控制接口 ===================== */

/**
 * @brief 绑定加热 PWM 硬件并初始化 PID/状态机
 *
 * @param hw              PWM 硬件配置（TIM3_CH4）
 * @param target_temp_c   目标温度 (℃)
 * @param pid_param[3]    PID 参数 {Kp, Ki, Kd}
 * @param max_out         PID 输出最大值（对应满功率）
 * @param max_iout        PID 积分项最大值
 * @param stable_delta_c  认为“到达目标温度”的允许误差 (℃)
 * @param stable_time_ms  温度持续在误差带内的时间 (ms)
 */
void IMU_TempCtrl_Init(const imu_temp_ctrl_hw_t *hw,
                       float target_temp_c,
                       const float pid_param[3],
                       float max_out,
                       float max_iout,
                       float stable_delta_c,
                       uint32_t stable_time_ms);

/**
 * @brief 使用阻塞采样的温控 step（如果不在主 IMU Task 使用）
 */
void IMU_TempCtrl_Step(void);

/**
 * @brief 在已有温度数据的情况下执行温控 step
 *
 * @param current_temp_c 当前 IMU 温度 (℃)
 * @param dt_s           本次新样本与上次新样本的时间差 (s)
 */
void IMU_TempCtrl_StepWithTemp(float current_temp_c, float dt_s);

/**
 * @brief 获取温度状态机状态
 */
imu_temp_state_t IMU_TempCtrl_GetState(void);

/**
 * @brief 温度是否已稳定
 * @return 1=稳定；0=未稳定
 */
uint8_t IMU_TempCtrl_IsStable(void);

/**
 * @brief 修改目标温度
 */
void IMU_TempCtrl_SetTarget(float target_temp_c);

/**
 * @brief 开启/关闭加热
 */
void IMU_TempCtrl_SetEnable(uint8_t enable);

/**
 * @brief 触发故障，关闭加热并锁存 fault 状态
 */
void IMU_TempCtrl_Fault(void);

/**
 * @brief 清除故障锁存（不会自动重新启用加热）
 */
void IMU_TempCtrl_ClearFault(void);

/**
 * @brief 最近一次记录的温度
 */
float IMU_TempCtrl_GetCurrentTemp(void);

/**
 * @brief 当前目标温度
 */
float IMU_TempCtrl_GetTargetTemp(void);

/** @brief 获取最近一次温控 PID 的低成本调试快照。 */
void IMU_TempCtrl_GetPidDebug(imu_temp_pid_debug_t *out);

/* ===================== 静态校准接口 ===================== */
void IMU_Calib_Start(void);
void IMU_Calib_AddSample(const imu_sample_t *samp);

/**
 * @brief 获取当前校准状态
 */
imu_calib_state_t IMU_Calib_GetState(void);

/**
 * @brief 获取校准结果
 *
 * @param out 若当前没有有效结果，会填入默认值且 success=0
 */
void IMU_Calib_GetResult(imu_calib_result_t *out);

/**
 * @brief Active calibration currently applied to runtime samples.
 */
void IMU_ActiveCalib_SetDefault(float temp_c);
void IMU_ActiveCalib_Set(const imu_calib_result_t *calib);
void IMU_ActiveCalib_Get(imu_calib_result_t *out);
void IMU_ApplyActiveCalibration(imu_sample_t *samp);

/**
 * @brief Lightweight EEPROM persistence for calibration data.
 */
typedef enum
{
    IMU_CALIB_STORAGE_OK = 0,
    IMU_CALIB_STORAGE_ERROR,
    IMU_CALIB_STORAGE_INVALID_PARAM,
    IMU_CALIB_STORAGE_BUSY,
    IMU_CALIB_STORAGE_BAD_DATA,
} imu_calib_storage_status_t;

imu_calib_storage_status_t IMU_CalibStorage_Init(uint8_t i2c_bus_id,
                                                 uint16_t i2c_addr_7bit);
imu_calib_storage_status_t IMU_CalibStorage_Load(uint8_t mem_addr,
                                                 imu_calib_result_t *out);
imu_calib_storage_status_t IMU_CalibStorage_Save(uint8_t mem_addr,
                                                 const imu_calib_result_t *calib);

#ifdef __cplusplus
}
#endif
