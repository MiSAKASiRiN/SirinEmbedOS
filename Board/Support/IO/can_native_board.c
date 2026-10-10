#include "IO/can_board.h"

/* All three FDCANs share PLL2Q and 2560 words of Message RAM. Each controller
 * owns a fixed 0x300-word window regardless of its frozen frame format. */
enum { SRNOS_CAN_NATIVE_CLOCK_HZ = 80000000u,
       SRNOS_CAN_NATIVE_RAM_STRIDE = 0x300u,
       SRNOS_CAN_NATIVE_RAM_WORDS = 2560u };

static bool SRNOS_BoardCANDataTiming(FDCAN_InitTypeDef *init, uint32_t bitrate)
{
    init->DataTimeSeg1 = 29u;
    init->DataTimeSeg2 = 10u;
    init->DataSyncJumpWidth = 4u;
    switch (bitrate) {
        case 125000u: init->DataPrescaler = 16u; break;
        case 250000u: init->DataPrescaler = 8u; break;
        case 500000u: init->DataPrescaler = 4u; break;
        case 1000000u: init->DataPrescaler = 2u; break;
        case 2000000u: init->DataPrescaler = 1u; break;
        case 4000000u:
            init->DataPrescaler = 1u;
            init->DataTimeSeg1 = 14u;
            init->DataTimeSeg2 = 5u;
            break;
        case 5000000u:
            init->DataPrescaler = 1u;
            init->DataTimeSeg1 = 11u;
            init->DataTimeSeg2 = 4u;
            break;
        case 8000000u:
            init->DataPrescaler = 1u;
            init->DataTimeSeg1 = 7u;
            init->DataTimeSeg2 = 2u;
            init->DataSyncJumpWidth = 2u;
            break;
        default: return false;
    }
    return true;
}

bool SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config)
{
    FDCAN_HandleTypeDef *h = SRNOS_BoardCANHandle(bus);
    if (bus < 1u || bus > 3u || !h || !config ||
        !SRNOS_CANControllerConfigValid(bus, config) || h->State != HAL_FDCAN_STATE_READY ||
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) != SRNOS_CAN_NATIVE_CLOCK_HZ ||
        (FDCAN_CCU->CCFG & FDCANCCU_CCFG_CDIV) != 0u) return false;

    const bool fd = config->mode == SRNOS_CAN_MODE_FD;
    FDCAN_InitTypeDef init = h->Init;
    init.FrameFormat = fd ? (config->brs ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_FD_NO_BRS)
                          : FDCAN_FRAME_CLASSIC;
    init.NominalPrescaler = 1000000u / config->nominal_bitrate;
    init.NominalTimeSeg1 = fd ? 63u : 60u;
    init.NominalTimeSeg2 = fd ? 16u : 19u;
    init.NominalSyncJumpWidth = 13u;
    if (!SRNOS_BoardCANDataTiming(&init, fd ? config->data_bitrate : config->nominal_bitrate)) return false;

    if(fd && config->data_bitrate<=4000000u) {
        init.DataTimeSeg1 = config->data_bitrate==4000000u ? 15u : 31u;
        init.DataTimeSeg2 = config->data_bitrate==4000000u ? 4u : 8u;
    }

    init.MessageRAMOffset = (bus - 1u) * SRNOS_CAN_NATIVE_RAM_STRIDE;
    init.StdFiltersNbr = 4u;
    init.ExtFiltersNbr = 4u;
    init.RxFifo0ElmtsNbr = 8u;
    init.RxFifo1ElmtsNbr = fd ? 0u : 8u;
    init.RxBuffersNbr = fd ? 0u : 3u;
    init.TxBuffersNbr = fd ? 0u : 4u;
    init.TxFifoQueueElmtsNbr = 28u;
    init.TxEventsNbr = 8u;
    init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    init.RxFifo0ElmtSize = fd ? FDCAN_DATA_BYTES_64 : FDCAN_DATA_BYTES_8;
    init.RxFifo1ElmtSize = init.RxFifo0ElmtSize;
    init.RxBufferSize = init.RxFifo0ElmtSize;
    init.TxElmtSize = init.RxFifo0ElmtSize;
    /* HAL sizes include the two header words. Its own check catches the total
     * RAM end, but not overlap with another controller's allocation. */
    const uint32_t words = init.StdFiltersNbr + 2u * init.ExtFiltersNbr +
        init.RxFifo0ElmtsNbr * init.RxFifo0ElmtSize +
        init.RxFifo1ElmtsNbr * init.RxFifo1ElmtSize +
        init.RxBuffersNbr * init.RxBufferSize + 2u * init.TxEventsNbr +
        (init.TxBuffersNbr + init.TxFifoQueueElmtsNbr) * init.TxElmtSize;
    if (words > SRNOS_CAN_NATIVE_RAM_STRIDE ||
        init.MessageRAMOffset + words >= SRNOS_CAN_NATIVE_RAM_WORDS) return false;

    h->Init = init;
    if (HAL_FDCAN_Init(h) != HAL_OK) return false;
    /* ST's FDCAN_Com_IT reference uses TDCO = DataTimeSeg1 * DataPrescaler,
     * TDCF = 0. At >1 Mbit/s these profiles use prescaler 1. This is a software
     * baseline, not validation of the physical transceiver or bus topology. */
    if (fd && config->brs && config->data_bitrate > 1000000u) {
        if (HAL_FDCAN_ConfigTxDelayCompensation(h, init.DataTimeSeg1 * init.DataPrescaler, 0u) != HAL_OK)
            return false;
        return HAL_FDCAN_EnableTxDelayCompensation(h) == HAL_OK;
    }
    return HAL_FDCAN_DisableTxDelayCompensation(h) == HAL_OK;
}
