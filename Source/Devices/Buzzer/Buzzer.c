#include "Devices/buzzer_hardware.h"
//
// Created by sirin on 2025/10/1.
//

#include "buzzer_service.h"

#include "SirinEmbedOS/Core/Memory/MemorySections.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t bsrr_table[2] SRNOS_DMA_NC_BUF_BSS;

static TIM_HandleTypeDef htim_buz;
static DMA_HandleTypeDef hdma_buz;

static volatile uint8_t s_initialized = 0u;
static volatile uint8_t s_running = 0u;
static volatile uint32_t s_curr_freq = 0u;

static volatile uint8_t s_playing_item = 0u;
static volatile uint8_t s_stop_requested = 0u;
static uint32_t s_deadline_ms = 0u;
static SRNOS_BuzzerWakeFn s_service_wake;
static void wake_service(bool from_isr) { if(s_service_wake)s_service_wake(from_isr); }

static float s_ref_a4 = 440.0f;

typedef struct {
    SRNOS_BuzzerNote buf[SRNOS_BUZZER_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} buzzer_queue_t;

static buzzer_queue_t s_q = { .head = 0u, .tail = 0u };

static uint32_t buzzer_default_now_ms(void *ctx)
{
    (void)ctx;
    return HAL_GetTick();
}

static uint32_t buzzer_default_enter(void *ctx)
{
    (void)ctx;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void buzzer_default_exit(void *ctx, uint32_t token)
{
    (void)ctx;
    __set_PRIMASK(token & 1u);
}

static const SRNOS_BuzzerPortHooks s_default_hooks = {
    .now_ms = buzzer_default_now_ms,
    .enter = buzzer_default_enter,
    .exit = buzzer_default_exit,
    .enter_from_isr = buzzer_default_enter,
    .exit_from_isr = buzzer_default_exit,
};

static SRNOS_BuzzerPortHooks s_hooks = {
    .now_ms = buzzer_default_now_ms,
    .enter = buzzer_default_enter,
    .exit = buzzer_default_exit,
    .enter_from_isr = buzzer_default_enter,
    .exit_from_isr = buzzer_default_exit,
};
static void *s_hook_ctx = NULL;

static void buzzer_load_hooks(const SRNOS_BuzzerConfig *cfg)
{
    s_hooks = s_default_hooks;
    s_hook_ctx = NULL;

    if (cfg == NULL) {
        return;
    }

    if (cfg->hooks.now_ms != NULL) {
        s_hooks.now_ms = cfg->hooks.now_ms;
    }
    if (cfg->hooks.enter != NULL) {
        s_hooks.enter = cfg->hooks.enter;
    }
    if (cfg->hooks.exit != NULL) {
        s_hooks.exit = cfg->hooks.exit;
    }
    if (cfg->hooks.enter_from_isr != NULL) {
        s_hooks.enter_from_isr = cfg->hooks.enter_from_isr;
    }
    if (cfg->hooks.exit_from_isr != NULL) {
        s_hooks.exit_from_isr = cfg->hooks.exit_from_isr;
    }
    s_hook_ctx = cfg->hook_ctx;
}

static uint32_t buzzer_enter(void)
{
    return s_hooks.enter(s_hook_ctx);
}

static void buzzer_exit(uint32_t token)
{
    s_hooks.exit(s_hook_ctx, token);
}

static uint32_t buzzer_enter_from_isr(void)
{
    return s_hooks.enter_from_isr(s_hook_ctx);
}

static void buzzer_exit_from_isr(uint32_t token)
{
    s_hooks.exit_from_isr(s_hook_ctx, token);
}

static uint32_t buzzer_now_ms(void)
{
    return s_hooks.now_ms(s_hook_ctx);
}

static uint16_t q_next(uint16_t x)
{
    return (uint16_t)((x + 1u) % SRNOS_BUZZER_QUEUE_CAPACITY);
}

static uint16_t q_count_locked(void)
{
    if (s_q.head >= s_q.tail) {
        return (uint16_t)(s_q.head - s_q.tail);
    }
    return (uint16_t)(SRNOS_BUZZER_QUEUE_CAPACITY - s_q.tail + s_q.head);
}

static uint16_t q_free_locked(void)
{
    return (uint16_t)((SRNOS_BUZZER_QUEUE_CAPACITY - 1u) - q_count_locked());
}

static SRNOS_BuzzerStatus q_push_locked(SRNOS_BuzzerNote n)
{
    const uint16_t nh = q_next(s_q.head);
    if (nh == s_q.tail) {
        return SRNOS_BUZZER_STATUS_QUEUE_FULL;
    }

    s_q.buf[s_q.head] = n;
    s_q.head = nh;
    return SRNOS_BUZZER_STATUS_OK;
}

static bool q_pop_locked(SRNOS_BuzzerNote *out)
{
    if (s_q.head == s_q.tail) {
        return false;
    }

    *out = s_q.buf[s_q.tail];
    s_q.tail = q_next(s_q.tail);
    return true;
}

static void q_clear_locked(void)
{
    s_q.head = 0u;
    s_q.tail = 0u;
}

static uint32_t buzzer_get_tim_clock(void)
{
    RCC_ClkInitTypeDef clk;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clk, &latency);

#if defined(TIM1) || defined(TIM8) || defined(TIM15) || defined(TIM16) || defined(TIM17)
    if (
#if defined(TIM1)
        (SRNOS_BUZZER_TIM == TIM1) ||
#endif
#if defined(TIM8)
        (SRNOS_BUZZER_TIM == TIM8) ||
#endif
#if defined(TIM15)
        (SRNOS_BUZZER_TIM == TIM15) ||
#endif
#if defined(TIM16)
        (SRNOS_BUZZER_TIM == TIM16) ||
#endif
#if defined(TIM17)
        (SRNOS_BUZZER_TIM == TIM17) ||
#endif
        false) {
        const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
        return (clk.APB2CLKDivider == RCC_HCLK_DIV1) ? pclk2 : (pclk2 * 2u);
    }
#endif

    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return (clk.APB1CLKDivider == RCC_HCLK_DIV1) ? pclk1 : (pclk1 * 2u);
}

static void buzzer_stop_hw_locked(void)
{
    __HAL_TIM_DISABLE_DMA(&htim_buz, TIM_DMA_UPDATE);
    (void)HAL_TIM_Base_Stop(&htim_buz);
    (void)HAL_DMA_Abort(&hdma_buz);
    SRNOS_BUZZER_GPIO_PORT->BSRR = (1u << (SRNOS_BUZZER_PIN + 16u));
    s_running = 0u;
    s_curr_freq = 0u;
}

static SRNOS_BuzzerStatus buzzer_apply_freq_locked(uint32_t f_hz)
{
    if (f_hz == 0u) {
        buzzer_stop_hw_locked();
        return SRNOS_BUZZER_STATUS_OK;
    }

    if (f_hz > (UINT32_MAX / 2u)) {
        buzzer_stop_hw_locked();
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    const uint32_t timclk = buzzer_get_tim_clock();
    const uint32_t updates = f_hz * 2u;
    if ((updates == 0u) || (updates > timclk)) {
        buzzer_stop_hw_locked();
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    uint32_t arr = timclk / updates;
    if (arr == 0u) {
        arr = 1u;
    }
    arr -= 1u;

    if (s_running == 0u) {
        __HAL_TIM_DISABLE(&htim_buz);
        __HAL_TIM_SET_PRESCALER(&htim_buz, 0u);
        __HAL_TIM_SET_AUTORELOAD(&htim_buz, arr);
        __HAL_TIM_SET_COUNTER(&htim_buz, 0u);

        HAL_StatusTypeDef st = HAL_DMA_Start(&hdma_buz,
                                             (uint32_t)bsrr_table,
                                             (uint32_t)&SRNOS_BUZZER_GPIO_PORT->BSRR,
                                             2u);
        if (st != HAL_OK) {
            buzzer_stop_hw_locked();
            return SRNOS_BUZZER_STATUS_HW_ERROR;
        }

        __HAL_TIM_ENABLE_DMA(&htim_buz, TIM_DMA_UPDATE);
        st = HAL_TIM_Base_Start(&htim_buz);
        if (st != HAL_OK) {
            buzzer_stop_hw_locked();
            return SRNOS_BUZZER_STATUS_HW_ERROR;
        }
        s_running = 1u;
    } else {
        htim_buz.Instance->ARR = arr;
        htim_buz.Instance->EGR = TIM_EGR_UG;
    }

    s_curr_freq = f_hz;
    return SRNOS_BUZZER_STATUS_OK;
}

static SRNOS_BuzzerStatus buzzer_require_init(void)
{
    return (s_initialized != 0u) ? SRNOS_BUZZER_STATUS_OK : SRNOS_BUZZER_STATUS_NOT_INIT;
}

SRNOS_BuzzerStatus SRNOS_BuzzerInit(const SRNOS_BuzzerConfig *cfg)
{
    buzzer_load_hooks(cfg);

    bsrr_table[0] = (1u << SRNOS_BUZZER_PIN);
    bsrr_table[1] = (1u << (SRNOS_BUZZER_PIN + 16u));

    SRNOS_BUZZER_GPIO_CLK_EN();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = (1u << SRNOS_BUZZER_PIN);
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SRNOS_BUZZER_GPIO_PORT, &gpio);
    SRNOS_BUZZER_GPIO_PORT->BSRR = (1u << (SRNOS_BUZZER_PIN + 16u));

    SRNOS_BUZZER_TIM_CLK_EN();
    htim_buz.Instance = SRNOS_BUZZER_TIM;
    htim_buz.Init.Prescaler = 0u;
    htim_buz.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_buz.Init.Period = 1000u;
    htim_buz.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_buz.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim_buz) != HAL_OK) {
        s_initialized = 0u;
        return SRNOS_BUZZER_STATUS_HW_ERROR;
    }

    __HAL_RCC_DMA1_CLK_ENABLE();
#if defined(__HAL_RCC_DMAMUX1_CLK_ENABLE)
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
#elif defined(__HAL_RCC_DMAMUX_CLK_ENABLE)
    __HAL_RCC_DMAMUX_CLK_ENABLE();
#endif

    hdma_buz.Instance = SRNOS_BUZZER_DMA;
    hdma_buz.Init.Request = SRNOS_BUZZER_DMA_REQ;
    hdma_buz.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_buz.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_buz.Init.MemInc = DMA_MINC_ENABLE;
    hdma_buz.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_buz.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_buz.Init.Mode = DMA_CIRCULAR;
    hdma_buz.Init.Priority = DMA_PRIORITY_LOW;
    hdma_buz.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_buz) != HAL_OK) {
        s_initialized = 0u;
        return SRNOS_BUZZER_STATUS_HW_ERROR;
    }

    __HAL_LINKDMA(&htim_buz, hdma[TIM_DMA_ID_UPDATE], hdma_buz);

    const uint32_t token = buzzer_enter();
    q_clear_locked();
    s_running = 0u;
    s_curr_freq = 0u;
    s_playing_item = 0u;
    s_stop_requested = 0u;
    s_deadline_ms = 0u;
    s_initialized = 1u;
    buzzer_exit(token);

    return SRNOS_BUZZER_STATUS_OK;
}

SRNOS_BuzzerStatus SRNOS_BuzzerStart(uint32_t freq_hz)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter();
    st = buzzer_apply_freq_locked(freq_hz);
    if (st == SRNOS_BUZZER_STATUS_OK) {
        s_playing_item = 0u;
        s_stop_requested = 0u;
    }
    buzzer_exit(token);
    wake_service(false);
    return st;
}

SRNOS_BuzzerStatus SRNOS_BuzzerSetFrequency(uint32_t freq_hz)
{
    return SRNOS_BuzzerStart(freq_hz);
}

SRNOS_BuzzerStatus SRNOS_BuzzerStop(void)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter();
    st = buzzer_apply_freq_locked(0u);
    s_playing_item = 0u;
    s_stop_requested = 0u;
    buzzer_exit(token);
    wake_service(false);
    return st;
}

SRNOS_BuzzerStatus SRNOS_BuzzerSetRefA4(float hz)
{
    if (hz < 200.0f) {
        hz = 200.0f;
    }
    if (hz > 500.0f) {
        hz = 500.0f;
    }

    const uint32_t token = buzzer_enter();
    s_ref_a4 = hz;
    buzzer_exit(token);
    return SRNOS_BUZZER_STATUS_OK;
}

static int note_letter_to_index(char n)
{
    switch ((char)toupper((unsigned char)n)) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return -1;
    }
}

static int parse_accidental(const char *p, int *consumed)
{
    *consumed = 0;
    if (*p == '#') {
        *consumed = 1;
        return 1;
    }
    if (*p == 'b') {
        *consumed = 1;
        return -1;
    }
    return 0;
}

static bool parse_int(const char *p, int *out, int *consumed)
{
    int i = 0;
    int sign = 1;
    int val = 0;
    if (p[i] == '+') {
        i++;
    } else if (p[i] == '-') {
        sign = -1;
        i++;
    }
    if (!isdigit((unsigned char)p[i])) {
        *consumed = 0;
        return false;
    }
    while (isdigit((unsigned char)p[i])) {
        val = (val * 10) + (p[i] - '0');
        i++;
    }
    *out = sign * val;
    *consumed = i;
    return true;
}

SRNOS_BuzzerStatus SRNOS_BuzzerStartNote(const char *note)
{
    if (note == NULL) {
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    if (toupper((unsigned char)note[0]) == 'R') {
        return SRNOS_BuzzerStop();
    }

    const int idx = note_letter_to_index(note[0]);
    if (idx < 0) {
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    int acc_c = 0;
    const int acc = parse_accidental(&note[1], &acc_c);

    int octave = 4;
    int oct_c = 0;
    if (note[1 + acc_c] != '\0') {
        if (!parse_int(&note[1 + acc_c], &octave, &oct_c)) {
            return SRNOS_BUZZER_STATUS_INVALID_ARG;
        }
    }

    int semitone = idx + acc;
    while (semitone < 0) {
        semitone += 12;
        octave -= 1;
    }
    while (semitone > 11) {
        semitone -= 12;
        octave += 1;
    }

    int midi = ((octave + 1) * 12) + semitone;
    if (midi < 0) {
        midi = 0;
    }
    if (midi > 127) {
        midi = 127;
    }

    const double n = (double)(midi - 69) / 12.0;
    const double freq = (double)s_ref_a4 * pow(2.0, n);
    if (freq < 1.0) {
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    return SRNOS_BuzzerSetFrequency((uint32_t)(freq + 0.5));
}

SRNOS_BuzzerStatus SRNOS_BuzzerEnqueue(SRNOS_BuzzerNote n)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter();
    st = q_push_locked(n);
    buzzer_exit(token);
    wake_service(false);
    return st;
}

SRNOS_BuzzerStatus SRNOS_BuzzerEnqueueMany(const SRNOS_BuzzerNote *arr, uint16_t n)
{
    if ((arr == NULL) && (n > 0u)) {
        return SRNOS_BUZZER_STATUS_INVALID_ARG;
    }

    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter();
    if (q_free_locked() < n) {
        buzzer_exit(token);
        return SRNOS_BUZZER_STATUS_QUEUE_FULL;
    }
    for (uint16_t i = 0u; i < n; ++i) {
        (void)q_push_locked(arr[i]);
    }
    buzzer_exit(token);
    wake_service(false);
    return SRNOS_BUZZER_STATUS_OK;
}

SRNOS_BuzzerStatus SRNOS_BuzzerFlush(void)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter();
    q_clear_locked();
    s_playing_item = 0u;
    s_stop_requested = 0u;
    st = buzzer_apply_freq_locked(0u);
    buzzer_exit(token);
    wake_service(false);
    return st;
}

SRNOS_BuzzerStatus SRNOS_BuzzerEnqueueFromISR(SRNOS_BuzzerNote n)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter_from_isr();
    st = q_push_locked(n);
    buzzer_exit_from_isr(token);
    wake_service(true);
    return st;
}

SRNOS_BuzzerStatus SRNOS_BuzzerFlushFromISR(void)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t token = buzzer_enter_from_isr();
    q_clear_locked();
    s_playing_item = 0u;
    s_stop_requested = 1u;
    buzzer_exit_from_isr(token);
    wake_service(true);
    return SRNOS_BUZZER_STATUS_OK;
}

bool SRNOS_BuzzerQueueEmpty(void)
{
    const uint32_t token = buzzer_enter();
    const bool empty = (s_q.head == s_q.tail);
    buzzer_exit(token);
    return empty;
}

bool SRNOS_BuzzerIsActive(void)
{
    return (s_running != 0u) && (s_curr_freq > 0u);
}

SRNOS_BuzzerStatus SRNOS_BuzzerService(void)
{
    SRNOS_BuzzerStatus st = buzzer_require_init();
    if (st != SRNOS_BUZZER_STATUS_OK) {
        return st;
    }

    const uint32_t now = buzzer_now_ms();
    SRNOS_BuzzerNote next = {0u, 0u};
    bool have_next = false;
    bool ended_playback_item = false;

    const uint32_t token = buzzer_enter();

    if (s_stop_requested != 0u) {
        st = buzzer_apply_freq_locked(0u);
        s_stop_requested = 0u;
        if (st != SRNOS_BUZZER_STATUS_OK) {
            buzzer_exit(token);
            return st;
        }
    }

    if (s_playing_item != 0u) {
        if ((int32_t)(now - s_deadline_ms) < 0) {
            buzzer_exit(token);
            return SRNOS_BUZZER_STATUS_OK;
        }
        s_playing_item = 0u;
        ended_playback_item = true;
    }

    have_next = q_pop_locked(&next);
    if (have_next) {
        st = buzzer_apply_freq_locked(next.freq_hz);
        if (st == SRNOS_BUZZER_STATUS_OK) {
            s_playing_item = 1u;
            s_deadline_ms = now + (uint32_t)next.dur_ms;
        }
        buzzer_exit(token);
        return st;
    }

    if (ended_playback_item) {
        st = buzzer_apply_freq_locked(0u);
    }

    buzzer_exit(token);
    return st;
}

/* The fixed board IRQ dispatches to this private DMA handle. */
void SRNOS_BuzzerDMAInterrupt(void) { HAL_DMA_IRQHandler(&hdma_buz); }

void SRNOS_BuzzerSetServiceWake(SRNOS_BuzzerWakeFn wake)
{
    const uint32_t token=buzzer_enter();s_service_wake=wake;buzzer_exit(token);
}
uint32_t SRNOS_BuzzerServiceNextWait(uint32_t now_ms)
{
    const uint32_t token=buzzer_enter();uint32_t wait=UINT32_MAX;
    if(s_stop_requested || (!s_playing_item && s_q.head!=s_q.tail)) wait=0;
    else if(s_playing_item) {
        int32_t delta=(int32_t)(s_deadline_ms-now_ms);wait=delta<=0?0:(uint32_t)delta;
    }
    buzzer_exit(token);return wait;
}
