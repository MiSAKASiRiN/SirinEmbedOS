//
// lib_imu.cpp
//
#include "SirinEmbedOS/Devices/IMU/lib_imu.h"
#include "SirinEmbedOS/Devices/IMU/lib_imu_project_config.h"
#include "SirinEmbedOS/Devices/IMU/imu_temp_controller.hpp"
#include "SirinEmbedOS/Devices/IMU/BMI088/bsp_bmi088.h"   // 提供 bmi088_data_t / BSP_BMI088_* API
#include "SirinEmbedOS/Devices/EEPROM/lib_24c02_bl.h"
#include <math.h>

// === NEW: orientation adapter ===
#include "SirinEmbedOS/Devices/IMU/lib_imu_orientation.h"

static constexpr float IMU_GYRO_DEG_TO_RAD = 0.017453292519943295769f;
static constexpr uint32_t IMU_CALIB_EEPROM_MAGIC = 0x434D5549u;
static constexpr uint16_t IMU_CALIB_EEPROM_VERSION = 0x0002u;

/* ---------- 小工具 ---------- */

static inline float clamp01_local(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x;
}

static void imu_fill_sample_from_bmi088(const bmi088_data_t *raw, imu_sample_t *out)
{
    if ((raw == nullptr) || (out == nullptr))
    {
        return;
    }

    float gyro_S[3]  = {
        raw->gyro[0] * IMU_GYRO_DEG_TO_RAD,
        raw->gyro[1] * IMU_GYRO_DEG_TO_RAD,
        raw->gyro[2] * IMU_GYRO_DEG_TO_RAD,
    };
    float accel_S[3] = { raw->accel[0], raw->accel[1], raw->accel[2] };
    float gyro_B[3];
    float accel_B[3];

    imu_apply_orientation(&g_imu_R_SB, gyro_S,  gyro_B);
    imu_apply_orientation(&g_imu_R_SB, accel_S, accel_B);

    out->gyro[0]  = gyro_B[0];
    out->gyro[1]  = gyro_B[1];
    out->gyro[2]  = gyro_B[2];

    out->accel[0] = accel_B[0];
    out->accel[1] = accel_B[1];
    out->accel[2] = accel_B[2];

    out->temperature = raw->temperature;
}

extern "C" {

/* ============================================================
 *  IMU 基础接口（封装 BMI088）
 * ============================================================ */

static uint8_t s_imu_sampling = 0U;   // 0: 空闲，1: 正在等待这次采样完成
static imu_calib_result_t s_active_calib {};
static lib_24c02_bl_t s_calib_eeprom {};
static imu_calib_storage_status_t s_calib_eeprom_status = IMU_CALIB_STORAGE_ERROR;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float gyro_bias[3];
    float accel_scale;
    float g_norm;
    float temp_c;
} imu_calib_persist_t;

static void imu_make_default_calib(imu_calib_result_t *out, float temp_c)
{
    if (out == nullptr)
    {
        return;
    }

    out->gyro_bias[0] = 0.0f;
    out->gyro_bias[1] = 0.0f;
    out->gyro_bias[2] = 0.0f;
    out->accel_scale = 1.0f;
    out->g_norm = CONF_IMU_CALIB_GRAVITY_MPS2;
    out->temp_c = temp_c;
    out->success = 0U;
}

static imu_calib_storage_status_t imu_calib_storage_from_eeprom(lib_24c02_bl_status_t st)
{
    switch (st)
    {
    case LIB_24C02_BL_OK:
        return IMU_CALIB_STORAGE_OK;
    case LIB_24C02_BL_BUSY:
        return IMU_CALIB_STORAGE_BUSY;
    case LIB_24C02_BL_INVALID_PARAM:
        return IMU_CALIB_STORAGE_INVALID_PARAM;
    case LIB_24C02_BL_ERROR:
    default:
        return IMU_CALIB_STORAGE_ERROR;
    }
}

static void imu_calib_persist_from_result(imu_calib_persist_t *dst,
                                          const imu_calib_result_t *src)
{
    if ((dst == nullptr) || (src == nullptr))
    {
        return;
    }

    dst->magic = IMU_CALIB_EEPROM_MAGIC;
    dst->version = IMU_CALIB_EEPROM_VERSION;
    dst->reserved = 0U;
    for (int i = 0; i < 3; ++i)
    {
        dst->gyro_bias[i] = src->gyro_bias[i];
    }
    dst->accel_scale = src->accel_scale;
    dst->g_norm = src->g_norm;
    dst->temp_c = src->temp_c;
}

static void imu_calib_result_from_persist(imu_calib_result_t *dst,
                                          const imu_calib_persist_t *src)
{
    if ((dst == nullptr) || (src == nullptr))
    {
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        dst->gyro_bias[i] = src->gyro_bias[i];
    }
    dst->accel_scale = src->accel_scale;
    dst->g_norm = src->g_norm;
    dst->temp_c = src->temp_c;
    dst->success = 1U;
}

imu_status_t IMU_Init(void)
{
    s_imu_sampling = 0U;
    IMU_ActiveCalib_SetDefault(25.0f);
    return IMU_STATUS_OK;
}

imu_status_t IMU_InternalInit(uint8_t spi_bus_id,
                              GPIO_TypeDef *acc_cs_port,
                              uint16_t acc_cs_pin,
                              GPIO_TypeDef *gyro_cs_port,
                              uint16_t gyro_cs_pin,
                              uint8_t run_boot_calibration)
{
    bsp_bmi088_status_t st = BSP_BMI088_Init(static_cast<bsp_spi_bus_id_t>(spi_bus_id),
                                             acc_cs_port,
                                             acc_cs_pin,
                                             gyro_cs_port,
                                             gyro_cs_pin,
                                             run_boot_calibration);
    switch (st)
    {
    case BSP_BMI088_OK:
        return IMU_STATUS_OK;
    case BSP_BMI088_BUSY:
        return IMU_STATUS_BUSY;
    case BSP_BMI088_ERR_STALE:
        return IMU_STATUS_STALE;
    default:
        return IMU_STATUS_ERR_BMI;
    }
}

/**
 * @brief 阻塞版一次读取（内部直接调用 Blocking BMI088）
 *
 * 注意：
 *  - raw.* 是传感器坐标系 S
 *  - 这里统一转换到机体系 B：v_B = R_SB * v_S
 */
imu_status_t IMU_SampleOnceBlocking(imu_sample_t *out)
{
    if (out == nullptr)
    {
        return IMU_STATUS_INVALID_ARG;
    }

    bmi088_data_t raw;
    BSP_BMI088_ReadBlocking(&raw);

    imu_fill_sample_from_bmi088(&raw, out);

    return IMU_STATUS_OK;
}

/**
 * @brief 低层手动控制：仅发起采样（一般不给 Task 直接用）
 */
imu_status_t IMU_BeginSample(void)
{
    bsp_bmi088_status_t st = BSP_BMI088_BeginSample();
    switch (st)
    {
        case BSP_BMI088_OK:   return IMU_STATUS_OK;
        case BSP_BMI088_BUSY: return IMU_STATUS_BUSY;
        case BSP_BMI088_ERR_STALE:
            s_imu_sampling = 0U;
            return IMU_STATUS_STALE;
        default:              return IMU_STATUS_ERR_BMI;
    }
}

/**
 * @brief 低层手动控制：尝试取样（一般不给 Task 直接用）
 *
 * 同样在这里完成 S->B 转换
 */
imu_status_t IMU_GetSampleIfReady(imu_sample_t *out)
{
    if (out == nullptr)
    {
        return IMU_STATUS_INVALID_ARG;
    }

    bmi088_data_t raw;
    bsp_bmi088_status_t st = BSP_BMI088_GetSampleIfReady(&raw);

    if (st == BSP_BMI088_BUSY)
    {
        return IMU_STATUS_BUSY;
    }
    if (st == BSP_BMI088_ERR_STALE)
    {
        return IMU_STATUS_STALE;
    }
    if (st != BSP_BMI088_OK)
    {
        return IMU_STATUS_ERR_BMI;
    }

    imu_fill_sample_from_bmi088(&raw, out);

    return IMU_STATUS_OK;
}

/**
 * @brief 推荐给 Task 用的“完全非阻塞单次采样接口”
 *
 * 同样统一输出为机体系 B
 */
imu_status_t IMU_SampleOnce(imu_sample_t *out)
{
    if (out == nullptr)
    {
        return IMU_STATUS_INVALID_ARG;
    }

    /* 阶段 1：如果当前没有在等一帧采样，则尝试发起一次 */
    if (s_imu_sampling == 0U)
    {
        bsp_bmi088_status_t st = BSP_BMI088_BeginSample();

        if (st == BSP_BMI088_OK)
        {
            s_imu_sampling = 1U;
            return IMU_STATUS_BUSY;
        }
        else if (st == BSP_BMI088_BUSY)
        {
            return IMU_STATUS_BUSY;
        }
        else if (st == BSP_BMI088_ERR_STALE)
        {
            s_imu_sampling = 0U;
            return IMU_STATUS_STALE;
        }
        else
        {
            return IMU_STATUS_ERR_BMI;
        }
    }

    /* 阶段 2：已经有一次在飞的采样了，看看是不是 ready 了 */
    bmi088_data_t raw;
    bsp_bmi088_status_t st = BSP_BMI088_GetSampleIfReady(&raw);

    if (st == BSP_BMI088_BUSY)
    {
        return IMU_STATUS_BUSY;
    }

    s_imu_sampling = 0U;

    if (st == BSP_BMI088_ERR_STALE)
    {
        return IMU_STATUS_STALE;
    }

    if (st != BSP_BMI088_OK)
    {
        return IMU_STATUS_ERR_BMI;
    }

    imu_fill_sample_from_bmi088(&raw, out);

    return IMU_STATUS_OK;
}

imu_status_t IMU_ResetRuntimeState(void)
{
    bsp_bmi088_status_t st = BSP_BMI088_ResetAsyncState();
    if (st == BSP_BMI088_BUSY)
    {
        return IMU_STATUS_BUSY;
    }
    if (st != BSP_BMI088_OK)
    {
        return IMU_STATUS_ERR_BMI;
    }

    s_imu_sampling = 0U;
    return IMU_STATUS_OK;
}

/* ============================================================
 *  温度控制内部状态
 * ============================================================ */

static imu_temp_ctrl_hw_t s_temp_hw {};
static uint8_t            s_temp_hw_bound = 0U;
static omx::control::Pid s_imu_temp_pid {};
static omx::imu::ImuTemperatureController s_imu_temp_controller {s_imu_temp_pid};
static uint32_t s_temp_blocking_last_tick = 0U;

/* internal helpers */

static void IMU_TempCtrl_SetDuty_Internal(float duty)
{
    if (!s_temp_hw_bound)
    {
        return;
    }

    duty = clamp01_local(duty);

    uint32_t ccr = static_cast<uint32_t>(duty * s_temp_hw.max_ccr + 0.5f);
    __HAL_TIM_SET_COMPARE(s_temp_hw.htim, s_temp_hw.channel, ccr);
}

static void IMU_TempCtrl_ControlWithTemp(float current_temp_c, float dt_s)
{
    const omx::imu::TempControllerResult result =
        s_imu_temp_controller.step(current_temp_c, dt_s, HAL_GetTick());
    IMU_TempCtrl_SetDuty_Internal(result.duty);
}

/* public temp ctrl APIs */

void IMU_TempCtrl_Init(const imu_temp_ctrl_hw_t *hw,
                       float target_temp_c,
                       const float pid_param[3],
                       float max_out,
                       float max_iout,
                       float stable_delta_c,
                       uint32_t stable_time_ms)
{
    if (hw == nullptr || hw->htim == nullptr || pid_param == nullptr ||
        hw->max_ccr <= 0.0f)
    {
        s_temp_hw_bound = 0U;
        return;
    }

    s_temp_hw = *hw;
    s_temp_hw_bound = 1U;

    IMU_TempCtrl_SetDuty_Internal(0.0f);
    HAL_TIM_PWM_Start(s_temp_hw.htim, s_temp_hw.channel);

    omx::imu::TempControllerConfig config {};
    config.target_c = target_temp_c;
    config.gains = {pid_param[0], pid_param[1], pid_param[2]};
    config.maximum_output = max_out;
    config.maximum_integral = max_iout;
    config.stable_delta_c = stable_delta_c;
    config.stable_time_ms = stable_time_ms;
    config.min_dt_s = CONF_IMU_INS_DT_MIN_S;
    config.max_dt_s = CONF_IMU_INS_DT_MAX_S;
    if (!s_imu_temp_controller.configure(config))
    {
        s_imu_temp_controller.latchFault();
        IMU_TempCtrl_SetDuty_Internal(0.0f);
    }
    s_temp_blocking_last_tick = 0U;
}

void IMU_TempCtrl_Step(void)
{
    imu_sample_t sample;
    imu_status_t st = IMU_SampleOnceBlocking(&sample);

    if (st != IMU_STATUS_OK)
    {
        IMU_TempCtrl_Fault();
        return;
    }

    const uint32_t now_ms = HAL_GetTick();
    float dt_s = 0.001f;
    if (s_temp_blocking_last_tick != 0U)
    {
        dt_s = (now_ms - s_temp_blocking_last_tick) / 1000.0f;
    }
    s_temp_blocking_last_tick = now_ms;
    IMU_TempCtrl_ControlWithTemp(sample.temperature, dt_s);
}

void IMU_TempCtrl_StepWithTemp(float current_temp_c, float dt_s)
{
    IMU_TempCtrl_ControlWithTemp(current_temp_c, dt_s);
}

imu_temp_state_t IMU_TempCtrl_GetState(void)
{
    return static_cast<imu_temp_state_t>(s_imu_temp_controller.result().state);
}

uint8_t IMU_TempCtrl_IsStable(void)
{
    return (s_imu_temp_controller.result().state ==
            omx::imu::TempControllerState::Stable) ? 1U : 0U;
}

void IMU_TempCtrl_SetTarget(float target_temp_c)
{
    s_imu_temp_controller.setTarget(target_temp_c);
}

void IMU_TempCtrl_SetEnable(uint8_t enable)
{
    s_imu_temp_controller.setEnabled(enable != 0U);
    if (enable == 0U)
    {
        IMU_TempCtrl_SetDuty_Internal(0.0f);
    }
}

void IMU_TempCtrl_Fault(void)
{
    s_imu_temp_controller.latchFault();
    IMU_TempCtrl_SetDuty_Internal(0.0f);
}

void IMU_TempCtrl_ClearFault(void)
{
    s_imu_temp_controller.clearFault();
}

float IMU_TempCtrl_GetCurrentTemp(void)
{
    return s_imu_temp_controller.result().current_c;
}

float IMU_TempCtrl_GetTargetTemp(void)
{
    return s_imu_temp_controller.result().target_c;
}

void IMU_TempCtrl_GetPidDebug(imu_temp_pid_debug_t *out)
{
    if (out == nullptr)
    {
        return;
    }
    const auto &result = s_imu_temp_controller.result();
    out->status = static_cast<uint32_t>(result.pid.status);
    out->error = result.pid.error;
    out->p = result.pid.p;
    out->i = result.pid.i;
    out->d = result.pid.d;
    out->raw_output = result.pid.raw_output;
    out->output = result.output;
    out->dt_s = result.pid.dt_s;
    out->saturated = result.pid.saturated ? 1U : 0U;
    out->slew_limited = result.pid.slew_limited ? 1U : 0U;
    out->fault_latched = result.fault_latched ? 1U : 0U;
}

/* ============================================================
 *        静态校准实现（任意姿态，保持静止即可）
 *        —— 由上层喂样本，不再内部采样
 * ============================================================ */

static imu_calib_state_t  s_calib_state = IMU_CALIB_IDLE;
static imu_calib_result_t s_calib_result {};

static uint32_t s_calib_start_ms = 0U;
static uint32_t s_calib_count    = 0U;

static float s_gyro_sum[3] = {0.0f, 0.0f, 0.0f};
static float s_gnorm_sum   = 0.0f;

void IMU_Calib_Start(void)
{
    s_calib_result.gyro_bias[0] = 0.0f;
    s_calib_result.gyro_bias[1] = 0.0f;
    s_calib_result.gyro_bias[2] = 0.0f;
    s_calib_result.accel_scale  = 1.0f;
    s_calib_result.g_norm       = CONF_IMU_CALIB_GRAVITY_MPS2;
    s_calib_result.temp_c       = 25.0f;
    s_calib_result.success      = 0U;

    s_calib_state    = IMU_CALIB_RUNNING;
    s_calib_start_ms = HAL_GetTick();
    s_calib_count    = 0U;

    s_gyro_sum[0]    = 0.0f;
    s_gyro_sum[1]    = 0.0f;
    s_gyro_sum[2]    = 0.0f;
    s_gnorm_sum      = 0.0f;
}

void IMU_Calib_AddSample(const imu_sample_t *samp)
{
    if (s_calib_state != IMU_CALIB_RUNNING)
        return;
    if (!samp)
        return;

    uint32_t now = HAL_GetTick();
    if (now - s_calib_start_ms > CONF_IMU_CALIB_TIMEOUT_MS)
    {
        s_calib_state          = IMU_CALIB_FAILED;
        s_calib_result.success = 0U;
        return;
    }

    // 使用已经转换到 Body frame 的样本
    float ax = samp->accel[0];
    float ay = samp->accel[1];
    float az = samp->accel[2];
    float gnorm = sqrtf(ax * ax + ay * ay + az * az);

    s_gnorm_sum   += gnorm;
    s_gyro_sum[0] += samp->gyro[0];
    s_gyro_sum[1] += samp->gyro[1];
    s_gyro_sum[2] += samp->gyro[2];
    s_calib_count++;

    if (s_calib_count < CONF_IMU_CALIB_SAMPLES)
        return;

    // 样本足够，算均值
    float gnorm_avg = s_gnorm_sum / (float)CONF_IMU_CALIB_SAMPLES;
    float gyro_bias[3] = {
        s_gyro_sum[0] / (float)CONF_IMU_CALIB_SAMPLES,
        s_gyro_sum[1] / (float)CONF_IMU_CALIB_SAMPLES,
        s_gyro_sum[2] / (float)CONF_IMU_CALIB_SAMPLES,
    };

    float accel_scale = 1.0f;
    if (gnorm_avg > 1e-3f)
    {
        accel_scale = CONF_IMU_CALIB_GRAVITY_MPS2 / gnorm_avg;
    }

    s_calib_result.gyro_bias[0] = gyro_bias[0];
    s_calib_result.gyro_bias[1] = gyro_bias[1];
    s_calib_result.gyro_bias[2] = gyro_bias[2];
    s_calib_result.g_norm       = gnorm_avg;
    s_calib_result.accel_scale  = accel_scale;
    s_calib_result.temp_c       = samp->temperature;
    s_calib_result.success      = 1U;

    s_calib_state = IMU_CALIB_DONE;
}

imu_calib_state_t IMU_Calib_GetState(void)
{
    return s_calib_state;
}

void IMU_Calib_GetResult(imu_calib_result_t *out)
{
    if (!out) return;

    out->gyro_bias[0] = 0.0f;
    out->gyro_bias[1] = 0.0f;
    out->gyro_bias[2] = 0.0f;
    out->accel_scale  = 1.0f;
    out->g_norm       = CONF_IMU_CALIB_GRAVITY_MPS2;
    out->temp_c       = 25.0f;
    out->success      = 0U;

    if (s_calib_state == IMU_CALIB_DONE && s_calib_result.success)
    {
        *out = s_calib_result;
    }
}

void IMU_ActiveCalib_SetDefault(float temp_c)
{
    imu_make_default_calib(&s_active_calib, temp_c);
}

void IMU_ActiveCalib_Set(const imu_calib_result_t *calib)
{
    if (calib == nullptr)
    {
        return;
    }
    s_active_calib = *calib;
}

void IMU_ActiveCalib_Get(imu_calib_result_t *out)
{
    if (out == nullptr)
    {
        return;
    }
    *out = s_active_calib;
}

void IMU_ApplyActiveCalibration(imu_sample_t *samp)
{
    if (samp == nullptr)
    {
        return;
    }

    samp->gyro[0] -= s_active_calib.gyro_bias[0];
    samp->gyro[1] -= s_active_calib.gyro_bias[1];
    samp->gyro[2] -= s_active_calib.gyro_bias[2];

    samp->accel[0] *= s_active_calib.accel_scale;
    samp->accel[1] *= s_active_calib.accel_scale;
    samp->accel[2] *= s_active_calib.accel_scale;
}

imu_calib_storage_status_t IMU_CalibStorage_Init(uint8_t i2c_bus_id,
                                                 uint16_t i2c_addr_7bit)
{
    lib_24c02_bl_status_t st =
        lib_24c02_bl_init(&s_calib_eeprom,
                          static_cast<bsp_i2c_bus_id_t>(i2c_bus_id),
                          i2c_addr_7bit);
    s_calib_eeprom_status = imu_calib_storage_from_eeprom(st);
    return s_calib_eeprom_status;
}

imu_calib_storage_status_t IMU_CalibStorage_Load(uint8_t mem_addr,
                                                 imu_calib_result_t *out)
{
    if (out == nullptr)
    {
        return IMU_CALIB_STORAGE_INVALID_PARAM;
    }
    if (s_calib_eeprom_status != IMU_CALIB_STORAGE_OK)
    {
        return s_calib_eeprom_status;
    }

    imu_calib_persist_t persist {};
    lib_24c02_bl_status_t st =
        lib_24c02_bl_read(&s_calib_eeprom,
                          mem_addr,
                          reinterpret_cast<uint8_t *>(&persist),
                          sizeof(persist),
                          CONF_IMU_EEPROM_XFER_TIMEOUT_MS);
    imu_calib_storage_status_t mapped = imu_calib_storage_from_eeprom(st);
    if (mapped != IMU_CALIB_STORAGE_OK)
    {
        return mapped;
    }
    if ((persist.magic != IMU_CALIB_EEPROM_MAGIC) ||
        (persist.version != IMU_CALIB_EEPROM_VERSION))
    {
        return IMU_CALIB_STORAGE_BAD_DATA;
    }

    imu_calib_result_from_persist(out, &persist);
    return IMU_CALIB_STORAGE_OK;
}

imu_calib_storage_status_t IMU_CalibStorage_Save(uint8_t mem_addr,
                                                 const imu_calib_result_t *calib)
{
    if (calib == nullptr)
    {
        return IMU_CALIB_STORAGE_INVALID_PARAM;
    }
    if (s_calib_eeprom_status != IMU_CALIB_STORAGE_OK)
    {
        return s_calib_eeprom_status;
    }

    imu_calib_persist_t persist {};
    imu_calib_persist_from_result(&persist, calib);

    lib_24c02_bl_status_t st =
        lib_24c02_bl_write(&s_calib_eeprom,
                           mem_addr,
                           reinterpret_cast<const uint8_t *>(&persist),
                           sizeof(persist),
                           CONF_IMU_EEPROM_XFER_TIMEOUT_MS,
                           CONF_IMU_EEPROM_WRITE_CYCLE_MS);
    return imu_calib_storage_from_eeprom(st);
}

} // extern "C"
