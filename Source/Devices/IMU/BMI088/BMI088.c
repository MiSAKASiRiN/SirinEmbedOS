#include "Devices/IMU/BMI088/bmi088_internal.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include <string.h>
#include <math.h>
#include "cmsis_os2.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"

/* ==== 灵敏度常量兜底：优先用你原来头文件里的 ===== */
#ifndef SRNOS_BMI088_ACCEL_SEN
  #if defined(SRNOS_BMI088_ACCEL_3G_SEN)
    #define SRNOS_BMI088_ACCEL_SEN  SRNOS_BMI088_ACCEL_3G_SEN
  #elif defined(SRNOS_BMI088_ACCEL_SEN)
    #define SRNOS_BMI088_ACCEL_SEN  SRNOS_BMI088_ACCEL_SEN
  #else
    #error "SRNOS_BMI088: no accel sensitivity macro configured."
  #endif
#endif

#ifndef SRNOS_BMI088_GYRO_SEN
  #if defined(SRNOS_BMI088_GYRO_2000_SEN)
    #define SRNOS_BMI088_GYRO_SEN   SRNOS_BMI088_GYRO_2000_SEN
  #else
    #error "SRNOS_BMI088: no gyro sensitivity macro configured."
  #endif
#endif

/* 延时兜底 */
#ifndef SRNOS_BMI088_LONG_DELAY_TIME
#define SRNOS_BMI088_LONG_DELAY_TIME   200u
#endif

/* 温度公式兜底：T = 23 + raw * 0.125 */
#ifndef SRNOS_BMI088_TEMP_FACTOR
#define SRNOS_BMI088_TEMP_FACTOR   (0.125f)
#endif
#ifndef SRNOS_BMI088_TEMP_OFFSET
#define SRNOS_BMI088_TEMP_OFFSET   (23.0f)
#endif

#define SRNOS_BMI088_SPI_TIMEOUT_MS   (5u)
#define SRNOS_BMI088_SAMPLE_TIMEOUT_MS (5u)

/* ==== SPI 设备 & DMA buffer ===================================== */

static SRNOS_SPIDevice s_acc_dev;
static SRNOS_SPIDevice s_gyro_dev;
static uint64_t s_acquisition_start, s_acquisition_end;

#define SRNOS_BMI088_SPI_MAX_XFER   (64u)

static uint8_t s_tx[SRNOS_BMI088_SPI_MAX_XFER];

static uint8_t s_rx[SRNOS_BMI088_SPI_MAX_XFER];

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgBMI088 SRNOS_DebugDbgBMI088;

/* ==== 帮助函数 =================================================== */

static inline int16_t SRNOS_BMI088MakeS16(uint8_t lo, uint8_t hi)
{
    return (int16_t)((((uint16_t)hi) << 8) | (uint16_t)lo);
}

static void SRNOS_BMI088DelayMs(uint32_t ms)
{
    if (osKernelGetState()==osKernelRunning) osDelay((ms*osKernelGetTickFreq()+999)/1000);
    else HAL_Delay(ms);
}

/* ==== SPI helpers: blocking 版本（用于初始化寄存器） = */

static uint8_t acc_write(uint8_t reg, uint8_t data)
{
    s_tx[0] = reg & 0x7Fu;
    s_tx[1] = data;

    return (SRNOS_SPITransferBlocking(&s_acc_dev,
                                      s_tx,
                                      s_rx,
                                      2u,
                                      SRNOS_BMI088_SPI_TIMEOUT_MS) == SRNOS_SPI_OK) ? 0u : 1u;
}

static uint8_t acc_read1(uint8_t reg, uint8_t *data)
{
    s_tx[0] = reg | 0x80u;
    s_tx[1] = 0xFFu;
    s_tx[2] = 0xFFu;

    if (SRNOS_SPITransferBlocking(&s_acc_dev,
                                  s_tx,
                                  s_rx,
                                  3u,
                                  SRNOS_BMI088_SPI_TIMEOUT_MS) != SRNOS_SPI_OK)
    {
        return 1u;
    }

    *data = s_rx[2];
    return 0u;
}


static uint8_t gyro_write(uint8_t reg, uint8_t data)
{
    s_tx[0] = reg & 0x7Fu;
    s_tx[1] = data;

    return (SRNOS_SPITransferBlocking(&s_gyro_dev,
                                      s_tx,
                                      s_rx,
                                      2u,
                                      SRNOS_BMI088_SPI_TIMEOUT_MS) == SRNOS_SPI_OK) ? 0u : 1u;
}

static uint8_t gyro_readN(uint8_t reg, uint8_t *buf, uint8_t len)
{
    if ((size_t)(len + 1u) > sizeof(s_tx))
        return 1u;

    s_tx[0] = reg | 0x80u;
    for (uint8_t i = 0; i < len; ++i)
    {
        s_tx[1 + i] = 0xFFu;
    }

    if (SRNOS_SPITransferBlocking(&s_gyro_dev,
                                  s_tx,
                                  s_rx,
                                  (size_t)(len + 1u),
                                  SRNOS_BMI088_SPI_TIMEOUT_MS) != SRNOS_SPI_OK)
    {
        return 1u;
    }

    for (uint8_t i = 0; i < len; ++i)
    {
        buf[i] = s_rx[1u + i];
    }
    return 0u;
}

static uint8_t acc_verify(uint8_t reg, uint8_t expected, uint8_t mask)
{
    uint8_t actual = 0u;
    if (acc_read1(reg, &actual) != 0u)
    {
        return SRNOS_BMI088_ERR_SPI;
    }
    if ((actual & mask) != (expected & mask))
    {
        SRNOS_LOGE("BMI088 ACC reg 0x%02X verify failed: got=0x%02X expect=0x%02X mask=0x%02X",
             reg, actual, expected, mask);
        SRNOS_DebugDbgBMI088.config_error_count++;
        return SRNOS_BMI088_ERR_CONFIG;
    }
    return SRNOS_BMI088_OK;
}

static uint8_t gyro_verify(uint8_t reg, uint8_t expected, uint8_t mask)
{
    uint8_t actual = 0u;
    if (gyro_readN(reg, &actual, 1u) != 0u)
    {
        return SRNOS_BMI088_ERR_SPI;
    }
    if ((actual & mask) != (expected & mask))
    {
        SRNOS_LOGE("BMI088 GYRO reg 0x%02X verify failed: got=0x%02X expect=0x%02X mask=0x%02X",
             reg, actual, expected, mask);
        SRNOS_DebugDbgBMI088.config_error_count++;
        return SRNOS_BMI088_ERR_CONFIG;
    }
    return SRNOS_BMI088_OK;
}

/* ==== 加速度计 / 陀螺仪 Init（blocking） ======================= */

static uint8_t SRNOS_BMI088AccelInit(void)
{
    uint8_t id = 0;

    /* check chip id */
    (void)acc_read1(SRNOS_BMI088_ACC_CHIP_ID, &id);
    SRNOS_BMI088DelayMs(1);
    (void)acc_read1(SRNOS_BMI088_ACC_CHIP_ID, &id);
    SRNOS_BMI088DelayMs(1);

    /* soft reset */
    (void)acc_write(SRNOS_BMI088_ACC_SOFTRESET, SRNOS_BMI088_ACC_SOFTRESET_VALUE);
    SRNOS_BMI088DelayMs(SRNOS_BMI088_LONG_DELAY_TIME);

    /* re-check chip id */
    (void)acc_read1(SRNOS_BMI088_ACC_CHIP_ID, &id);
    SRNOS_BMI088DelayMs(1);
    (void)acc_read1(SRNOS_BMI088_ACC_CHIP_ID, &id);
    SRNOS_BMI088DelayMs(1);

    if (id != SRNOS_BMI088_ACC_CHIP_ID_VALUE)
    {
        return SRNOS_BMI088_ERR_NO_SENSOR;
    }

    /* power config */
    if (acc_write(SRNOS_BMI088_ACC_PWR_CONF, SRNOS_BMI088_ACC_PWR_ACTIVE_MODE) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    if (acc_write(SRNOS_BMI088_ACC_PWR_CTRL, SRNOS_BMI088_ACC_ENABLE_ACC_ON) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    /* ODR / mode / filter */
    const uint8_t acc_conf = (uint8_t)(SRNOS_BMI088_ACC_NORMAL | SRNOS_BMI088_ACC_1600_HZ | SRNOS_BMI088_ACC_CONF_MUST_Set);
    if (acc_write(SRNOS_BMI088_ACC_CONF, acc_conf) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

#if defined(SRNOS_BMI088_ACC_RANGE_3G)
    const uint8_t acc_range = SRNOS_BMI088_ACC_RANGE_3G;
#elif defined(SRNOS_BMI088_ACC_RANGE_6G)
    const uint8_t acc_range = SRNOS_BMI088_ACC_RANGE_6G;
#else
    const uint8_t acc_range = 0x00u;
#endif
    if (acc_write(SRNOS_BMI088_ACC_RANGE, acc_range) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    uint8_t verify_st = acc_verify(SRNOS_BMI088_ACC_CONF, acc_conf, 0xFFu);
    if (verify_st != SRNOS_BMI088_OK)
        return verify_st;
    verify_st = acc_verify(SRNOS_BMI088_ACC_RANGE, acc_range, 0x03u);
    if (verify_st != SRNOS_BMI088_OK)
        return verify_st;

#if defined(SRNOS_BMI088_INT1_IO_CTRL)
    (void)acc_write(
        SRNOS_BMI088_INT1_IO_CTRL,
        (uint8_t)(SRNOS_BMI088_ACC_INT1_IO_ENABLE |
                  SRNOS_BMI088_ACC_INT1_GPIO_PP   |
                  SRNOS_BMI088_ACC_INT1_GPIO_LOW)
    );
    SRNOS_BMI088DelayMs(1);
#endif

#if defined(SRNOS_BMI088_INT_MAP_DATA)
    (void)acc_write(
        SRNOS_BMI088_INT_MAP_DATA,
        SRNOS_BMI088_ACC_INT1_DRDY_INTERRUPT
    );
    SRNOS_BMI088DelayMs(1);
#endif

    return SRNOS_BMI088_OK;
}

static uint8_t SRNOS_BMI088GyroInit(void)
{
    uint8_t id = 0;

    /* check chip id */
    (void)gyro_readN(SRNOS_BMI088_GYRO_CHIP_ID, &id, 1);
    SRNOS_BMI088DelayMs(1);
    (void)gyro_readN(SRNOS_BMI088_GYRO_CHIP_ID, &id, 1);
    SRNOS_BMI088DelayMs(1);

    /* soft reset */
    (void)gyro_write(SRNOS_BMI088_GYRO_SOFTRESET, SRNOS_BMI088_GYRO_SOFTRESET_VALUE);
    SRNOS_BMI088DelayMs(SRNOS_BMI088_LONG_DELAY_TIME);

    /* re-check chip id */
    (void)gyro_readN(SRNOS_BMI088_GYRO_CHIP_ID, &id, 1);
    SRNOS_BMI088DelayMs(1);
    (void)gyro_readN(SRNOS_BMI088_GYRO_CHIP_ID, &id, 1);
    SRNOS_BMI088DelayMs(1);

    if (id != SRNOS_BMI088_GYRO_CHIP_ID_VALUE)
    {
        return SRNOS_BMI088_ERR_NO_SENSOR;
    }

    if (gyro_write(SRNOS_BMI088_GYRO_RANGE, SRNOS_BMI088_GYRO_2000) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

#if defined(SRNOS_BMI088_GYRO_2000_230_HZ)
    const uint8_t gyro_bandwidth = (uint8_t)(SRNOS_BMI088_GYRO_2000_230_HZ | SRNOS_BMI088_GYRO_BANDWIDTH_MUST_Set);
#else
    const uint8_t gyro_bandwidth = (uint8_t)(SRNOS_BMI088_GYRO_1000_116_HZ | SRNOS_BMI088_GYRO_BANDWIDTH_MUST_Set);
#endif
    if (gyro_write(SRNOS_BMI088_GYRO_BANDWIDTH, gyro_bandwidth) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    if (gyro_write(SRNOS_BMI088_GYRO_LPM1, SRNOS_BMI088_GYRO_NORMAL_MODE) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    if (gyro_write(SRNOS_BMI088_GYRO_CTRL, SRNOS_BMI088_DRDY_ON) != 0u)
        return SRNOS_BMI088_ERR_SPI;
    SRNOS_BMI088DelayMs(1);

    uint8_t verify_st = gyro_verify(SRNOS_BMI088_GYRO_RANGE, SRNOS_BMI088_GYRO_2000, 0x07u);
    if (verify_st != SRNOS_BMI088_OK)
        return verify_st;
    verify_st = gyro_verify(SRNOS_BMI088_GYRO_BANDWIDTH, gyro_bandwidth, 0x07u);
    if (verify_st != SRNOS_BMI088_OK)
        return verify_st;

#if defined(SRNOS_BMI088_GYRO_INT3_INT4_IO_CONF)
    (void)gyro_write(
        SRNOS_BMI088_GYRO_INT3_INT4_IO_CONF,
        (uint8_t)(SRNOS_BMI088_GYRO_INT3_GPIO_PP | SRNOS_BMI088_GYRO_INT3_GPIO_LOW)
    );
    SRNOS_BMI088DelayMs(1);
#endif

#if defined(SRNOS_BMI088_GYRO_INT3_INT4_IO_MAP)
    (void)gyro_write(
        SRNOS_BMI088_GYRO_INT3_INT4_IO_MAP,
        SRNOS_BMI088_GYRO_DRDY_IO_INT3
    );
    SRNOS_BMI088DelayMs(1);
#endif

    return SRNOS_BMI088_OK;
}

/* ==== 异步采样状态机 ============================================ */

/* raw 数据由 SPI dispatcher 填写，BMI task 完成 SI 转换 */
static SRNOS_BMI088CompletionFn s_completion;
static void *s_completion_user;
static volatile int16_t s_raw_acc[3]  = {0};
static volatile int16_t s_raw_gyro[3] = {0};
static volatile int16_t s_raw_temp    = 0;

typedef enum
{
    SRNOS_BMI088_SM_IDLE = 0,
    SRNOS_BMI088_SM_ACC_READ,
    SRNOS_BMI088_SM_GYRO_READ,
    SRNOS_BMI088_SM_TEMP_READ
} SRNOS_BMI088SmState;

static volatile SRNOS_BMI088SmState s_sm_state = SRNOS_BMI088_SM_IDLE;
static volatile uint8_t           s_sample_ready = 0;
static volatile uint8_t           s_kick_pending = 0;
static volatile uint32_t          s_sample_start_tick = 0u;
static volatile uint32_t          s_last_sample_tick = 0u;
static volatile uint32_t          s_sample_seq = 0u;
static volatile uint32_t          s_stale_count = 0u;
static volatile uint32_t          s_spi_error_count = 0u;
static volatile SRNOS_BMI088Status s_last_status = SRNOS_BMI088_OK;

static void SRNOS_BMI088SPICb(SRNOS_SPIDevice *dev, void *user_ctx, SRNOS_SPIStatus status);
static SRNOS_BMI088Status SRNOS_BMI088KickNext(void);
static SRNOS_BMI088Status SRNOS_BMI088CheckStale(void);

static void SRNOS_BMI088DbgSync(SRNOS_BMI088Status status)
{
    SRNOS_DebugDbgBMI088.phase = (uint32_t)s_sm_state;
    SRNOS_DebugDbgBMI088.sample_start_tick = s_sample_start_tick;
    SRNOS_DebugDbgBMI088.last_sample_tick = s_last_sample_tick;
    SRNOS_DebugDbgBMI088.sample_seq = s_sample_seq;
    SRNOS_DebugDbgBMI088.stale_count = s_stale_count;
    SRNOS_DebugDbgBMI088.spi_error_count = s_spi_error_count;
    SRNOS_DbgEnd(&SRNOS_DebugDbgBMI088.h, HAL_GetTick(), (uint32_t)status);
}

/* ==== 公共接口：Init ============================================ */

SRNOS_BMI088Status SRNOS_BMI088Init(SRNOS_SPIBusId bus_id,
                                    GPIO_TypeDef *acc_cs_port,  uint16_t acc_cs_pin,
                                    GPIO_TypeDef *gyro_cs_port, uint16_t gyro_cs_pin,
                                    SRNOS_BMI088CompletionFn completion, void *user)
{
    s_completion=completion;s_completion_user=user;
    SRNOS_DbgZero(&SRNOS_DebugDbgBMI088, sizeof(SRNOS_DebugDbgBMI088));
    SRNOS_DbgInit(&SRNOS_DebugDbgBMI088.h,
                 SRNOS_DBG_MODULE_BMI088,
                 0u,
                 (uint16_t)sizeof(SRNOS_DebugDbgBMI088));

    if (SRNOS_SPIDeviceInit(&s_acc_dev, bus_id, acc_cs_port, acc_cs_pin) != SRNOS_SPI_OK)
    {
        SRNOS_DebugDbgBMI088.spi_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        return SRNOS_BMI088_ERR_SPI;
    }
    if (SRNOS_SPIDeviceInit(&s_gyro_dev, bus_id, gyro_cs_port, gyro_cs_pin) != SRNOS_SPI_OK)
    {
        SRNOS_DebugDbgBMI088.spi_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        return SRNOS_BMI088_ERR_SPI;
    }

    s_acc_dev.imu_priority=1;
    s_gyro_dev.imu_priority=1;
    uint8_t err;

    err = SRNOS_BMI088AccelInit();
    if (err != SRNOS_BMI088_OK)
    {
        if (err == SRNOS_BMI088_ERR_SPI)
        {
            SRNOS_DebugDbgBMI088.spi_error_count++;
        }
        SRNOS_BMI088DbgSync((SRNOS_BMI088Status)err);
        return err;
    }

    err = SRNOS_BMI088GyroInit();
    if (err != SRNOS_BMI088_OK)
    {
        if (err == SRNOS_BMI088_ERR_SPI)
        {
            SRNOS_DebugDbgBMI088.spi_error_count++;
        }
        SRNOS_BMI088DbgSync((SRNOS_BMI088Status)err);
        return err;
    }

    s_sm_state     = SRNOS_BMI088_SM_IDLE;
    s_sample_ready = 0;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_sample_tick = HAL_GetTick();
    s_sample_seq = 0u;
    s_stale_count = 0u;
    s_spi_error_count = 0u;
    s_last_status = SRNOS_BMI088_OK;
    SRNOS_BMI088DbgSync(SRNOS_BMI088_OK);

    return SRNOS_BMI088_OK;
}

/* ==== 公共接口：异步采样 ======================================== */

SRNOS_BMI088Status SRNOS_BMI088BeginSample(void)
{
    if (s_sm_state != SRNOS_BMI088_SM_IDLE)
    {
        SRNOS_BMI088Status stale_st = SRNOS_BMI088CheckStale();
        if (stale_st != SRNOS_BMI088_OK)
        {
            SRNOS_BMI088DbgSync(stale_st);
            return stale_st;
        }
        SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
        return SRNOS_BMI088_BUSY;
    }

    if ((size_t)(6 + 2u) > sizeof(s_tx))
    {
        SRNOS_LOGE_SRC("BMI088", "BeginSample: ACC buf too small");
        SRNOS_DebugDbgBMI088.config_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_CONFIG);
        return SRNOS_BMI088_ERR_CONFIG;
    }

    s_tx[0] = SRNOS_BMI088_ACCEL_XOUT_L | 0x80u;
    s_tx[1] = 0xFFu;
    for (uint8_t i = 0; i < 6u; ++i)
    {
        s_tx[2u + i] = 0xFFu;
    }

    s_sample_ready = 0;
    s_kick_pending = 0;
    s_acquisition_start = SRNOS_TimeGetUptimeUs();
    s_sample_start_tick = HAL_GetTick();
    s_last_status = SRNOS_BMI088_BUSY;
    s_sm_state     = SRNOS_BMI088_SM_ACC_READ;

    SRNOS_SPIStatus ret = SRNOS_SPITransferAsyncWait(&s_acc_dev,
                                                       s_tx,
                                                       s_rx,
                                                       8u,
                                                       SRNOS_BMI088SPICb,
                                                       NULL,
                                                       1u);
    if (ret != SRNOS_SPI_OK)
    {
        s_sm_state = SRNOS_BMI088_SM_IDLE;
        s_sample_start_tick = 0u;
        if (ret == SRNOS_SPI_BUSY)
        {
            SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
            return SRNOS_BMI088_BUSY;
        }
        SRNOS_LOGE_SRC("BMI088", "BeginSample: SPI async start fail=%d", (int)ret);
        s_last_status = SRNOS_BMI088_ERR_SPI;
        s_spi_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        return SRNOS_BMI088_ERR_SPI;
    }

    SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
    return SRNOS_BMI088_OK;
}

SRNOS_BMI088Status SRNOS_BMI088GetSampleIfReady(SRNOS_BMI088Data *out)
{
    if (out == NULL)
    {
        SRNOS_DebugDbgBMI088.config_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_CONFIG);
        return SRNOS_BMI088_ERR_CONFIG;
    }

    SRNOS_BMI088Status stale_st = SRNOS_BMI088CheckStale();
    if (stale_st != SRNOS_BMI088_OK)
    {
        SRNOS_BMI088DbgSync(stale_st);
        return stale_st;
    }

    SRNOS_BMI088Status kick_st = SRNOS_BMI088KickNext();
    if (kick_st == SRNOS_BMI088_ERR_SPI || kick_st == SRNOS_BMI088_ERR_CONFIG)
    {
        SRNOS_BMI088DbgSync(kick_st);
        return kick_st;
    }

    if ((s_last_status == SRNOS_BMI088_ERR_SPI) &&
        (s_sm_state == SRNOS_BMI088_SM_IDLE) &&
        (s_sample_ready == 0u))
    {
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        return SRNOS_BMI088_ERR_SPI;
    }

    if (s_sample_ready == 0u)
    {
        SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
        return SRNOS_BMI088_BUSY;
    }

    /* raw -> float at task context */
    float ax = (float)s_raw_acc[0] * SRNOS_BMI088_ACCEL_SEN;
    float ay = (float)s_raw_acc[1] * SRNOS_BMI088_ACCEL_SEN;
    float az = (float)s_raw_acc[2] * SRNOS_BMI088_ACCEL_SEN;

    float gx = (float)s_raw_gyro[0] * SRNOS_BMI088_GYRO_SEN;
    float gy = (float)s_raw_gyro[1] * SRNOS_BMI088_GYRO_SEN;
    float gz = (float)s_raw_gyro[2] * SRNOS_BMI088_GYRO_SEN;

    float t;
    {
        int16_t raw = s_raw_temp;
        t = (float)raw * SRNOS_BMI088_TEMP_FACTOR + SRNOS_BMI088_TEMP_OFFSET;
    }

    out->accel[0] = ax;
    out->accel[1] = ay;
    out->accel[2] = az;

    out->gyro[0]  = gx;
    out->gyro[1]  = gy;
    out->gyro[2]  = gz;

    out->temperature = t;
    for(unsigned i=0;i<3;i++) { out->raw_accel[i]=s_raw_acc[i]; out->raw_gyro[i]=s_raw_gyro[i]; }
    out->raw_temperature=s_raw_temp;
    out->acquisition_start_us=s_acquisition_start;
    out->acquisition_end_us=s_acquisition_end;

    s_sample_ready = 0;
    SRNOS_BMI088DbgSync(SRNOS_BMI088_OK);

    return SRNOS_BMI088_OK;
}

SRNOS_BMI088Status SRNOS_BMI088ResetAsyncState(void)
{
    if ((s_acc_dev.bus == NULL) || (s_gyro_dev.bus == NULL))
    {
        SRNOS_DebugDbgBMI088.config_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_CONFIG);
        return SRNOS_BMI088_ERR_CONFIG;
    }

    /* Queued and active requests both own s_rx until cancellation is acknowledged. */
    SRNOS_SPIStatus acc=SRNOS_SPICancel(&s_acc_dev), gyro=SRNOS_SPICancel(&s_gyro_dev);
    if(acc==SRNOS_SPI_BUSY || gyro==SRNOS_SPI_BUSY) return SRNOS_BMI088_BUSY;
    if(acc!=SRNOS_SPI_OK || gyro!=SRNOS_SPI_OK) return SRNOS_BMI088_ERR_SPI;

    s_raw_acc[0] = 0;
    s_raw_acc[1] = 0;
    s_raw_acc[2] = 0;
    s_raw_gyro[0] = 0;
    s_raw_gyro[1] = 0;
    s_raw_gyro[2] = 0;
    s_raw_temp = 0;

    s_sm_state = SRNOS_BMI088_SM_IDLE;
    s_sample_ready = 0u;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_status = SRNOS_BMI088_OK;
    SRNOS_BMI088DbgSync(SRNOS_BMI088_OK);

    return SRNOS_BMI088_OK;
}

void SRNOS_BMI088GetHealth(SRNOS_BMI088Health *out)
{
    if (!out)
    {
        return;
    }

    out->phase = (SRNOS_BMI088Phase)s_sm_state;
    out->last_status = s_last_status;
    out->sample_start_tick = s_sample_start_tick;
    out->last_sample_tick = s_last_sample_tick;
    out->sample_seq = s_sample_seq;
    out->stale_count = s_stale_count;
    out->spi_error_count = s_spi_error_count;
    SRNOS_BMI088DbgSync(s_last_status);
}

static SRNOS_BMI088Status SRNOS_BMI088CheckStale(void)
{
    if (s_sm_state == SRNOS_BMI088_SM_IDLE)
    {
        return SRNOS_BMI088_OK;
    }

    uint32_t start = s_sample_start_tick;
    if (start == 0u)
    {
        return SRNOS_BMI088_OK;
    }

    uint32_t now = HAL_GetTick();
    if ((now - start) <= SRNOS_BMI088_SAMPLE_TIMEOUT_MS)
    {
        return SRNOS_BMI088_OK;
    }

    s_stale_count++;
    (void)SRNOS_BMI088ResetAsyncState();
    s_last_status = SRNOS_BMI088_ERR_STALE;
    SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_STALE);
    return SRNOS_BMI088_ERR_STALE;
}

static SRNOS_BMI088Status SRNOS_BMI088KickNext(void)
{
    if (s_kick_pending == 0u)
    {
        return SRNOS_BMI088_OK;
    }

    SRNOS_SPIStatus ret = SRNOS_SPI_OK;

    if (s_sm_state == SRNOS_BMI088_SM_GYRO_READ)
    {
        if ((size_t)(8 + 1u) > sizeof(s_tx))
        {
            SRNOS_LOGE_SRC("BMI088", "kick: gyro buf too small");
            s_sm_state = SRNOS_BMI088_SM_IDLE;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = SRNOS_BMI088_ERR_CONFIG;
            SRNOS_DebugDbgBMI088.config_error_count++;
            SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_CONFIG);
            return SRNOS_BMI088_ERR_CONFIG;
        }

        s_tx[0] = SRNOS_BMI088_GYRO_CHIP_ID | 0x80u;
        for (uint8_t i = 0; i < 8u; ++i)
        {
            s_tx[1u + i] = 0xFFu;
        }

        ret = SRNOS_SPITransferAsync(&s_gyro_dev,
                                     s_tx,
                                     s_rx,
                                     9u,
                                     SRNOS_BMI088SPICb,
                                     NULL);
    }
    else if (s_sm_state == SRNOS_BMI088_SM_TEMP_READ)
    {
        if ((size_t)(2 + 2u) > sizeof(s_tx))
        {
            SRNOS_LOGE_SRC("BMI088", "kick: temp buf too small");
            s_sm_state = SRNOS_BMI088_SM_IDLE;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = SRNOS_BMI088_ERR_CONFIG;
            SRNOS_DebugDbgBMI088.config_error_count++;
            SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_CONFIG);
            return SRNOS_BMI088_ERR_CONFIG;
        }

        s_tx[0] = SRNOS_BMI088_TEMP_M | 0x80u;
        s_tx[1] = 0xFFu;
        s_tx[2] = 0xFFu;
        s_tx[3] = 0xFFu;

        ret = SRNOS_SPITransferAsync(&s_acc_dev,
                                     s_tx,
                                     s_rx,
                                     4u,
                                     SRNOS_BMI088SPICb,
                                     NULL);
    }
    else
    {
        s_kick_pending = 0u;
        return SRNOS_BMI088_OK;
    }

    if (ret == SRNOS_SPI_OK)
    {
        s_kick_pending = 0u;
        s_last_status = SRNOS_BMI088_BUSY;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
        return SRNOS_BMI088_OK;
    }
    if (ret == SRNOS_SPI_BUSY)
    {
        SRNOS_BMI088DbgSync(SRNOS_BMI088_BUSY);
        return SRNOS_BMI088_BUSY;
    }

    SRNOS_LOGE_SRC("BMI088", "kick: SPI async start fail=%d", (int)ret);
    s_sm_state = SRNOS_BMI088_SM_IDLE;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_status = SRNOS_BMI088_ERR_SPI;
    s_spi_error_count++;
    SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
    return SRNOS_BMI088_ERR_SPI;
}

/* ==== SPI callback：由 SRNOS_SPI 回调线程推进 state machine ========= */

static void SRNOS_BMI088SPICb(SRNOS_SPIDevice *dev, void *user_ctx, SRNOS_SPIStatus status)
{
    (void)user_ctx;

    if (status != SRNOS_SPI_OK)
    {
        SRNOS_LOGE("SPI cb error status=%d, sm=%d", (int)status, (int)s_sm_state);
        s_sm_state     = SRNOS_BMI088_SM_IDLE;
        s_sample_ready = 0;
        s_kick_pending = 0;
        s_sample_start_tick = 0u;
        s_last_status = SRNOS_BMI088_ERR_SPI;
        s_spi_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        return;
    }

    switch (s_sm_state)
    {
    case SRNOS_BMI088_SM_ACC_READ:
    {
        // SRNOS_LOGD("cb: ACC_READ");

        uint8_t *p = &s_rx[2];
        s_raw_acc[0] = SRNOS_BMI088MakeS16(p[0], p[1]);
        s_raw_acc[1] = SRNOS_BMI088MakeS16(p[2], p[3]);
        s_raw_acc[2] = SRNOS_BMI088MakeS16(p[4], p[5]);

        s_sm_state = SRNOS_BMI088_SM_GYRO_READ;
        s_kick_pending = 1u;
    }
    break;

    case SRNOS_BMI088_SM_GYRO_READ:
    {
        // SRNOS_LOGD("cb: GYRO_READ");

        // s_rx[0] = dummy（发命令时回读的）
        // s_rx[1..8] = 等价于老 blocking 里的 buf[0..7]
        uint8_t *p = &s_rx[1];  // p[0] = CHIP_ID, p[1] = STATUS, p[2] = Gx_L, ...

        if (p[0] != SRNOS_BMI088_GYRO_CHIP_ID_VALUE)
        {
            SRNOS_LOGW("GYRO_READ: no valid CHIP_ID in rx: %02X %02X %02X",
                 s_rx[0], s_rx[1], s_rx[2]);
            s_sm_state = SRNOS_BMI088_SM_IDLE;
            s_sample_ready = 0u;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = SRNOS_BMI088_ERR_SPI;
            s_spi_error_count++;
            SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
            return;
        }

        // 对齐老代码：Gx = make_s16(buf[2], buf[3]) 等价于 p[2], p[3]
        s_raw_gyro[0] = SRNOS_BMI088MakeS16(p[2], p[3]);
        s_raw_gyro[1] = SRNOS_BMI088MakeS16(p[4], p[5]);
        s_raw_gyro[2] = SRNOS_BMI088MakeS16(p[6], p[7]);

        s_sm_state = SRNOS_BMI088_SM_TEMP_READ;
        s_kick_pending = 1u;
    }
    break;

    case SRNOS_BMI088_SM_TEMP_READ:
    {
        // SRNOS_LOGD("cb: TEMP_READ -> sample_ready=1");

        // 和 blocking 完全对齐：buf[0]=s_rx[2], buf[1]=s_rx[3]
        uint8_t hi = s_rx[2];
        uint8_t lo = s_rx[3];

        int16_t raw = (int16_t)(((int16_t)hi << 3) | (lo >> 5));
        if (raw > 1023)
            raw -= 2048;

        s_raw_temp     = raw;
        s_acquisition_end = SRNOS_TimeGetUptimeUs();
        s_sample_ready = 1u;
        s_sm_state     = SRNOS_BMI088_SM_IDLE;
        s_kick_pending = 0u;
        s_sample_start_tick = 0u;
        s_last_sample_tick = HAL_GetTick();
        s_sample_seq++;
        s_last_status = SRNOS_BMI088_OK;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_OK);
        if(s_completion) s_completion(s_completion_user);
    }
    break;

    default:
        SRNOS_LOGE("cb: unexpected sm=%d", (int)s_sm_state);
        s_sm_state = SRNOS_BMI088_SM_IDLE;
        s_kick_pending = 0u;
        s_sample_start_tick = 0u;
        s_last_status = SRNOS_BMI088_ERR_SPI;
        s_spi_error_count++;
        SRNOS_BMI088DbgSync(SRNOS_BMI088_ERR_SPI);
        break;
    }

    if (s_kick_pending != 0u)
    {
        (void)SRNOS_BMI088KickNext();
    }
}

void SRNOS_BMI088GetSPIStatistics(SRNOS_SPIStatistics *accel,SRNOS_SPIStatistics *gyro) {
    SRNOS_SPIGetStatistics(&s_acc_dev,accel); SRNOS_SPIGetStatistics(&s_gyro_dev,gyro);
}
