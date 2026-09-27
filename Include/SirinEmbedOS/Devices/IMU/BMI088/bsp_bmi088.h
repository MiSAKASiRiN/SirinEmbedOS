//
// bsp_bmi088.h  —— 适配你当前的 bsp_bmi088.c（支持异步采样）
//
#ifndef H723VG_V2_FREERTOS_BSP_BMI088_H
#define H723VG_V2_FREERTOS_BSP_BMI088_H

#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/Devices/IMU/BMI088/bsp_bmi088_reg.h"
#include "stm32h7xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct
    {
        float gyro[3];        /* deg/s */
        float accel[3];       /* m/s^2 */
        int16_t raw_accel[3], raw_gyro[3], raw_temperature;
        uint64_t acquisition_start_us, acquisition_end_us;
        float temperature;    /* deg C */
    } bmi088_data_t;

    /* 和 .c 里用到的错误码保持一致 */
    typedef enum
    {
        BSP_BMI088_OK = 0,
        BSP_BMI088_ERR_NO_SENSOR = 1,
        BSP_BMI088_ERR_CONFIG    = 2,
        BSP_BMI088_ERR_SPI       = 3,
        BSP_BMI088_BUSY          = 4,
        BSP_BMI088_ERR_STALE     = 5
    } bsp_bmi088_status_t;

    typedef enum
    {
        BSP_BMI088_PHASE_IDLE = 0,
        BSP_BMI088_PHASE_ACC_READ,
        BSP_BMI088_PHASE_GYRO_READ,
        BSP_BMI088_PHASE_TEMP_READ
    } bsp_bmi088_phase_t;

    typedef struct
    {
        bsp_bmi088_phase_t   phase;
        bsp_bmi088_status_t  last_status;
        uint32_t             sample_start_tick;
        uint32_t             last_sample_tick;
        uint32_t             sample_seq;
        uint32_t             stale_count;
        uint32_t             spi_error_count;
    } bsp_bmi088_health_t;

    /* Completion runs in SPI dispatcher task: only notify, never block. */
    typedef void (*bsp_bmi088_completion_fn)(void *user);

    /**
     * @brief 初始化 BMI088（阻塞寄存器配置，无用户校准）
     */
    bsp_bmi088_status_t BSP_BMI088_Init(bsp_spi_bus_id_t bus_id,
                                        GPIO_TypeDef *acc_cs_port,  uint16_t acc_cs_pin,
                                        GPIO_TypeDef *gyro_cs_port, uint16_t gyro_cs_pin,
                                        bsp_bmi088_completion_fn completion, void *user);

    /**
     * @brief 异步开始一次完整采样（acc+gyro+temp）
     *
     * - 内部会启动 ACC 读 DMA，之后在 SPI 回调里按 state machine 继续
     * - 若上一次还没完成，会返回 BSP_BMI088_BUSY
     */
    bsp_bmi088_status_t BSP_BMI088_BeginSample(void);

    /**
     * @brief 如果采样已经完成，则取出最新一帧数据
     *
     * - 若数据尚未 ready，返回 BSP_BMI088_BUSY
     * - 若 OK，则填充 out 并清除 ready 标志
     */
    bsp_bmi088_status_t BSP_BMI088_GetSampleIfReady(bmi088_data_t *out);

    /**
     * @brief 经共享SPI所有者取消本器件请求，确认结束后重置软件状态。
     * BUSY表示仍持有缓冲，须等待下一周期重试；不私自重配共享总线。
     */
    bsp_bmi088_status_t BSP_BMI088_ResetAsyncState(void);

    void BSP_BMI088_GetSPIStatistics(bsp_spi_statistics_t *accel, bsp_spi_statistics_t *gyro);
    void BSP_BMI088_GetHealth(bsp_bmi088_health_t *out);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_BSP_BMI088_H
