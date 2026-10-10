#include "IO/can_board.h"

/* Test-only A/B: 1 Mbit/s remains unchanged, nominal sample point becomes
 * 80%, matching MCP2518FD's 40 MHz vendor profile. No production override. */
bool __real_SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config);
bool __wrap_SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config)
{
    if (!__real_SRNOS_BoardCANApplyConfig(bus, config)) return false;
    if (bus != 1u || config->mode != SRNOS_CAN_MODE_FD) return true;
    FDCAN_HandleTypeDef *h = SRNOS_BoardCANHandle(bus);
    h->Init.NominalTimeSeg1 = 63u;
    h->Init.NominalTimeSeg2 = 16u;
    if (HAL_FDCAN_Init(h) != HAL_OK) return false;
    if (HAL_FDCAN_ConfigTxDelayCompensation(h,
            h->Init.DataTimeSeg1 * h->Init.DataPrescaler, 0u) != HAL_OK) return false;
    return HAL_FDCAN_EnableTxDelayCompensation(h) == HAL_OK;
}
