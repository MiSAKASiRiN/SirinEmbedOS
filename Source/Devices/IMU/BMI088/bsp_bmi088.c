#include "SirinEmbedOS/Devices/IMU/BMI088/bsp_bmi088.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include <string.h>
#include <math.h>
#include "cmsis_os2.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"

/* ==== 灵敏度常量兜底：优先用你原来头文件里的 ===== */
#ifndef SRN_BMI088_ACCEL_SEN
  #if defined(BMI088_ACCEL_3G_SEN)
    #define SRN_BMI088_ACCEL_SEN  BMI088_ACCEL_3G_SEN
  #elif defined(BMI088_ACCEL_SEN)
    #define SRN_BMI088_ACCEL_SEN  BMI088_ACCEL_SEN
  #else
    #error "bsp_bmi088: no accel sensitivity macro configured."
  #endif
#endif

#ifndef SRN_BMI088_GYRO_SEN
  #if defined(BMI088_GYRO_2000_SEN)
    #define SRN_BMI088_GYRO_SEN   BMI088_GYRO_2000_SEN
  #else
    #error "bsp_bmi088: no gyro sensitivity macro configured."
  #endif
#endif

/* 延时兜底 */
#ifndef BMI088_LONG_DELAY_TIME
#define BMI088_LONG_DELAY_TIME   200u
#endif

/* 温度公式兜底：T = 23 + raw * 0.125 */
#ifndef BMI088_TEMP_FACTOR
#define BMI088_TEMP_FACTOR   (0.125f)
#endif
#ifndef BMI088_TEMP_OFFSET
#define BMI088_TEMP_OFFSET   (23.0f)
#endif

#define BMI088_SPI_TIMEOUT_MS   (5u)
#define BMI088_SAMPLE_TIMEOUT_MS (5u)

/* ==== SPI 设备 & DMA buffer ===================================== */

static bsp_spi_device_t s_acc_dev;
static bsp_spi_device_t s_gyro_dev;
static uint64_t s_acquisition_start, s_acquisition_end;

#define BMI088_SPI_MAX_XFER   (64u)

static uint8_t s_tx[BMI088_SPI_MAX_XFER];

static uint8_t s_rx[BMI088_SPI_MAX_XFER];

OMX_DEBUG_WATCH_BSS volatile omx_dbg_bmi088_t g_omx_dbg_bmi088;

/* ==== 帮助函数 =================================================== */

static inline int16_t bmi088_make_s16(uint8_t lo, uint8_t hi)
{
    return (int16_t)((((uint16_t)hi) << 8) | (uint16_t)lo);
}

static void bmi088_delay_ms(uint32_t ms)
{
    if (osKernelGetState()==osKernelRunning) osDelay((ms*osKernelGetTickFreq()+999)/1000);
    else HAL_Delay(ms);
}

/* ==== SPI helpers: blocking 版本（用于初始化寄存器） = */

static uint8_t acc_write(uint8_t reg, uint8_t data)
{
    s_tx[0] = reg & 0x7Fu;
    s_tx[1] = data;

    return (bsp_spi_transfer_blocking(&s_acc_dev,
                                      s_tx,
                                      s_rx,
                                      2u,
                                      BMI088_SPI_TIMEOUT_MS) == BSP_SPI_OK) ? 0u : 1u;
}

static uint8_t acc_read1(uint8_t reg, uint8_t *data)
{
    s_tx[0] = reg | 0x80u;
    s_tx[1] = 0xFFu;
    s_tx[2] = 0xFFu;

    if (bsp_spi_transfer_blocking(&s_acc_dev,
                                  s_tx,
                                  s_rx,
                                  3u,
                                  BMI088_SPI_TIMEOUT_MS) != BSP_SPI_OK)
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

    return (bsp_spi_transfer_blocking(&s_gyro_dev,
                                      s_tx,
                                      s_rx,
                                      2u,
                                      BMI088_SPI_TIMEOUT_MS) == BSP_SPI_OK) ? 0u : 1u;
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

    if (bsp_spi_transfer_blocking(&s_gyro_dev,
                                  s_tx,
                                  s_rx,
                                  (size_t)(len + 1u),
                                  BMI088_SPI_TIMEOUT_MS) != BSP_SPI_OK)
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
        return BSP_BMI088_ERR_SPI;
    }
    if ((actual & mask) != (expected & mask))
    {
        LOGE("BMI088 ACC reg 0x%02X verify failed: got=0x%02X expect=0x%02X mask=0x%02X",
             reg, actual, expected, mask);
        g_omx_dbg_bmi088.config_error_count++;
        return BSP_BMI088_ERR_CONFIG;
    }
    return BSP_BMI088_OK;
}

static uint8_t gyro_verify(uint8_t reg, uint8_t expected, uint8_t mask)
{
    uint8_t actual = 0u;
    if (gyro_readN(reg, &actual, 1u) != 0u)
    {
        return BSP_BMI088_ERR_SPI;
    }
    if ((actual & mask) != (expected & mask))
    {
        LOGE("BMI088 GYRO reg 0x%02X verify failed: got=0x%02X expect=0x%02X mask=0x%02X",
             reg, actual, expected, mask);
        g_omx_dbg_bmi088.config_error_count++;
        return BSP_BMI088_ERR_CONFIG;
    }
    return BSP_BMI088_OK;
}

/* ==== 加速度计 / 陀螺仪 Init（blocking） ======================= */

static uint8_t bmi088_accel_init(void)
{
    uint8_t id = 0;

    /* check chip id */
    (void)acc_read1(BMI088_ACC_CHIP_ID, &id);
    bmi088_delay_ms(1);
    (void)acc_read1(BMI088_ACC_CHIP_ID, &id);
    bmi088_delay_ms(1);

    /* soft reset */
    (void)acc_write(BMI088_ACC_SOFTRESET, BMI088_ACC_SOFTRESET_VALUE);
    bmi088_delay_ms(BMI088_LONG_DELAY_TIME);

    /* re-check chip id */
    (void)acc_read1(BMI088_ACC_CHIP_ID, &id);
    bmi088_delay_ms(1);
    (void)acc_read1(BMI088_ACC_CHIP_ID, &id);
    bmi088_delay_ms(1);

    if (id != BMI088_ACC_CHIP_ID_VALUE)
    {
        return BSP_BMI088_ERR_NO_SENSOR;
    }

    /* power config */
    if (acc_write(BMI088_ACC_PWR_CONF, BMI088_ACC_PWR_ACTIVE_MODE) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    if (acc_write(BMI088_ACC_PWR_CTRL, BMI088_ACC_ENABLE_ACC_ON) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    /* ODR / mode / filter */
    const uint8_t acc_conf = (uint8_t)(BMI088_ACC_NORMAL | BMI088_ACC_1600_HZ | BMI088_ACC_CONF_MUST_Set);
    if (acc_write(BMI088_ACC_CONF, acc_conf) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

#if defined(BMI088_ACC_RANGE_3G)
    const uint8_t acc_range = BMI088_ACC_RANGE_3G;
#elif defined(BMI088_ACC_RANGE_6G)
    const uint8_t acc_range = BMI088_ACC_RANGE_6G;
#else
    const uint8_t acc_range = 0x00u;
#endif
    if (acc_write(BMI088_ACC_RANGE, acc_range) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    uint8_t verify_st = acc_verify(BMI088_ACC_CONF, acc_conf, 0xFFu);
    if (verify_st != BSP_BMI088_OK)
        return verify_st;
    verify_st = acc_verify(BMI088_ACC_RANGE, acc_range, 0x03u);
    if (verify_st != BSP_BMI088_OK)
        return verify_st;

#if defined(BMI088_INT1_IO_CTRL)
    (void)acc_write(
        BMI088_INT1_IO_CTRL,
        (uint8_t)(BMI088_ACC_INT1_IO_ENABLE |
                  BMI088_ACC_INT1_GPIO_PP   |
                  BMI088_ACC_INT1_GPIO_LOW)
    );
    bmi088_delay_ms(1);
#endif

#if defined(BMI088_INT_MAP_DATA)
    (void)acc_write(
        BMI088_INT_MAP_DATA,
        BMI088_ACC_INT1_DRDY_INTERRUPT
    );
    bmi088_delay_ms(1);
#endif

    return BSP_BMI088_OK;
}

static uint8_t bmi088_gyro_init(void)
{
    uint8_t id = 0;

    /* check chip id */
    (void)gyro_readN(BMI088_GYRO_CHIP_ID, &id, 1);
    bmi088_delay_ms(1);
    (void)gyro_readN(BMI088_GYRO_CHIP_ID, &id, 1);
    bmi088_delay_ms(1);

    /* soft reset */
    (void)gyro_write(BMI088_GYRO_SOFTRESET, BMI088_GYRO_SOFTRESET_VALUE);
    bmi088_delay_ms(BMI088_LONG_DELAY_TIME);

    /* re-check chip id */
    (void)gyro_readN(BMI088_GYRO_CHIP_ID, &id, 1);
    bmi088_delay_ms(1);
    (void)gyro_readN(BMI088_GYRO_CHIP_ID, &id, 1);
    bmi088_delay_ms(1);

    if (id != BMI088_GYRO_CHIP_ID_VALUE)
    {
        return BSP_BMI088_ERR_NO_SENSOR;
    }

    if (gyro_write(BMI088_GYRO_RANGE, BMI088_GYRO_2000) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

#if defined(BMI088_GYRO_2000_230_HZ)
    const uint8_t gyro_bandwidth = (uint8_t)(BMI088_GYRO_2000_230_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set);
#else
    const uint8_t gyro_bandwidth = (uint8_t)(BMI088_GYRO_1000_116_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set);
#endif
    if (gyro_write(BMI088_GYRO_BANDWIDTH, gyro_bandwidth) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    if (gyro_write(BMI088_GYRO_LPM1, BMI088_GYRO_NORMAL_MODE) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    if (gyro_write(BMI088_GYRO_CTRL, BMI088_DRDY_ON) != 0u)
        return BSP_BMI088_ERR_SPI;
    bmi088_delay_ms(1);

    uint8_t verify_st = gyro_verify(BMI088_GYRO_RANGE, BMI088_GYRO_2000, 0x07u);
    if (verify_st != BSP_BMI088_OK)
        return verify_st;
    verify_st = gyro_verify(BMI088_GYRO_BANDWIDTH, gyro_bandwidth, 0x07u);
    if (verify_st != BSP_BMI088_OK)
        return verify_st;

#if defined(BMI088_GYRO_INT3_INT4_IO_CONF)
    (void)gyro_write(
        BMI088_GYRO_INT3_INT4_IO_CONF,
        (uint8_t)(BMI088_GYRO_INT3_GPIO_PP | BMI088_GYRO_INT3_GPIO_LOW)
    );
    bmi088_delay_ms(1);
#endif

#if defined(BMI088_GYRO_INT3_INT4_IO_MAP)
    (void)gyro_write(
        BMI088_GYRO_INT3_INT4_IO_MAP,
        BMI088_GYRO_DRDY_IO_INT3
    );
    bmi088_delay_ms(1);
#endif

    return BSP_BMI088_OK;
}

/* ==== 异步采样状态机 ============================================ */

/* raw 数据由 SPI dispatcher 填写，BMI task 完成 SI 转换 */
static bsp_bmi088_completion_fn s_completion;
static void *s_completion_user;
static volatile int16_t s_raw_acc[3]  = {0};
static volatile int16_t s_raw_gyro[3] = {0};
static volatile int16_t s_raw_temp    = 0;

typedef enum
{
    BMI088_SM_IDLE = 0,
    BMI088_SM_ACC_READ,
    BMI088_SM_GYRO_READ,
    BMI088_SM_TEMP_READ
} bmi088_sm_state_t;

static volatile bmi088_sm_state_t s_sm_state = BMI088_SM_IDLE;
static volatile uint8_t           s_sample_ready = 0;
static volatile uint8_t           s_kick_pending = 0;
static volatile uint32_t          s_sample_start_tick = 0u;
static volatile uint32_t          s_last_sample_tick = 0u;
static volatile uint32_t          s_sample_seq = 0u;
static volatile uint32_t          s_stale_count = 0u;
static volatile uint32_t          s_spi_error_count = 0u;
static volatile bsp_bmi088_status_t s_last_status = BSP_BMI088_OK;

static void bmi088_spi_cb(bsp_spi_device_t *dev, void *user_ctx, bsp_spi_status_t status);
static bsp_bmi088_status_t bmi088_kick_next(void);
static bsp_bmi088_status_t bmi088_check_stale(void);

static void bmi088_dbg_sync(bsp_bmi088_status_t status)
{
    g_omx_dbg_bmi088.phase = (uint32_t)s_sm_state;
    g_omx_dbg_bmi088.sample_start_tick = s_sample_start_tick;
    g_omx_dbg_bmi088.last_sample_tick = s_last_sample_tick;
    g_omx_dbg_bmi088.sample_seq = s_sample_seq;
    g_omx_dbg_bmi088.stale_count = s_stale_count;
    g_omx_dbg_bmi088.spi_error_count = s_spi_error_count;
    omx_dbg_end(&g_omx_dbg_bmi088.h, HAL_GetTick(), (uint32_t)status);
}

/* ==== 公共接口：Init ============================================ */

bsp_bmi088_status_t BSP_BMI088_Init(bsp_spi_bus_id_t bus_id,
                                    GPIO_TypeDef *acc_cs_port,  uint16_t acc_cs_pin,
                                    GPIO_TypeDef *gyro_cs_port, uint16_t gyro_cs_pin,
                                    bsp_bmi088_completion_fn completion, void *user)
{
    s_completion=completion;s_completion_user=user;
    omx_dbg_zero(&g_omx_dbg_bmi088, sizeof(g_omx_dbg_bmi088));
    omx_dbg_init(&g_omx_dbg_bmi088.h,
                 OMX_DBG_MODULE_BMI088,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_bmi088));

    if (bsp_spi_device_init(&s_acc_dev, bus_id, acc_cs_port, acc_cs_pin) != BSP_SPI_OK)
    {
        g_omx_dbg_bmi088.spi_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        return BSP_BMI088_ERR_SPI;
    }
    if (bsp_spi_device_init(&s_gyro_dev, bus_id, gyro_cs_port, gyro_cs_pin) != BSP_SPI_OK)
    {
        g_omx_dbg_bmi088.spi_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        return BSP_BMI088_ERR_SPI;
    }

    s_acc_dev.imu_priority=1;
    s_gyro_dev.imu_priority=1;
    uint8_t err;

    err = bmi088_accel_init();
    if (err != BSP_BMI088_OK)
    {
        if (err == BSP_BMI088_ERR_SPI)
        {
            g_omx_dbg_bmi088.spi_error_count++;
        }
        bmi088_dbg_sync((bsp_bmi088_status_t)err);
        return err;
    }

    err = bmi088_gyro_init();
    if (err != BSP_BMI088_OK)
    {
        if (err == BSP_BMI088_ERR_SPI)
        {
            g_omx_dbg_bmi088.spi_error_count++;
        }
        bmi088_dbg_sync((bsp_bmi088_status_t)err);
        return err;
    }

    s_sm_state     = BMI088_SM_IDLE;
    s_sample_ready = 0;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_sample_tick = HAL_GetTick();
    s_sample_seq = 0u;
    s_stale_count = 0u;
    s_spi_error_count = 0u;
    s_last_status = BSP_BMI088_OK;
    bmi088_dbg_sync(BSP_BMI088_OK);

    return BSP_BMI088_OK;
}

/* ==== 公共接口：异步采样 ======================================== */

bsp_bmi088_status_t BSP_BMI088_BeginSample(void)
{
    if (s_sm_state != BMI088_SM_IDLE)
    {
        bsp_bmi088_status_t stale_st = bmi088_check_stale();
        if (stale_st != BSP_BMI088_OK)
        {
            bmi088_dbg_sync(stale_st);
            return stale_st;
        }
        bmi088_dbg_sync(BSP_BMI088_BUSY);
        return BSP_BMI088_BUSY;
    }

    if ((size_t)(6 + 2u) > sizeof(s_tx))
    {
        LOGE_SRC("BMI088", "BeginSample: ACC buf too small");
        g_omx_dbg_bmi088.config_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_CONFIG);
        return BSP_BMI088_ERR_CONFIG;
    }

    s_tx[0] = BMI088_ACCEL_XOUT_L | 0x80u;
    s_tx[1] = 0xFFu;
    for (uint8_t i = 0; i < 6u; ++i)
    {
        s_tx[2u + i] = 0xFFu;
    }

    s_sample_ready = 0;
    s_kick_pending = 0;
    s_acquisition_start = DWT_GetUptimeUs();
    s_sample_start_tick = HAL_GetTick();
    s_last_status = BSP_BMI088_BUSY;
    s_sm_state     = BMI088_SM_ACC_READ;

    bsp_spi_status_t ret = bsp_spi_transfer_async_wait(&s_acc_dev,
                                                       s_tx,
                                                       s_rx,
                                                       8u,
                                                       bmi088_spi_cb,
                                                       NULL,
                                                       1u);
    if (ret != BSP_SPI_OK)
    {
        s_sm_state = BMI088_SM_IDLE;
        s_sample_start_tick = 0u;
        if (ret == BSP_SPI_BUSY)
        {
            bmi088_dbg_sync(BSP_BMI088_BUSY);
            return BSP_BMI088_BUSY;
        }
        LOGE_SRC("BMI088", "BeginSample: SPI async start fail=%d", (int)ret);
        s_last_status = BSP_BMI088_ERR_SPI;
        s_spi_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        return BSP_BMI088_ERR_SPI;
    }

    bmi088_dbg_sync(BSP_BMI088_BUSY);
    return BSP_BMI088_OK;
}

bsp_bmi088_status_t BSP_BMI088_GetSampleIfReady(bmi088_data_t *out)
{
    if (out == NULL)
    {
        g_omx_dbg_bmi088.config_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_CONFIG);
        return BSP_BMI088_ERR_CONFIG;
    }

    bsp_bmi088_status_t stale_st = bmi088_check_stale();
    if (stale_st != BSP_BMI088_OK)
    {
        bmi088_dbg_sync(stale_st);
        return stale_st;
    }

    bsp_bmi088_status_t kick_st = bmi088_kick_next();
    if (kick_st == BSP_BMI088_ERR_SPI || kick_st == BSP_BMI088_ERR_CONFIG)
    {
        bmi088_dbg_sync(kick_st);
        return kick_st;
    }

    if ((s_last_status == BSP_BMI088_ERR_SPI) &&
        (s_sm_state == BMI088_SM_IDLE) &&
        (s_sample_ready == 0u))
    {
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        return BSP_BMI088_ERR_SPI;
    }

    if (s_sample_ready == 0u)
    {
        bmi088_dbg_sync(BSP_BMI088_BUSY);
        return BSP_BMI088_BUSY;
    }

    /* raw -> float at task context */
    float ax = (float)s_raw_acc[0] * SRN_BMI088_ACCEL_SEN;
    float ay = (float)s_raw_acc[1] * SRN_BMI088_ACCEL_SEN;
    float az = (float)s_raw_acc[2] * SRN_BMI088_ACCEL_SEN;

    float gx = (float)s_raw_gyro[0] * SRN_BMI088_GYRO_SEN;
    float gy = (float)s_raw_gyro[1] * SRN_BMI088_GYRO_SEN;
    float gz = (float)s_raw_gyro[2] * SRN_BMI088_GYRO_SEN;

    float t;
    {
        int16_t raw = s_raw_temp;
        t = (float)raw * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;
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
    bmi088_dbg_sync(BSP_BMI088_OK);

    return BSP_BMI088_OK;
}

bsp_bmi088_status_t BSP_BMI088_ResetAsyncState(void)
{
    if ((s_acc_dev.bus == NULL) || (s_gyro_dev.bus == NULL))
    {
        g_omx_dbg_bmi088.config_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_CONFIG);
        return BSP_BMI088_ERR_CONFIG;
    }

    /* Queued and active requests both own s_rx until cancellation is acknowledged. */
    bsp_spi_status_t acc=bsp_spi_cancel(&s_acc_dev), gyro=bsp_spi_cancel(&s_gyro_dev);
    if(acc==BSP_SPI_BUSY || gyro==BSP_SPI_BUSY) return BSP_BMI088_BUSY;
    if(acc!=BSP_SPI_OK || gyro!=BSP_SPI_OK) return BSP_BMI088_ERR_SPI;

    s_raw_acc[0] = 0;
    s_raw_acc[1] = 0;
    s_raw_acc[2] = 0;
    s_raw_gyro[0] = 0;
    s_raw_gyro[1] = 0;
    s_raw_gyro[2] = 0;
    s_raw_temp = 0;

    s_sm_state = BMI088_SM_IDLE;
    s_sample_ready = 0u;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_status = BSP_BMI088_OK;
    bmi088_dbg_sync(BSP_BMI088_OK);

    return BSP_BMI088_OK;
}

void BSP_BMI088_GetHealth(bsp_bmi088_health_t *out)
{
    if (!out)
    {
        return;
    }

    out->phase = (bsp_bmi088_phase_t)s_sm_state;
    out->last_status = s_last_status;
    out->sample_start_tick = s_sample_start_tick;
    out->last_sample_tick = s_last_sample_tick;
    out->sample_seq = s_sample_seq;
    out->stale_count = s_stale_count;
    out->spi_error_count = s_spi_error_count;
    bmi088_dbg_sync(s_last_status);
}

static bsp_bmi088_status_t bmi088_check_stale(void)
{
    if (s_sm_state == BMI088_SM_IDLE)
    {
        return BSP_BMI088_OK;
    }

    uint32_t start = s_sample_start_tick;
    if (start == 0u)
    {
        return BSP_BMI088_OK;
    }

    uint32_t now = HAL_GetTick();
    if ((now - start) <= BMI088_SAMPLE_TIMEOUT_MS)
    {
        return BSP_BMI088_OK;
    }

    s_stale_count++;
    (void)BSP_BMI088_ResetAsyncState();
    s_last_status = BSP_BMI088_ERR_STALE;
    bmi088_dbg_sync(BSP_BMI088_ERR_STALE);
    return BSP_BMI088_ERR_STALE;
}

static bsp_bmi088_status_t bmi088_kick_next(void)
{
    if (s_kick_pending == 0u)
    {
        return BSP_BMI088_OK;
    }

    bsp_spi_status_t ret = BSP_SPI_OK;

    if (s_sm_state == BMI088_SM_GYRO_READ)
    {
        if ((size_t)(8 + 1u) > sizeof(s_tx))
        {
            LOGE_SRC("BMI088", "kick: gyro buf too small");
            s_sm_state = BMI088_SM_IDLE;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = BSP_BMI088_ERR_CONFIG;
            g_omx_dbg_bmi088.config_error_count++;
            bmi088_dbg_sync(BSP_BMI088_ERR_CONFIG);
            return BSP_BMI088_ERR_CONFIG;
        }

        s_tx[0] = BMI088_GYRO_CHIP_ID | 0x80u;
        for (uint8_t i = 0; i < 8u; ++i)
        {
            s_tx[1u + i] = 0xFFu;
        }

        ret = bsp_spi_transfer_async(&s_gyro_dev,
                                     s_tx,
                                     s_rx,
                                     9u,
                                     bmi088_spi_cb,
                                     NULL);
    }
    else if (s_sm_state == BMI088_SM_TEMP_READ)
    {
        if ((size_t)(2 + 2u) > sizeof(s_tx))
        {
            LOGE_SRC("BMI088", "kick: temp buf too small");
            s_sm_state = BMI088_SM_IDLE;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = BSP_BMI088_ERR_CONFIG;
            g_omx_dbg_bmi088.config_error_count++;
            bmi088_dbg_sync(BSP_BMI088_ERR_CONFIG);
            return BSP_BMI088_ERR_CONFIG;
        }

        s_tx[0] = BMI088_TEMP_M | 0x80u;
        s_tx[1] = 0xFFu;
        s_tx[2] = 0xFFu;
        s_tx[3] = 0xFFu;

        ret = bsp_spi_transfer_async(&s_acc_dev,
                                     s_tx,
                                     s_rx,
                                     4u,
                                     bmi088_spi_cb,
                                     NULL);
    }
    else
    {
        s_kick_pending = 0u;
        return BSP_BMI088_OK;
    }

    if (ret == BSP_SPI_OK)
    {
        s_kick_pending = 0u;
        s_last_status = BSP_BMI088_BUSY;
        bmi088_dbg_sync(BSP_BMI088_BUSY);
        return BSP_BMI088_OK;
    }
    if (ret == BSP_SPI_BUSY)
    {
        bmi088_dbg_sync(BSP_BMI088_BUSY);
        return BSP_BMI088_BUSY;
    }

    LOGE_SRC("BMI088", "kick: SPI async start fail=%d", (int)ret);
    s_sm_state = BMI088_SM_IDLE;
    s_kick_pending = 0u;
    s_sample_start_tick = 0u;
    s_last_status = BSP_BMI088_ERR_SPI;
    s_spi_error_count++;
    bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
    return BSP_BMI088_ERR_SPI;
}

/* ==== SPI callback：由 bsp_spi 回调线程推进 state machine ========= */

static void bmi088_spi_cb(bsp_spi_device_t *dev, void *user_ctx, bsp_spi_status_t status)
{
    (void)user_ctx;

    if (status != BSP_SPI_OK)
    {
        LOGE("SPI cb error status=%d, sm=%d", (int)status, (int)s_sm_state);
        s_sm_state     = BMI088_SM_IDLE;
        s_sample_ready = 0;
        s_kick_pending = 0;
        s_sample_start_tick = 0u;
        s_last_status = BSP_BMI088_ERR_SPI;
        s_spi_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        return;
    }

    switch (s_sm_state)
    {
    case BMI088_SM_ACC_READ:
    {
        // LOGD("cb: ACC_READ");

        uint8_t *p = &s_rx[2];
        s_raw_acc[0] = bmi088_make_s16(p[0], p[1]);
        s_raw_acc[1] = bmi088_make_s16(p[2], p[3]);
        s_raw_acc[2] = bmi088_make_s16(p[4], p[5]);

        s_sm_state = BMI088_SM_GYRO_READ;
        s_kick_pending = 1u;
    }
    break;

    case BMI088_SM_GYRO_READ:
    {
        // LOGD("cb: GYRO_READ");

        // s_rx[0] = dummy（发命令时回读的）
        // s_rx[1..8] = 等价于老 blocking 里的 buf[0..7]
        uint8_t *p = &s_rx[1];  // p[0] = CHIP_ID, p[1] = STATUS, p[2] = Gx_L, ...

        if (p[0] != BMI088_GYRO_CHIP_ID_VALUE)
        {
            LOGW("GYRO_READ: no valid CHIP_ID in rx: %02X %02X %02X",
                 s_rx[0], s_rx[1], s_rx[2]);
            s_sm_state = BMI088_SM_IDLE;
            s_sample_ready = 0u;
            s_kick_pending = 0u;
            s_sample_start_tick = 0u;
            s_last_status = BSP_BMI088_ERR_SPI;
            s_spi_error_count++;
            bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
            return;
        }

        // 对齐老代码：Gx = make_s16(buf[2], buf[3]) 等价于 p[2], p[3]
        s_raw_gyro[0] = bmi088_make_s16(p[2], p[3]);
        s_raw_gyro[1] = bmi088_make_s16(p[4], p[5]);
        s_raw_gyro[2] = bmi088_make_s16(p[6], p[7]);

        s_sm_state = BMI088_SM_TEMP_READ;
        s_kick_pending = 1u;
    }
    break;

    case BMI088_SM_TEMP_READ:
    {
        // LOGD("cb: TEMP_READ -> sample_ready=1");

        // 和 blocking 完全对齐：buf[0]=s_rx[2], buf[1]=s_rx[3]
        uint8_t hi = s_rx[2];
        uint8_t lo = s_rx[3];

        int16_t raw = (int16_t)(((int16_t)hi << 3) | (lo >> 5));
        if (raw > 1023)
            raw -= 2048;

        s_raw_temp     = raw;
        s_acquisition_end = DWT_GetUptimeUs();
        s_sample_ready = 1u;
        s_sm_state     = BMI088_SM_IDLE;
        s_kick_pending = 0u;
        s_sample_start_tick = 0u;
        s_last_sample_tick = HAL_GetTick();
        s_sample_seq++;
        s_last_status = BSP_BMI088_OK;
        bmi088_dbg_sync(BSP_BMI088_OK);
        if(s_completion) s_completion(s_completion_user);
    }
    break;

    default:
        LOGE("cb: unexpected sm=%d", (int)s_sm_state);
        s_sm_state = BMI088_SM_IDLE;
        s_kick_pending = 0u;
        s_sample_start_tick = 0u;
        s_last_status = BSP_BMI088_ERR_SPI;
        s_spi_error_count++;
        bmi088_dbg_sync(BSP_BMI088_ERR_SPI);
        break;
    }

    if (s_kick_pending != 0u)
    {
        (void)bmi088_kick_next();
    }
}

void BSP_BMI088_GetSPIStatistics(bsp_spi_statistics_t *accel,bsp_spi_statistics_t *gyro) {
    bsp_spi_get_statistics(&s_acc_dev,accel); bsp_spi_get_statistics(&s_gyro_dev,gyro);
}
