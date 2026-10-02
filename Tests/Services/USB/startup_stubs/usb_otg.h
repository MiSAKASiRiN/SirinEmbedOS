#pragma once
#include <stdint.h>
typedef struct { void *Instance; } PCD_HandleTypeDef;
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
#define USB_OTG_HS ((void *)0x1000)
#define GPIOA ((void *)0x2000)
#define GPIO_PIN_11 (1u<<11)
#define GPIO_PIN_12 (1u<<12)
#define GPIO_MODE_AF_PP 2u
#define GPIO_NOPULL 0u
#define GPIO_SPEED_FREQ_VERY_HIGH 3u
#define GPIO_AF10_OTG1_HS 10u
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
void test_gpio_clock(void);
#define __HAL_RCC_GPIOA_CLK_ENABLE() test_gpio_clock()
void HAL_GPIO_Init(void *, const GPIO_InitTypeDef *);
