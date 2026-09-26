#include "SirinEmbedOS/Devices/WS2812/ws2812_critical.h"
#include "SirinEmbedOS/Devices/WS2812/bsp_ws2812.h"
#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#include "stm32h7xx_hal_dma.h"
#include <stdbool.h>

static uint8_t s_buf[WS2812CT_LED_COUNT*3u]; // GRB
static uint16_t s_arr, s_t0h, s_t1h;         // timer counts
#define WS2812CT_DMA_WORDS (WS2812CT_LED_COUNT * 24u + WS2812CT_RESET_SLOTS)
static DMA_HandleTypeDef s_dma;

static inline void _rcc_gpio_en(GPIO_TypeDef *p) {
    if (p==GPIOA) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOB) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOC) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOD) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIODEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOE) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOF) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOFEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOG) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOGEN; (void)RCC->AHB4ENR; }
    else if (p==GPIOH) { RCC->AHB4ENR |= RCC_AHB4ENR_GPIOHEN; (void)RCC->AHB4ENR; }
}

static inline void _gpio_to_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af) {
    uint32_t pos = pin*2u;
    port->MODER   = (port->MODER & ~(0x3u<<pos)) | (0x2u<<pos);   // AF
    port->OTYPER &= ~(1u<<pin);                                   // PP
    port->OSPEEDR = (port->OSPEEDR & ~(0x3u<<pos)) | (0x3u<<pos); // VeryHigh
    port->PUPDR  &= ~(0x3u<<pos);                                 // NoPull
    uint32_t idx=(pin<8)?0:1; uint32_t sh=((pin&7u)*4u);
    port->AFR[idx] = (port->AFR[idx] & ~(0xFu<<sh)) | ((af&0xFu)<<sh);
}

static inline uint32_t _ns_to_counts(uint32_t hz, uint32_t ns) {
    uint64_t num = (uint64_t)hz * (uint64_t)ns + 500000000ull;
    return (uint32_t)(num / 1000000000ull);
}

static void _compute_tim_params(uint32_t tim_clk_hz, uint32_t *psc, uint32_t *arr_p1, uint32_t *cnt_freq)
{
    const uint32_t target_counts = 115u;
    uint32_t presc = ((uint64_t)tim_clk_hz + (uint64_t)WS2812CT_BITRATE_HZ * target_counts / 2u) /
                     ((uint64_t)WS2812CT_BITRATE_HZ * target_counts);
    if (presc == 0u) presc = 1u;
    presc -= 1u;
    if (presc > 0xFFFFu) presc = 0xFFFFu;

    uint32_t cf = tim_clk_hz / (presc + 1u);
    uint32_t period = (cf + WS2812CT_BITRATE_HZ / 2u) / WS2812CT_BITRATE_HZ;
    if (period < 2u) period = 2u;
    if (period > 0x10000u) period = 0x10000u;

    *psc = presc;
    *arr_p1 = period;
    *cnt_freq = cf;
}

void ws2812ct_init(uint32_t tim_clk_hz)
{
    // 1) GPIO AF to TIM1_CH3
    _rcc_gpio_en(WS2812CT_GPIO_PORT);
    _gpio_to_af(WS2812CT_GPIO_PORT, WS2812CT_GPIO_PIN, WS2812CT_GPIO_AF);

    // 2) Enable TIM1 clock & basic PWM config on CH3
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN; (void)RCC->APB2ENR;

    uint32_t psc, arr_p1, cnt_freq;
    _compute_tim_params(tim_clk_hz, &psc, &arr_p1, &cnt_freq);
    s_arr = (uint16_t)(arr_p1 - 1u);

    // Duty counts for T0H/T1H
    uint32_t t0 = _ns_to_counts(cnt_freq, WS2812CT_T0H_NS);
    uint32_t t1 = _ns_to_counts(cnt_freq, WS2812CT_T1H_NS);
    if (t0>=arr_p1) t0 = arr_p1-1u; if (t1>=arr_p1) t1 = arr_p1-1u;
    s_t0h = (uint16_t)t0; s_t1h = (uint16_t)t1;

    // Reset timer
    WS2812CT_TIM->CR1 = 0; WS2812CT_TIM->CR2 = 0; WS2812CT_TIM->SMCR = 0;
    WS2812CT_TIM->DIER = 0;
    WS2812CT_TIM->PSC = (uint16_t)psc;
    WS2812CT_TIM->ARR = s_arr;            // period

    // CH3 PWM1 + preload
    WS2812CT_TIM->CCMR2 &= ~(TIM_CCMR2_OC3M_Msk | TIM_CCMR2_CC3S_Msk);
    WS2812CT_TIM->CCMR2 |= (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE; // PWM1, preload
    WS2812CT_TIM->CCER  &= ~(TIM_CCER_CC3P | TIM_CCER_CC3NP);
    WS2812CT_TIM->CCER  |= TIM_CCER_CC3E;  // enable output

    WS2812CT_TIM->CCR3  = 0;              // idle low
    WS2812CT_TIM->BDTR |= TIM_BDTR_MOE;   // main output enable

    // Generate UG to latch
    WS2812CT_TIM->EGR = TIM_EGR_UG;

    // Clear pending flags
    WS2812CT_TIM->SR = 0;
}

void ws2812ct_set(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= WS2812CT_LED_COUNT) return;
    uint32_t base = (uint32_t)index*3u;
    s_buf[base+0] = g;
    s_buf[base+1] = r;
    s_buf[base+2] = b;
}

void ws2812ct_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i=0;i<WS2812CT_LED_COUNT;i++) {
        uint32_t base=i*3u; s_buf[base]=g; s_buf[base+1]=r; s_buf[base+2]=b;
    }
}

static void _encode_dma_buf(void)
{
    uint32_t capacity_words = 0u;
    uint16_t *dma_buf = WS2812_AcquireDmaScratch(WS2812CT_DMA_WORDS, &capacity_words);
    if (dma_buf == NULL) {
        return;
    }

    uint32_t out = 0u;
    const uint32_t total_bytes = WS2812CT_LED_COUNT * 3u;

    for (uint32_t i = 0u; i < total_bytes; ++i) {
        uint8_t v = s_buf[i];
        for (int bit = 7; bit >= 0; --bit) {
            dma_buf[out++] = ((v >> bit) & 1u) ? s_t1h : s_t0h;
        }
    }

    for (uint32_t i = 0u; i < WS2812CT_RESET_SLOTS; ++i) {
        dma_buf[out++] = 0u;
    }
}

static void _dma_clock_enable(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
#if defined(__HAL_RCC_DMAMUX1_CLK_ENABLE)
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
#elif defined(__HAL_RCC_DMAMUX_CLK_ENABLE)
    __HAL_RCC_DMAMUX_CLK_ENABLE();
#endif
}

static bool _dma_force_stop(void)
{
    DMA_Stream_TypeDef *stream = (DMA_Stream_TypeDef *)WS2812CT_DMA_STREAM;
    stream->CR &= ~DMA_SxCR_EN;

    uint32_t spins = WS2812CT_WAIT_SPIN_LIMIT;
    while (((stream->CR & DMA_SxCR_EN) != 0u) && (spins-- > 0u)) {
        __NOP();
    }

    return (stream->CR & DMA_SxCR_EN) == 0u;
}

static bool _dma_start(void)
{
    uint32_t capacity_words = 0u;
    uint16_t *dma_buf = WS2812_AcquireDmaScratch(WS2812CT_DMA_WORDS, &capacity_words);
    if (dma_buf == NULL) {
        return false;
    }

    _dma_clock_enable();

    s_dma.Instance                 = WS2812CT_DMA_STREAM;
    if (!_dma_force_stop()) {
        return false;
    }

    s_dma.Init.Request             = WS2812CT_DMA_REQUEST;
    s_dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_dma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_dma.Init.MemInc              = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    s_dma.Init.Mode                = DMA_NORMAL;
    s_dma.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    s_dma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_dma) != HAL_OK) {
        return false;
    }

    __HAL_DMA_DISABLE_IT(&s_dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME | DMA_IT_FE);
    __HAL_DMA_CLEAR_FLAG(&s_dma,
                         __HAL_DMA_GET_TC_FLAG_INDEX(&s_dma) |
                         __HAL_DMA_GET_HT_FLAG_INDEX(&s_dma) |
                         __HAL_DMA_GET_TE_FLAG_INDEX(&s_dma) |
                         __HAL_DMA_GET_DME_FLAG_INDEX(&s_dma) |
                         __HAL_DMA_GET_FE_FLAG_INDEX(&s_dma));

    return HAL_DMA_Start(&s_dma,
                         (uint32_t)&dma_buf[1],
                         (uint32_t)&WS2812CT_TIM->CCR3,
                         (uint32_t)(WS2812CT_DMA_WORDS - 1u)) == HAL_OK;
}

static void _stop_hw(void)
{
    WS2812CT_TIM->DIER &= ~WS2812CT_DMA_DIER_FLAG;
    WS2812CT_TIM->CR1 &= ~TIM_CR1_CEN;
    WS2812CT_TIM->CCR3 = 0;
    (void)_dma_force_stop();
}

static void _wait_dma_done(void)
{
    uint32_t spins = WS2812CT_DMA_TIMEOUT_SPINS;

    while (spins-- > 0u) {
        if (__HAL_DMA_GET_COUNTER(&s_dma) == 0u) break;
        __NOP();
    }
}

void ws2812ct_tx_blocking(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    _encode_dma_buf();
    uint32_t capacity_words = 0u;
    uint16_t *dma_buf = WS2812_AcquireDmaScratch(WS2812CT_DMA_WORDS, &capacity_words);
    if (dma_buf == NULL) goto out;

    WS2812CT_TIM->CCR3 = dma_buf[0];
    WS2812CT_TIM->EGR = TIM_EGR_UG;
    if (!_dma_start()) goto out;

    WS2812CT_TIM->CNT = 0;
    WS2812CT_TIM->SR = 0;
    WS2812CT_TIM->DIER |= WS2812CT_DMA_DIER_FLAG;
    WS2812CT_TIM->CR1 |= TIM_CR1_CEN;

    _wait_dma_done();

out:
    _stop_hw();
    if (!primask) __enable_irq();
}
