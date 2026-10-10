#include "fault_usart1.h"
#include "stm32h7xx.h"

/* Fixed board: PB14 AF4 = USART1 TX. No HAL tick, scheduler, DMA or heap.
 * Bounded polls still terminate if the clock or peripheral never becomes ready.
 * HSI selection also changes the USART6/UART9/USART10 shared kernel mux; the
 * system is already in fatal handling and will reset/halt, never resume tasks. */
#define SRNOS_FAULT_USART1_BAUD 115200u
#define SRNOS_FAULT_USART1_POLL_LIMIT 1000000u
static bool initialized, failed;

static bool wait_bits(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t n=0; n<SRNOS_FAULT_USART1_POLL_LIMIT; ++n)
        if ((*reg & mask) == mask) return true;
    failed=true;
    return false;
}

static bool initialize(void)
{
    if (failed) return false;
    if (initialized) return true;
    RCC->CR |= RCC_CR_HSION | RCC_CR_HSIKERON;
    if (!wait_bits(&RCC->CR,RCC_CR_HSIRDY)) return false;
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    __DSB();
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    __DSB();
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->D2CCIP2R = (RCC->D2CCIP2R & ~RCC_D2CCIP2R_USART16910SEL) |
        RCC_D2CCIP2R_USART16910SEL_0 | RCC_D2CCIP2R_USART16910SEL_1;
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(15u<<24)) | (4u<<24);
    GPIOB->OTYPER &= ~(1u<<14);
    GPIOB->OSPEEDR = (GPIOB->OSPEEDR & ~(3u<<28)) | (2u<<28);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u<<28)) | (1u<<28);
    GPIOB->MODER = (GPIOB->MODER & ~(3u<<28)) | (2u<<28);
    USART1->CR1=0;
    USART1->CR2=0;
    USART1->CR3=0;
    USART1->PRESC=0;
    const uint32_t hz=64000000u >> ((RCC->CR & RCC_CR_HSIDIV)>>RCC_CR_HSIDIV_Pos);
    USART1->BRR=(hz+SRNOS_FAULT_USART1_BAUD/2u)/SRNOS_FAULT_USART1_BAUD;
    USART1->CR1=USART_CR1_TE | USART_CR1_UE;
    if (!wait_bits(&USART1->ISR,USART_ISR_TEACK)) return false;
    initialized=true;
    return true;
}

bool SRNOS_FaultUSART1WriteByte(uint8_t byte)
{
    if (!initialize() || !wait_bits(&USART1->ISR,USART_ISR_TXE_TXFNF)) return false;
    USART1->TDR=byte;
    return true;
}

void SRNOS_FaultUSART1Flush(void)
{
    if (initialized && !failed) (void)wait_bits(&USART1->ISR,USART_ISR_TC);
}
