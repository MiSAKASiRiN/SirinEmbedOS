#include "Devices/ws2812_hardware.h"
#include "Devices/WS2812/ws2812_internal.h"
#include "stm32h7xx_hal_dma.h"

#include "SirinEmbedOS/Core/Memory/MemorySections.h"

static DMA_HandleTypeDef s_dma;
static volatile uint8_t  s_busy = 0;

static uint16_t  s_psc = 0, s_arr = 0;
static uint32_t  s_cnt_freq = 0;

#define SRNOS_WS2812_BUF_LEN   (SRNOS_WS2812_LED_COUNT * SRNOS_WS2812_BITS_PER_LED + SRNOS_WS2812_RESET_SLOTS)

static uint16_t s_buf_front[SRNOS_WS2812_BUF_LEN] SRNOS_DMA_NC_BUF_BSS;
static uint16_t s_buf_back [SRNOS_WS2812_BUF_LEN] SRNOS_DMA_NC_BUF_BSS;

/* duty（计数） */
static uint16_t s_duty_t0h = 0;
static uint16_t s_duty_t1h = 0;


static uint8_t  s_brightness = 255;
static SRNOS_WS2812Hooks s_hooks = {0};

/* ================== tools ================== */

static void _ws_stop_hw(void)
{
    SRNOS_WS2812_TIM->DIER &= ~TIM_DIER_UDE;
    SRNOS_WS2812_TIM->CR1  &= ~TIM_CR1_CEN;
}

static void _compute_tim_params(uint32_t tim_clk_hz, uint32_t target_bit_hz,
                                uint16_t *psc_out, uint16_t *arr_out, uint32_t *cnt_freq_out)
{
    const uint32_t target_counts = 115u;
    uint32_t presc = ((uint64_t)tim_clk_hz + (uint64_t)target_bit_hz*target_counts/2) /
                     ((uint64_t)target_bit_hz*target_counts);
    if (presc == 0) presc = 1;
    presc -= 1;
    if (presc > 0xFFFFu) presc = 0xFFFFu;

    uint32_t cnt_freq = tim_clk_hz / (presc + 1u);
    uint32_t arr = ((uint64_t)cnt_freq + target_bit_hz/2) / target_bit_hz;
    if (arr == 0) arr = 1;
    arr -= 1;
    if (arr > 0xFFFFu) arr = 0xFFFFu;

    *psc_out = (uint16_t)presc;
    *arr_out = (uint16_t)arr;
    *cnt_freq_out = cnt_freq;
}

static uint16_t _ns_to_counts(uint32_t cnt_freq, uint32_t ns)
{
    uint64_t num = (uint64_t)cnt_freq * (uint64_t)ns + 500000000ull;
    return (uint16_t)(num / 1000000000ull);
}

static void _gpio_port_clk_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void _gpio_init(void)
{
    _gpio_port_clk_enable(SRNOS_WS2812_GPIO_PORT);

    GPIO_InitTypeDef gi = {0};
    gi.Pin       = SRNOS_WS2812_GPIO_PIN;
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = SRNOS_WS2812_GPIO_AF;
    HAL_GPIO_Init(SRNOS_WS2812_GPIO_PORT, &gi);
}

static void _tim_init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    SRNOS_WS2812_TIM->CR1  = TIM_CR1_ARPE;  // 仅开 ARPE
    SRNOS_WS2812_TIM->CR2  = 0;
    SRNOS_WS2812_TIM->SMCR = 0;

    SRNOS_WS2812_TIM->PSC = s_psc;
    SRNOS_WS2812_TIM->ARR = s_arr;

    SRNOS_WS2812_TIM->CCMR2 &= ~(TIM_CCMR2_OC3M_Msk | TIM_CCMR2_CC3S_Msk);
    SRNOS_WS2812_TIM->CCMR2 |= (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE; // PWM1 + preload

    SRNOS_WS2812_TIM->CCER &= ~(TIM_CCER_CC3P | TIM_CCER_CC3NP);
    SRNOS_WS2812_TIM->CCER |= TIM_CCER_CC3E;

    SRNOS_WS2812_TIM->CCR3 = 0;
    SRNOS_WS2812_TIM->BDTR |= TIM_BDTR_MOE;

    SRNOS_WS2812_TIM->EGR = TIM_EGR_UG;
}

static void _dmamux_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
#if defined(__HAL_RCC_DMAMUX1_CLK_ENABLE)
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
#elif defined(__HAL_RCC_DMAMUX_CLK_ENABLE)
    __HAL_RCC_DMAMUX_CLK_ENABLE();
#endif

    s_dma.Instance                 = SRNOS_WS2812_DMA_STREAM;
    s_dma.Init.Request             = SRNOS_WS2812_DMA_REQUEST;
    s_dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_dma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_dma.Init.MemInc              = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    s_dma.Init.Mode                = DMA_NORMAL;
    s_dma.Init.Priority            = DMA_PRIORITY_LOW;
    s_dma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    (void)HAL_DMA_Abort(&s_dma);
    HAL_DMA_Init(&s_dma);

#if defined(HAL_DMA_RegisterCallback)
    __HAL_DMA_DISABLE_IT(&s_dma, DMA_IT_HT);
    HAL_DMA_RegisterCallback(&s_dma, HAL_DMA_XFER_CPLT_CB_ID,  NULL);
    HAL_DMA_RegisterCallback(&s_dma, HAL_DMA_XFER_ERROR_CB_ID, NULL);
#endif

    HAL_NVIC_SetPriority(SRNOS_WS2812DMAIRQn, SRNOS_WS2812_DMA_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(SRNOS_WS2812DMAIRQn);
}

static inline uint8_t _apply_brightness(uint8_t v)
{
    if (s_brightness == 255) return v;
    // 线性缩放（可换gamma）
    return (uint8_t)((v * (uint16_t)s_brightness) / 255u);
}

static void _encode_led_to(uint16_t *buf, uint16_t led_index, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t base = (uint32_t)led_index * SRNOS_WS2812_BITS_PER_LED;
    uint8_t grb[3] = { _apply_brightness(g), _apply_brightness(r), _apply_brightness(b) };

    for (uint32_t c = 0; c < 3; c++) {
        for (int bit = 7; bit >= 0; bit--) {
            buf[base++] = ((grb[c] >> bit) & 0x1) ? s_duty_t1h : s_duty_t0h;
        }
    }
}

static void _append_reset_slots_to(uint16_t *buf)
{
    for (uint32_t i = SRNOS_WS2812_LED_COUNT * SRNOS_WS2812_BITS_PER_LED; i < SRNOS_WS2812_BUF_LEN; i++) {
        buf[i] = 0;
    }
}

/* ================== APIs ================== */

void SRNOS_WS2812Init(uint32_t tim_clk_hz)
{
    _compute_tim_params(tim_clk_hz, SRNOS_WS2812_BITRATE_HZ, &s_psc, &s_arr, &s_cnt_freq);

    uint32_t period_counts = (uint32_t)s_arr + 1u;
    uint16_t t0 = _ns_to_counts(s_cnt_freq, SRNOS_WS2812_T0H_NS);
    uint16_t t1 = _ns_to_counts(s_cnt_freq, SRNOS_WS2812_T1H_NS);
    if (t0 >= period_counts) t0 = (uint16_t)(period_counts - 1u);
    if (t1 >= period_counts) t1 = (uint16_t)(period_counts - 1u);
    s_duty_t0h = t0;
    s_duty_t1h = t1;

    _gpio_init();
    _tim_init();
    _dmamux_dma_init();

    // Clean both buffer
    for (uint16_t i = 0; i < SRNOS_WS2812_LED_COUNT; ++i) {
        _encode_led_to(s_buf_back , i, 0,0,0);
        _encode_led_to(s_buf_front, i, 0,0,0);
    }
    _append_reset_slots_to(s_buf_back);
    _append_reset_slots_to(s_buf_front);

    SRNOS_WS2812_TIM->DIER &= ~TIM_DIER_UDE;
}

void SRNOS_WS2812SetGlobalBrightness(uint8_t scale)
{
    s_brightness = scale;
}

/**************************
 * @brief Set single pixel in "back" buffer (non-blocking)
 * @param index: index of LED, 0-based
 * @param r: Red (0-255)
 * @param g: Green (0-255)
 * @param b: Blue (0-255)
 **************************/
void SRNOS_WS2812SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= SRNOS_WS2812_LED_COUNT) return;
    _encode_led_to(s_buf_back, index, r, g, b);
}

/**************************
 * @brief Quickly fill all LEDs in "back" buffer (non-blocking)
 * @param r: Red (0-255)
 * @param g: Green (0-255)
 * @param b: Blue (0-255)
 **************************/
void SRNOS_WS2812SetAll(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < SRNOS_WS2812_LED_COUNT; i++) {
        _encode_led_to(s_buf_back, i, r, g, b);
    }
    _append_reset_slots_to(s_buf_back);
}

SRNOS_WS2812Result SRNOS_WS2812CommitStartAsync(const SRNOS_WS2812Hooks *hooks)
{
    if (s_busy) return SRNOS_WS2812_BUSY;
    s_busy = 1;

    // record hooks
    if (hooks) s_hooks = *hooks;
    else       s_hooks = (SRNOS_WS2812Hooks){0};

    // exchange front/back
    for (uint32_t i = 0; i < SRNOS_WS2812_BUF_LEN; ++i) {
        s_buf_front[i] = s_buf_back[i];
    }

    uint32_t periph_addr = (uint32_t)&(SRNOS_WS2812_TIM->CCR3);

    __HAL_DMA_CLEAR_FLAG(&s_dma, DMA_FLAG_TCIF1_5);
    HAL_StatusTypeDef st = HAL_DMA_Start_IT(&s_dma,
                                            (uint32_t)s_buf_front,
                                            periph_addr,
                                            SRNOS_WS2812_BUF_LEN);
    if (st != HAL_OK) {
        s_busy = 0;
        return st == HAL_BUSY ? SRNOS_WS2812_BUSY :
               st == HAL_TIMEOUT ? SRNOS_WS2812_TIMEOUT : SRNOS_WS2812_ERROR;
    }

    SRNOS_WS2812_TIM->DIER |= TIM_DIER_UDE;
    SRNOS_WS2812_TIM->CR1  |= TIM_CR1_CEN;

    return SRNOS_WS2812_OK;
}

uint8_t SRNOS_WS2812IsBusy(void) { return s_busy; }

uint16_t *SRNOS_WS2812AcquireDmaScratch(uint32_t min_words, uint32_t *capacity_words)
{
    const uint32_t capacity = (uint32_t)SRNOS_WS2812_BUF_LEN;
    if (capacity_words) {
        *capacity_words = capacity;
    }
    if (min_words > capacity) {
        return NULL;
    }

    _ws_stop_hw();
    DMA_Stream_TypeDef *stream = (DMA_Stream_TypeDef *)SRNOS_WS2812_DMA_STREAM;
    stream->CR &= ~DMA_SxCR_EN;
    uint32_t spins = 1000000u;
    while (((stream->CR & DMA_SxCR_EN) != 0u) && (spins-- > 0u)) {
        __NOP();
    }
    s_busy = 0;
    return s_buf_front;
}

void SRNOS_WS2812WaitIdle(void)
{
    if (!s_busy) return;
    uint32_t ndtr_last = __HAL_DMA_GET_COUNTER(&s_dma);
    uint32_t watchdog  = 1000000u;

    while (watchdog--) {
        uint32_t ndtr = __HAL_DMA_GET_COUNTER(&s_dma);
        if (ndtr == 0u) break;
        if (ndtr != ndtr_last) { ndtr_last = ndtr; watchdog = 1000000u; }
        __NOP();
    }

    for (volatile int i = 0; i < 512; ++i) { __NOP(); }
    _ws_stop_hw();
    s_busy = 0;
}

/* ================== IRQ ================== */

void SRNOS_WS2812DMAIRQHandler(void)
{
    // Read and clear flags
    HAL_DMA_IRQHandler(&s_dma);

    // Decide if done
    if (__HAL_DMA_GET_COUNTER(&s_dma) == 0) {
        _ws_stop_hw();
        s_busy = 0;

        // Callback
        if (s_hooks.on_tx_done) s_hooks.on_tx_done(s_hooks.user);
        if (s_hooks.notify_from_isr) s_hooks.notify_from_isr(s_hooks.notify_arg);
        return;
    }

    // Error handling
    if (__HAL_DMA_GET_FLAG(&s_dma, DMA_FLAG_TEIF1_5)) {
        _ws_stop_hw();
        s_busy = 0;
        if (s_hooks.on_tx_error) s_hooks.on_tx_error(s_hooks.user);
        if (s_hooks.notify_from_isr) s_hooks.notify_from_isr(s_hooks.notify_arg);
        return;
    }
}
