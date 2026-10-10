#pragma once
/* Register bit values follow Board/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h723xx.h. */
#include <stdint.h>
typedef struct { volatile uint32_t CR,AHB4ENR,APB2ENR,APB2RSTR,D2CCIP2R; } TestRCC;
typedef struct { volatile uint32_t AFR[2],OTYPER,OSPEEDR,PUPDR,MODER; } TestGPIO;
typedef struct { volatile uint32_t CR1,CR2,CR3,PRESC,BRR,ISR,TDR; } TestUSART;
extern TestRCC test_rcc;
extern TestGPIO test_gpio;
extern TestUSART test_usart;
#define RCC (&test_rcc)
#define GPIOB (&test_gpio)
#define USART1 (&test_usart)
#define __DSB() ((void)0)
#define RCC_AHB4ENR_GPIOBEN                    RCC_AHB4ENR_GPIOBEN_Msk
#define RCC_AHB4ENR_GPIOBEN_Msk                (0x1UL << RCC_AHB4ENR_GPIOBEN_Pos)
#define RCC_AHB4ENR_GPIOBEN_Pos                (1U)
#define RCC_APB2ENR_USART1EN                   RCC_APB2ENR_USART1EN_Msk
#define RCC_APB2ENR_USART1EN_Msk               (0x1UL << RCC_APB2ENR_USART1EN_Pos)
#define RCC_APB2ENR_USART1EN_Pos               (4U)
#define RCC_APB2RSTR_USART1RST                 RCC_APB2RSTR_USART1RST_Msk
#define RCC_APB2RSTR_USART1RST_Msk             (0x1UL << RCC_APB2RSTR_USART1RST_Pos)
#define RCC_APB2RSTR_USART1RST_Pos             (4U)
#define RCC_CR_HSIDIV                          RCC_CR_HSIDIV_Msk
#define RCC_CR_HSIDIV_Msk                      (0x3UL << RCC_CR_HSIDIV_Pos)
#define RCC_CR_HSIDIV_Pos                      (3U)
#define RCC_CR_HSIKERON                        RCC_CR_HSIKERON_Msk
#define RCC_CR_HSIKERON_Msk                    (0x1UL << RCC_CR_HSIKERON_Pos)
#define RCC_CR_HSIKERON_Pos                    (1U)
#define RCC_CR_HSION                           RCC_CR_HSION_Msk
#define RCC_CR_HSION_Msk                       (0x1UL << RCC_CR_HSION_Pos)
#define RCC_CR_HSION_Pos                       (0U)
#define RCC_CR_HSIRDY                          RCC_CR_HSIRDY_Msk
#define RCC_CR_HSIRDY_Msk                      (0x1UL << RCC_CR_HSIRDY_Pos)
#define RCC_CR_HSIRDY_Pos                      (2U)
#define RCC_D2CCIP2R_USART16910SEL             RCC_D2CCIP2R_USART16910SEL_Msk
#define RCC_D2CCIP2R_USART16910SEL_0           (0x1UL << RCC_D2CCIP2R_USART16910SEL_Pos)
#define RCC_D2CCIP2R_USART16910SEL_1           (0x2UL << RCC_D2CCIP2R_USART16910SEL_Pos)
#define RCC_D2CCIP2R_USART16910SEL_Msk         (0x7UL << RCC_D2CCIP2R_USART16910SEL_Pos)
#define RCC_D2CCIP2R_USART16910SEL_Pos         (3U)
#define USART_CR1_TE                    USART_CR1_TE_Msk
#define USART_CR1_TE_Msk                (0x1UL << USART_CR1_TE_Pos)
#define USART_CR1_TE_Pos                (3U)
#define USART_CR1_UE                    USART_CR1_UE_Msk
#define USART_CR1_UE_Msk                (0x1UL << USART_CR1_UE_Pos)
#define USART_CR1_UE_Pos                (0U)
#define USART_ISR_TC                    USART_ISR_TC_Msk
#define USART_ISR_TC_Msk                (0x1UL << USART_ISR_TC_Pos)
#define USART_ISR_TC_Pos                (6U)
#define USART_ISR_TEACK                 USART_ISR_TEACK_Msk
#define USART_ISR_TEACK_Msk             (0x1UL << USART_ISR_TEACK_Pos)
#define USART_ISR_TEACK_Pos             (21U)
#define USART_ISR_TXE_TXFNF             USART_ISR_TXE_TXFNF_Msk
#define USART_ISR_TXE_TXFNF_Msk         (0x1UL << USART_ISR_TXE_TXFNF_Pos)
#define USART_ISR_TXE_TXFNF_Pos         (7U)
