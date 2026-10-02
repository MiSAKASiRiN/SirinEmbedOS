#pragma once
#include "stm32h7xx_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
extern GPIO_TypeDef mcp_gpio;
#define MCP_CS_GPIO_Port (&mcp_gpio)
#define MCP_CS_Pin 8u
#define MCP1_INT_Pin 16u
#define MCP1_INT0_Pin 32u
#define MCP1_INT1_Pin 64u
#ifdef __cplusplus
}
#endif
