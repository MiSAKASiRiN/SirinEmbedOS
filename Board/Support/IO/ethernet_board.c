#include "IO/ethernet_board.h"
#include "IO/ethernet_rx.h"
#include "IO/Ethernet/ethernet_internal.h"
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#include "eth.h"
#include "cmsis_os2.h"
#include <string.h>

extern ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
extern ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
#define SRNOS_ETHERNET_RX_BUFFERS (ETH_RX_DESC_CNT + 1u)
#define SRNOS_ETHERNET_DMA_BYTES 1536u
static SRNOS_ETH_DMA_BSS uint8_t rx_data[SRNOS_ETHERNET_RX_BUFFERS][SRNOS_ETHERNET_DMA_BYTES];
static SRNOS_ETH_DMA_BSS uint8_t tx_data[SRNOS_ETHERNET_DMA_BYTES];
static SRNOS_EthernetRxBuffer rx_buffers[SRNOS_ETHERNET_RX_BUFFERS];
static volatile uint32_t fatal_error;
static uint32_t tx_index;
static bool tx_released;

void SRNOS_EthernetBoardMAC(uint8_t mac[6])
{
    const uint32_t uid[] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 4; ++j) {
            hash ^= (uid[i] >> (j * 8)) & 255u;
            hash *= UINT64_C(1099511628211);
        }
    mac[0] = 0x02; /* Locally administered unicast; not a registered OUI. */
    for (unsigned i = 1; i < 6; ++i) mac[i] = (uint8_t)(hash >> ((i - 1) * 8));
}
void SRNOS_EthernetBoardReset(void)
{
    HAL_NVIC_DisableIRQ(ETH_IRQn);
    /* Peripheral reset revokes DMA ownership before any buffer can be reused.
     * Stop_IT alone is insufficient after a fatal DMA error / TX timeout. */
    __HAL_RCC_ETH1MAC_FORCE_RESET();
    __DSB();
    __HAL_RCC_ETH1MAC_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(ETH_IRQn);
    HAL_GPIO_WritePin(LAN_NRST_GPIO_Port, LAN_NRST_Pin, GPIO_PIN_RESET);
    memset(&heth, 0, sizeof(heth));
    memset(rx_buffers, 0, sizeof(rx_buffers));
    fatal_error = 0;
}
bool SRNOS_EthernetBoardInit(uint8_t mac[6], uint32_t *clock_hz)
{
    SRNOS_EthernetBoardReset();
    PLL1_ClocksTypeDef pll;
    HAL_RCCEx_GetPLL1ClockFreq(&pll);
    uint32_t divider = (RCC->CFGR & RCC_CFGR_MCO1PRE) >> RCC_CFGR_MCO1PRE_Pos;
    *clock_hz = divider ? pll.PLL1_Q_Frequency / divider : 0;
    if ((RCC->CFGR & RCC_CFGR_MCO1) != RCC_MCO1SOURCE_PLL1QCLK ||
        !(RCC->PLLCFGR & RCC_PLLCFGR_DIVQ1EN) || *clock_hz != 50000000u)
        return false;
    /* PA8 AF0 is already configured by CubeMX. PE14 reset stays low until
     * MCO is stable; straps latch on this rising edge (nINTSEL must be high).
     * 2 ms reset and 50 ms settle exceed LAN8720A reset timing minima. */
    osDelay(2);
    HAL_GPIO_WritePin(LAN_NRST_GPIO_Port, LAN_NRST_Pin, GPIO_PIN_SET);
    osDelay(50);
    memset(rx_data, 0, sizeof(rx_data));
    memset(tx_data, 0, sizeof(tx_data));
    heth.Instance = ETH;
    heth.Init.MACAddr = mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.RxBuffLen = SRNOS_ETHERNET_DMA_BYTES;
    if (HAL_ETH_Init(&heth) != HAL_OK) return false;
    ETH_MACFilterConfigTypeDef filter = {0};
    filter.ControlPacketsFilter = ETH_CTRLPACKETS_BLOCK_ALL;
    if (HAL_ETH_SetMACFilterConfig(&heth, &filter) != HAL_OK) return false;
    HAL_NVIC_SetPriority(ETH_IRQn, 7, 0); /* Below CAN/IMU (5), UART/MCP (6). */
    return true;
}
bool SRNOS_EthernetBoardReadPHY(uint8_t address, uint8_t reg, uint32_t *value)
{ return HAL_ETH_ReadPHYRegister(&heth, address, reg, value) == HAL_OK; }
bool SRNOS_EthernetBoardWritePHY(uint8_t address, uint8_t reg, uint32_t value)
{ return HAL_ETH_WritePHYRegister(&heth, address, reg, value) == HAL_OK; }
bool SRNOS_EthernetBoardLink(uint16_t speed, bool full)
{
    ETH_MACConfigTypeDef config;
    if (HAL_ETH_GetMACConfig(&heth, &config) != HAL_OK) return false;
    config.Speed = speed == 100 ? ETH_SPEED_100M : ETH_SPEED_10M;
    config.DuplexMode = full ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    config.ChecksumOffload = DISABLE;
    /* Preserve all padding; strip the four FCS bytes ourselves for both
     * Ethernet II and 802.3 length frames. HAL reports total wire length. */
    config.CRCStripTypePacket = DISABLE;
    config.AutomaticPadCRCStrip = DISABLE;
    if (HAL_ETH_SetMACConfig(&heth, &config) != HAL_OK) return false;
    if (HAL_ETH_Start_IT(&heth) != HAL_OK) return false;
    HAL_NVIC_EnableIRQ(ETH_IRQn);
    return true;
}
void HAL_ETH_RxAllocateCallback(uint8_t **buffer)
{
    *buffer = NULL;
    for (unsigned i = 0; i < SRNOS_ETHERNET_RX_BUFFERS; ++i)
        if (!rx_buffers[i].used) {
            rx_buffers[i] = (SRNOS_EthernetRxBuffer){.used = true};
            *buffer = rx_data[i];
            return;
        }
}
void HAL_ETH_RxLinkCallback(void **start, void **end, uint8_t *buffer, uint16_t length)
{
    for (unsigned i = 0; i < SRNOS_ETHERNET_RX_BUFFERS; ++i)
        if (buffer == rx_data[i]) {
            SRNOS_EthernetRxLink(start, end, &rx_buffers[i], length);
            return;
        }
}
int SRNOS_EthernetBoardReceive(SRNOS_EthernetFrame *frame)
{
    SRNOS_EthernetRxBuffer *packet = NULL;
    if (HAL_ETH_ReadData(&heth, (void **)&packet) != HAL_OK) return 0;
    uint32_t errors = 0;
    HAL_ETH_GetRxDataErrorCode(&heth, &errors);
    int result = -1;
    /* Non-jumbo frames fit one descriptor. Always return every linked buffer
     * even when malformed, oversized or split; never hand DMA pointers out. */
    if (packet && !packet->next && !errors && packet->length >= 18u &&
        packet->length <= SRNOS_ETHERNET_FRAME_MAX + 4u) {
        frame->length = packet->length - 4u;
        memcpy(frame->data, rx_data[packet - rx_buffers], frame->length);
        result = 1;
    }
    while (packet) {
        SRNOS_EthernetRxBuffer *next = packet->next;
        packet->used = false;
        packet = next;
    }
    return result;
}
bool SRNOS_EthernetBoardSend(const uint8_t *frame, uint16_t length)
{
    memcpy(tx_data, frame, length);
    ETH_BufferTypeDef buffer = {.buffer = tx_data, .len = length, .next = NULL};
    ETH_TxPacketConfigTypeDef config = {0};
    config.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    config.CRCPadCtrl = ETH_CRC_PAD_INSERT;
    config.Length = length;
    config.TxBuffer = &buffer;
    config.pData = tx_data;
    tx_index = heth.TxDescList.CurTxDesc;
    tx_released = false;
    return HAL_ETH_Transmit_IT(&heth, &config) == HAL_OK;
}
void HAL_ETH_TxFreeCallback(uint32_t *buffer)
{ if (buffer == (uint32_t *)tx_data) tx_released = true; }
int SRNOS_EthernetBoardTxComplete(void)
{
    uint32_t flags = DMATxDscrTab[tx_index].DESC3;
    if (flags & ETH_DMATXNDESCWBF_OWN) return 0;
    HAL_ETH_ReleaseTxPacket(&heth);
    if (!tx_released) return 0;
    return (flags & ETH_DMATXNDESCWBF_ES) ? -1 : 1;
}
uint32_t SRNOS_EthernetBoardError(void) { return fatal_error; }
void ETH_IRQHandler(void) { HAL_ETH_IRQHandler(&heth); }
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handle)
{ if (handle == &heth) SRNOS_EthernetWakeFromISR(); }
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handle)
{ if (handle == &heth) SRNOS_EthernetWakeFromISR(); }
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handle)
{
    if (handle != &heth) return;
    /* RX buffer unavailable is transient; ReadData rearms freed descriptors. */
    if (handle->gState == HAL_ETH_STATE_ERROR ||
        (handle->DMAErrorCode & (ETH_DMACSR_FBE | ETH_DMACSR_CDE)))
        fatal_error = handle->DMAErrorCode | 0x80000000u;
    SRNOS_EthernetWakeFromISR();
}
