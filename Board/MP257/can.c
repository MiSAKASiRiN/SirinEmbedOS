#include "IO/can_board.h"
#include "stm32mp2xx_ll_rcc.h"

static FDCAN_HandleTypeDef handles[3];
static FDCAN_GlobalTypeDef *const instances[] = {FDCAN1, FDCAN2, FDCAN3};
static const IRQn_Type irqs[] = {FDCAN1_IT0_IRQn, FDCAN2_IT0_IRQn, FDCAN3_IT0_IRQn};
static GPIO_TypeDef *const ports[] = {GPIOB, GPIOI, GPIOD};
static uint16_t held_pins[3];
static const uint8_t pins[][3] = {{9, 11, 7}, {9, 10, 4}, {2, 1, 9}};

FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus)
{
    return bus >= 1 && bus <= 3 ? &handles[bus - 1] : NULL;
}

static bool configure_pin(unsigned index, unsigned pin, unsigned af)
{
    GPIO_TypeDef *port = ports[index];
    const uint32_t cid = *(&port->CIDCFGR0 + 2 * pin);
    const uint16_t mask = (uint16_t)(1U << pin);
    if (port->SECCFGR & mask) return false;
    if (cid & GPIO_CIDCFGR0_CFEN) {
        if (cid & GPIO_CIDCFGR0_SEM_EN) {
            if (!(cid & GPIO_CIDCFGR0_SEMWLC2) || HAL_GPIO_TakePinSemaphore(port, mask) != HAL_OK)
                return false;
            held_pins[index] |= mask;
        } else if ((cid & GPIO_CIDCFGR0_SCID_Msk) != (2U << GPIO_CIDCFGR0_SCID_Pos)) {
            return false;
        }
    }
    const GPIO_InitTypeDef init = {.Pin=mask, .Mode=GPIO_MODE_AF_PP,
        .Pull=GPIO_NOPULL, .Speed=GPIO_SPEED_FREQ_MEDIUM, .Alternate=af};
    HAL_GPIO_Init(port, &init);
    return ((port->MODER >> (2 * pin)) & 3U) == 2 &&
           ((port->AFR[pin / 8] >> (4 * (pin % 8))) & 15U) == af;
}

bool SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config)
{
    FDCAN_HandleTypeDef *h = SRNOS_BoardCANHandle(bus);
    if (!h || !SRNOS_CANControllerConfigValid(bus, config)) return false;
    unsigned i = bus - 1;
    LL_RCC_EnableFDCAN_KerClk();
    LL_RCC_EnableFDCAN_FINDIV();
    __HAL_RCC_FDCAN_CLK_ENABLE();
    const uint32_t hz = LL_RCC_GetFDCANClockFreq(LL_RCC_FDCAN_CLKSOURCE);
    if (!(RCC->FDCANCFGR & RCC_FDCANCFGR_FDCANEN) || hz != 100000000U ||
        (FDCAN_CCU->CCFG & FDCANCCU_CCFG_CDIV)) return false;
    if (bus == 1) __HAL_RCC_GPIOB_CLK_ENABLE();
    if (bus == 2) __HAL_RCC_GPIOI_CLK_ENABLE();
    if (bus == 3) __HAL_RCC_GPIOD_CLK_ENABLE();
    if (!configure_pin(i, pins[i][0], pins[i][2]) ||
        !configure_pin(i, pins[i][1], pins[i][2])) return false;

    h->Instance = instances[i];
    /* HAL offsets are in 32-bit words. Each bus uses 208 of its 256 words. */
    h->Init = (FDCAN_InitTypeDef){
        .FrameFormat=FDCAN_FRAME_CLASSIC, .Mode=FDCAN_MODE_NORMAL,
        .AutoRetransmission=ENABLE, .NominalPrescaler=5, .NominalTimeSeg1=15,
        .NominalTimeSeg2=4, .NominalSyncJumpWidth=4, .DataPrescaler=5,
        .DataTimeSeg1=15, .DataTimeSeg2=4, .DataSyncJumpWidth=4,
        .MessageRAMOffset=i * 256U, .RxFifo0ElmtsNbr=32, .TxFifoQueueElmtsNbr=16,
        .TxEventsNbr=8, .RxFifo0ElmtSize=FDCAN_DATA_BYTES_8, .RxFifo1ElmtSize=FDCAN_DATA_BYTES_8,
        .RxBufferSize=FDCAN_DATA_BYTES_8, .TxElmtSize=FDCAN_DATA_BYTES_8,
        .TxFifoQueueMode=FDCAN_TX_FIFO_OPERATION
    };
#ifdef SRNOS_MP257_CAN_LOOPBACK
    h->Init.Mode = FDCAN_MODE_INTERNAL_LOOPBACK;
#endif
    if (HAL_FDCAN_Init(h) != HAL_OK) return false;
    const uint32_t nbtp = (3U << FDCAN_NBTP_NSJW_Pos) | (4U << FDCAN_NBTP_NBRP_Pos) |
                          (14U << FDCAN_NBTP_NTSEG1_Pos) | (3U << FDCAN_NBTP_NTSEG2_Pos);
    if (h->Instance->NBTP != nbtp || (h->Instance->CCCR & FDCAN_CCCR_FDOE)) return false;
    h->Instance->ILS = 0; /* All enabled notifications use IT0. */
    h->Instance->IR = h->Instance->IR; /* Clear latched events from a prior image. */
    NVIC_SetPriority(irqs[i], 6);
    NVIC_ClearPendingIRQ(irqs[i]);
    NVIC_EnableIRQ(irqs[i]);
    return true;
}

void FDCAN1_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&handles[0]); }
void FDCAN2_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&handles[1]); }
void FDCAN3_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&handles[2]); }

/* Linux m33@0 names IPCC1 channel index 2 (hardware channel 3) "shutdown". */
void SRNOS_BoardShutdownEnable(void)
{
    /* The Linux mailbox driver holds the IPCC1 bus clock while remoteproc uses it. */
    IPCC1->C2CR |= IPCC_C2CR_RXOIE;
    IPCC1->C2MR &= ~IPCC_C2MR_CH3OM;
    NVIC_SetPriority(IPCC1_RX_IRQn, 4);
    NVIC_EnableIRQ(IPCC1_RX_IRQn);
}

void IPCC1_RX_IRQHandler(void)
{
    if (!(IPCC1->C1TOC2SR & IPCC_C1TOC2SR_CH3F)) return;
    __disable_irq();
    for (unsigned i = 0; i < 3; ++i) {
        if (handles[i].Instance) {
            instances[i]->IE = 0;
            instances[i]->TXBCR = instances[i]->TXBRP;
            instances[i]->CCCR |= FDCAN_CCCR_INIT;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (handles[i].Instance)
            while (!(instances[i]->CCCR & FDCAN_CCCR_INIT)) {}
        if (held_pins[i]) (void)HAL_GPIO_ReleasePinSemaphore(ports[i], held_pins[i]);
    }
    __DSB();
    IPCC1->C2SCR = IPCC_C2SCR_CH3C;
    __DSB();
    for (;;) __WFI();
}
