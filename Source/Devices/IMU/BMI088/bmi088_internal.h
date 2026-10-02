//
// BMI088.h  —— 适配你当前的 BMI088.c（支持异步采样）
//
#ifndef SRNOS_BMI088_H
#define SRNOS_BMI088_H

#include "IO/SPI/spi_internal.h"
#include "Devices/IMU/BMI088/bmi088_registers.h"
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
    } SRNOS_BMI088Data;

    /* 和 .c 里用到的错误码保持一致 */
    typedef enum
    {
        SRNOS_BMI088_OK = 0,
        SRNOS_BMI088_ERR_NO_SENSOR = 1,
        SRNOS_BMI088_ERR_CONFIG    = 2,
        SRNOS_BMI088_ERR_SPI       = 3,
        SRNOS_BMI088_BUSY          = 4,
        SRNOS_BMI088_ERR_STALE     = 5
    } SRNOS_BMI088Status;

    typedef enum
    {
        SRNOS_BMI088_PHASE_IDLE = 0,
        SRNOS_BMI088_PHASE_ACC_READ,
        SRNOS_BMI088_PHASE_GYRO_READ,
        SRNOS_BMI088_PHASE_TEMP_READ
    } SRNOS_BMI088Phase;

    typedef struct
    {
        SRNOS_BMI088Phase   phase;
        SRNOS_BMI088Status  last_status;
        uint32_t             sample_start_tick;
        uint32_t             last_sample_tick;
        uint32_t             sample_seq;
        uint32_t             stale_count;
        uint32_t             spi_error_count;
    } SRNOS_BMI088Health;

    /* Completion runs in SPI dispatcher task: only notify, never block. */
    typedef void (*SRNOS_BMI088CompletionFn)(void *user);

    /**
     * @brief 初始化 BMI088（阻塞寄存器配置，无用户校准）
     */
    SRNOS_BMI088Status SRNOS_BMI088Init(SRNOS_SPIBusId bus_id,
                                        GPIO_TypeDef *acc_cs_port,  uint16_t acc_cs_pin,
                                        GPIO_TypeDef *gyro_cs_port, uint16_t gyro_cs_pin,
                                        SRNOS_BMI088CompletionFn completion, void *user);

    /**
     * @brief 异步开始一次完整采样（acc+gyro+temp）
     *
     * - 内部会启动 ACC 读 DMA，之后在 SPI 回调里按 state machine 继续
     * - 若上一次还没完成，会返回 SRNOS_BMI088_BUSY
     */
    SRNOS_BMI088Status SRNOS_BMI088BeginSample(void);

    /**
     * @brief 如果采样已经完成，则取出最新一帧数据
     *
     * - 若数据尚未 ready，返回 SRNOS_BMI088_BUSY
     * - 若 OK，则填充 out 并清除 ready 标志
     */
    SRNOS_BMI088Status SRNOS_BMI088GetSampleIfReady(SRNOS_BMI088Data *out);

    /**
     * @brief 经共享SPI所有者取消本器件请求，确认结束后重置软件状态。
     * BUSY表示仍持有缓冲，须等待下一周期重试；不私自重配共享总线。
     */
    SRNOS_BMI088Status SRNOS_BMI088ResetAsyncState(void);

    void SRNOS_BMI088GetSPIStatistics(SRNOS_SPIStatistics *accel, SRNOS_SPIStatistics *gyro);
    void SRNOS_BMI088GetHealth(SRNOS_BMI088Health *out);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_BMI088_H
