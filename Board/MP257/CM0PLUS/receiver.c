#include "stm32mp2xx.h"
#include "SirinEmbedOS/IO/MP257/WBusReceiver.h"
#include "SirinEmbedOS/Services/RemoteControl/WBus/WBus.h"
#include "../IPC/peer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Clock tree supplied by the ATK-DLMP257B Linux remoteproc device. */
#define SRNOS_M0_CORE_HZ 200000000u
#define SRNOS_M0_LPUART_HZ 16000000u
#define SRNOS_M0_BAUD 100000u
static char trace[512] __attribute__((section(".remote_trace"), used));
/* Linux remoteproc v1, one RSC_TRACE entry. All device addresses are 32-bit. */
static const struct {
    uint32_t version, count, reserved[2], offset;
    uint32_t type;
    const void *address;
    uint32_t length, padding;
    char name[32];
} resources __attribute__((section(".resource_table"), used)) = {
    1, 1, {0, 0}, 20, 2, trace, sizeof(trace), 0, "W.Bus RX"
};
static volatile uint32_t tick_ms;
static SRNOS_WBusFramer framer;
static SRNOS_WBusDec latest;
static uint32_t rx_bytes, uart_errors, last_byte_ms, last_frame_ms;
static bool seen_frame, fresh_frame;

void SysTick_Handler(void)
{
    ++tick_ms;
}

static void discard_partial(void)
{
    framer.discarded_bytes += framer.length;
    framer.length = 0;
}

void LPUART1_IRQHandler(void)
{
    const uint32_t errors = USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE;
    /* Bound service time even if the line is continuously noisy. */
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t status = LPUART1->ISR;
        bool available = (status & USART_ISR_RXNE_RXFNE) != 0;
        if (!available && !(status & errors)) break;
        uint8_t byte = 0;
        if (available) {
            byte = (uint8_t)LPUART1->RDR;
            ++rx_bytes;
        }
        if (status & errors) {
            LPUART1->ICR = status & errors; /* PE/FE/NE/ORE have matching clear bits. */
            ++uart_errors;
            discard_partial();
            framer.discarded_bytes += available;
            continue;
        }
        uint32_t now = tick_ms;
        if (now - last_byte_ms > 30u) discard_partial();
        last_byte_ms = now;
        if (SRNOS_WBusFramerFeed(&framer, &byte, 1, &latest)) {
            last_frame_ms = now;
            seen_frame = true;
            fresh_frame = true;
            SRNOS_IpcRadio radio = {.frame_ms = now, .frames = framer.frames,
                .flags = latest.digital17 | (latest.digital18 << 1) |
                         (latest.frame_lost << 2) | (latest.failsafe << 3)};
            for (unsigned ch = 0; ch < SRNOS_WBUS_CHANNEL_COUNT; ++ch)
                radio.channels[ch] = latest.ch[ch];
            SRNOS_IpcPublishRadio(&radio);
        }
    }
}

void IPCC2_RX_IRQHandler(void)
{
    if (IPCC2->C2TOC1SR & IPCC_C2TOC1SR_CH3F) {
        __disable_irq();
        LPUART1->CR1 = 0;
        SysTick->CTRL = 0;
        __DMB();
        IPCC2->C1SCR = IPCC_C1SCR_CH3C;
        __DSB();
        for (;;) __WFI();
    }
}

static void publish(void)
{
    /* Copy the IRQ-owned values, then format with reception enabled. */
    __disable_irq();
    uint32_t now = tick_ms;
    uint32_t age = now - last_frame_ms;
    if (age > 100u) fresh_frame = false;
    const char *state = !seen_frame ? "WAITING" : fresh_frame ? "RECEIVING" : "STALE";
    uint32_t bytes = rx_bytes, frames = framer.frames;
    uint32_t discarded = framer.discarded_bytes, errors = uart_errors;
    SRNOS_WBusDec decoded = latest;
    bool available = seen_frame;
    __enable_irq();

    char text[sizeof(trace)];
    int length = snprintf(text, sizeof(text),
        "W.Bus %s uptime_ms=%" PRIu32 " bytes=%" PRIu32 " frames=%" PRIu32
        " discarded=%" PRIu32 " uart_errors=%" PRIu32 "\n"
        "LPUART1 cr1=%08" PRIx32 " cr2=%08" PRIx32 " brr=%" PRIu32 " reack=%u\n",
        state, now, bytes, frames, discarded, errors, LPUART1->CR1, LPUART1->CR2,
        LPUART1->BRR, (unsigned)((LPUART1->ISR & USART_ISR_REACK) != 0));
    if (length < 0 || (size_t)length >= sizeof(text)) return;
    if (available) {
        int added = snprintf(text + length, sizeof(text) - (size_t)length,
            "last_frame_age_ms=%" PRIu32 " lost=%u failsafe=%u D17=%u D18=%u\n"
            "CH1..16:", age, (unsigned)decoded.frame_lost, (unsigned)decoded.failsafe,
            (unsigned)decoded.digital17, (unsigned)decoded.digital18);
        if (added < 0 || (size_t)added >= sizeof(text) - (size_t)length) return;
        length += added;
        for (unsigned i = 0; i < SRNOS_WBUS_CHANNEL_COUNT; ++i) {
            added = snprintf(text + length, sizeof(text) - (size_t)length,
                             " %u", (unsigned)decoded.ch[i]);
            if (added < 0 || (size_t)added >= sizeof(text) - (size_t)length) return;
            length += added;
        }
        if ((size_t)length + 2 > sizeof(text)) return;
        text[length++] = '\n';
        text[length] = '\0';
    }
    memcpy(trace, text, (size_t)length + 1u);
    __DMB();
}

static uint32_t published, last_poll, last_status;

void SRNOS_MP257WBusReceiverInit(void)
{
    SRNOS_IpcInit();
    SRNOS_WBusFramerInit(&framer);
    /* IPCC2 processor 1 is M0+, processor 2 is Linux (st,proc-id = 1). */
    IPCC2->C1MR &= ~IPCC_C1MR_CH3OM;
    IPCC2->C1CR |= IPCC_C1CR_RXOIE;
    NVIC_EnableIRQ(IPCC2_RX_IRQn);
    LPUART1->CR1 = 0;
    LPUART1->CR2 = USART_CR2_STOP_1 | USART_CR2_RXINV;
    LPUART1->CR3 = USART_CR3_EIE;
    LPUART1->PRESC = 0;
    LPUART1->BRR = (SRNOS_M0_LPUART_HZ / SRNOS_M0_BAUD) * 256u;
    LPUART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    LPUART1->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_RE | USART_CR1_PEIE |
                   USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
    NVIC_EnableIRQ(LPUART1_IRQn);
    SysTick_Config(SRNOS_M0_CORE_HZ / 1000u);
    published = tick_ms;
    last_poll = published - 1u;
    last_status = published - 100u;
    publish();
}

void SRNOS_MP257WBusReceiverWaitAndPoll(void)
{
    __WFI();
    uint32_t now = tick_ms;
    if (now != last_poll) {
        SRNOS_IpcPoll(now);
        last_poll = now;
    }
    if (now - last_status >= 100u) {
        __disable_irq();
        SRNOS_IpcStatus status = {.uptime_ms = now, .uart_bytes = rx_bytes,
            .uart_errors = uart_errors, .frames = framer.frames, .discarded = framer.discarded_bytes};
        __enable_irq();
        SRNOS_IpcPublishStatus(&status);
        last_status = now;
    }
    if (tick_ms - published >= 50u) {
        published = tick_ms;
        publish();
    }
}
