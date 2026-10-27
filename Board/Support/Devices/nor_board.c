#include "Devices/nor_board.h"
#include "cmsis_os2.h"
#include "octospi.h"

#define SRNOS_NOR_BOARD_MAX_CLOCK_HZ 20000000U
#define SRNOS_NOR_BOARD_TRANSFER_TIMEOUT_MS 5U

static osThreadId_t owner;
static bool transport_failed;

static bool task_context(void)
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U &&
           osKernelGetState() == osKernelRunning && osThreadGetId() != NULL;
}

bool SRNOS_NORBoardIsOwner(void)
{
    return task_context() && owner != NULL && owner == osThreadGetId();
}

static SRNOS_NORStatus transfer_result(HAL_StatusTypeDef result)
{
    if (result == HAL_OK) return SRNOS_NOR_OK;
    /* No abort/reset/retry after a partial program transfer: the NOR may have
     * accepted data already. Quarantine transport for the remainder of boot. */
    transport_failed = true;
    return result == HAL_TIMEOUT ? SRNOS_NOR_TIMEOUT : SRNOS_NOR_IO_ERROR;
}

SRNOS_NORStatus SRNOS_NORBoardInitialize(void)
{
    if (!task_context() || (owner != NULL && owner != osThreadGetId())) return SRNOS_NOR_INVALID;
    if (owner != NULL) return transport_failed ? SRNOS_NOR_IO_ERROR : SRNOS_NOR_OK;
    owner = osThreadGetId();
    if (hospi1.Instance != OCTOSPI1 || HAL_OSPI_GetState(&hospi1) != HAL_OSPI_STATE_READY ||
        __HAL_OSPI_GET_FLAG(&hospi1, HAL_OSPI_FLAG_BUSY) != RESET)
        return transfer_result(HAL_ERROR);
    /* This HAL revision does not handle OSPI in GetPeriphCLKFreq. The fixed
     * board uses D1HCLK; reject other mux sources instead of guessing a rate. */
    if (__HAL_RCC_GET_OSPI_SOURCE() != RCC_OSPICLKSOURCE_D1HCLK)
        return transfer_result(HAL_ERROR);
    const uint32_t source_hz = HAL_RCC_GetHCLKFreq();
    const uint32_t divider = source_hz / SRNOS_NOR_BOARD_MAX_CLOCK_HZ +
                             (source_hz % SRNOS_NOR_BOARD_MAX_CLOCK_HZ != 0U);
    if (divider == 0U || divider > 256U) return transfer_result(HAL_ERROR);

    /* CubeMX owns MSP, pin mux and OSPIM routing. Its default /3 clock can
     * exceed READ 03h's limit. HAL_OSPI_Init ignores Init changes when READY,
     * so apply this fixed SDR profile with the idle peripheral disabled.
     * MTYP Micron/Macronix only selects octal-DTR ordering; choose the standard
     * non-HyperBus value explicitly, not as a claim about the chip vendor. */
    __HAL_OSPI_DISABLE(&hospi1);
    hospi1.Init.FifoThreshold = 8U;
    hospi1.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
    hospi1.Init.MemoryType = HAL_OSPI_MEMTYPE_MICRON;
    hospi1.Init.DeviceSize = 24U; /* HAL uses address bits, not register encoding. */
    hospi1.Init.ChipSelectHighTime = 4U; /* >= 200 ns at <= 20 MHz. */
    hospi1.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
    hospi1.Init.ClockMode = HAL_OSPI_CLOCK_MODE_3;
    hospi1.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
    hospi1.Init.ClockPrescaler = divider;
    hospi1.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_HALFCYCLE;
    hospi1.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
    hospi1.Init.ChipSelectBoundary = 0U;
    hospi1.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED;
    hospi1.Init.MaxTran = 0U;
    hospi1.Init.Refresh = 0U;
    MODIFY_REG(hospi1.Instance->DCR1,
               OCTOSPI_DCR1_MTYP | OCTOSPI_DCR1_DEVSIZE | OCTOSPI_DCR1_CSHT |
               OCTOSPI_DCR1_DLYBYP | OCTOSPI_DCR1_FRCK | OCTOSPI_DCR1_CKMODE,
               hospi1.Init.MemoryType | ((hospi1.Init.DeviceSize - 1U) << OCTOSPI_DCR1_DEVSIZE_Pos) |
               ((hospi1.Init.ChipSelectHighTime - 1U) << OCTOSPI_DCR1_CSHT_Pos) |
               hospi1.Init.DelayBlockBypass | hospi1.Init.ClockMode);
    MODIFY_REG(hospi1.Instance->DCR2, OCTOSPI_DCR2_WRAPSIZE | OCTOSPI_DCR2_PRESCALER,
               (divider - 1U) << OCTOSPI_DCR2_PRESCALER_Pos);
    hospi1.Instance->DCR3 = 0U;
    hospi1.Instance->DCR4 = 0U;
    MODIFY_REG(hospi1.Instance->CR, OCTOSPI_CR_FTHRES | OCTOSPI_CR_DQM,
               (hospi1.Init.FifoThreshold - 1U) << OCTOSPI_CR_FTHRES_Pos);
    MODIFY_REG(hospi1.Instance->TCR, OCTOSPI_TCR_SSHIFT | OCTOSPI_TCR_DHQC,
               hospi1.Init.SampleShifting);

    /* In standard SPI these board pins are /WP and /HOLD, not data. Keep them
     * high without relying on external pull resistors; never alter QE/status. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3 | GPIO_PIN_6, GPIO_PIN_SET);
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_3 | GPIO_PIN_6;
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &pins);
    __HAL_OSPI_ENABLE(&hospi1);
    return transfer_result(HAL_OSPI_SetTimeout(&hospi1, SRNOS_NOR_BOARD_TRANSFER_TIMEOUT_MS));
}

SRNOS_NORStatus SRNOS_NORBoardTransfer(uint8_t instruction, bool addressed,
                                     uint32_t address, const void *tx,
                                     void *rx, size_t length)
{
    if (!SRNOS_NORBoardIsOwner() || length > SRNOS_NOR_PAGE_BYTES ||
        (addressed && address >= SRNOS_NOR_SIZE_BYTES) ||
        (tx != NULL && rx != NULL) ||
        (length == 0U ? tx != NULL || rx != NULL : tx == NULL && rx == NULL))
        return SRNOS_NOR_INVALID;
    if (transport_failed) return SRNOS_NOR_IO_ERROR;
    OSPI_RegularCmdTypeDef command = {0};
    command.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    command.FlashId = HAL_OSPI_FLASH_ID_1;
    command.Instruction = instruction;
    command.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    command.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    command.AddressMode = addressed ? HAL_OSPI_ADDRESS_1_LINE : HAL_OSPI_ADDRESS_NONE;
    command.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
    command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = length != 0U ? HAL_OSPI_DATA_1_LINE : HAL_OSPI_DATA_NONE;
    command.NbData = (uint32_t)length;
    command.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
    command.DummyCycles = 0U;
    command.DQSMode = HAL_OSPI_DQS_DISABLE;
    command.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    SRNOS_NORStatus result = transfer_result(HAL_OSPI_Command(&hospi1, &command,
                                                           SRNOS_NOR_BOARD_TRANSFER_TIMEOUT_MS));
    if (result == SRNOS_NOR_OK && rx != NULL)
        result = transfer_result(HAL_OSPI_Receive(&hospi1, rx, SRNOS_NOR_BOARD_TRANSFER_TIMEOUT_MS));
    if (result == SRNOS_NOR_OK && tx != NULL)
        result = transfer_result(HAL_OSPI_Transmit(&hospi1, (uint8_t *)tx,
                                                 SRNOS_NOR_BOARD_TRANSFER_TIMEOUT_MS));
    return result;
}

uint32_t SRNOS_NORBoardTimeMs(void)
{
    return HAL_GetTick();
}

SRNOS_NORStatus SRNOS_NORBoardYield(void)
{
    if (!SRNOS_NORBoardIsOwner()) return SRNOS_NOR_INVALID;
    return osDelay(1U) == osOK ? SRNOS_NOR_OK : SRNOS_NOR_IO_ERROR;
}
