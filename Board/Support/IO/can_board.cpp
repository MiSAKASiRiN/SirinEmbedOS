#include "IO/can_board.h"
#include "main.h"
#include "fdcan.h"
#include "IO/CAN/External/EXFDCAN_Router_Internal.h"

static const SRNOS_ExternalCANDevCfg s_cfg[] =
{
    {
        .dev_id   = 0u,
        .bus_id   = SRNOS_SPI_BUS_4,
        .cs_port  = MCP_CS_GPIO_Port,
        .cs_pin   = MCP_CS_Pin,
        .int_pin  = MCP1_INT_Pin,
        .int0_pin = MCP1_INT0_Pin,
        .int1_pin = MCP1_INT1_Pin
    }
};

extern "C" const SRNOS_ExternalCANDevCfg *SRNOS_BoardExternalCANConfig(uint8_t dev)
{
    return dev < sizeof(s_cfg)/sizeof(s_cfg[0]) ? &s_cfg[dev] : nullptr;
}
extern "C" FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus)
{
    switch(bus) { case 1: return &hfdcan1; case 2: return &hfdcan2;
                  case 3: return &hfdcan3; default: return nullptr; }
}
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    SRNOS_ExternalCANOnExti(pin);
}
