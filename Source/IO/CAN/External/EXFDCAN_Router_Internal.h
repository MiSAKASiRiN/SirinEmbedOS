//
// EXFDCAN_Router_Internal.h — internal helpers for MCP2518FD routers
//
#ifndef SRNOS_EXFDCAN_ROUTER_INTERNAL_H
#define SRNOS_EXFDCAN_ROUTER_INTERNAL_H

#include "SirinEmbedOS/IO/CAN/External/ExternalCAN.h"
#include "cmsis_os2.h"
#include "IO/can_board.h"
#include "IO/SPI/spi_internal.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_EXTERNAL_CAN_DEV_COUNT
#define SRNOS_EXTERNAL_CAN_DEV_COUNT          (1u)
#endif
#ifndef SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH
#define SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH       (64u)
#endif
#define SRNOS_EXTERNAL_CAN_EVT_RX             (1u << 0)
#define SRNOS_EXTERNAL_CAN_EVT_TX             (1u << 1)
#define SRNOS_EXTERNAL_CAN_EVT_INT            (1u << 2)
#define SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER      (1u << 3)
#define SRNOS_EXTERNAL_CAN_EVT_ANY            (SRNOS_EXTERNAL_CAN_EVT_RX | SRNOS_EXTERNAL_CAN_EVT_TX | SRNOS_EXTERNAL_CAN_EVT_INT | SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER)
#ifndef SRNOS_EXTERNAL_CAN_RX_FIFO
#define SRNOS_EXTERNAL_CAN_RX_FIFO            CAN_FIFO_CH1
#endif
#ifndef SRNOS_EXTERNAL_CAN_TX_FIFO
#define SRNOS_EXTERNAL_CAN_TX_FIFO            CAN_FIFO_CH2
#endif
#ifndef SRNOS_EXTERNAL_CAN_TXF_SIZE
// FSIZE is "size - 1", max 31 -> 32 objects per FIFO
#define SRNOS_EXTERNAL_CAN_TXF_SIZE           (31u)
#endif
#ifndef SRNOS_EXTERNAL_CAN_RXF_SIZE
// FSIZE is "size - 1", max 31 -> 32 objects per FIFO
#define SRNOS_EXTERNAL_CAN_RXF_SIZE           (31u)
#endif



typedef struct
{
    osEventFlagsId_t   evt;
    volatile uint32_t  pending_isr;
    volatile SRNOS_ExternalCANStats *stats;
    SRNOS_ExternalCANRxFrame rx_log[SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH];
    volatile uint16_t  rx_head;
    volatile uint16_t  rx_tail;
    uint8_t            init_done;
    bool              tx_retry_pending; /* TX worker only; transient SPI error. */
    uint32_t          tx_retry_after_ms;
} SRNOS_ExternalCANCtx;

void SRNOS_ExternalCANOnExti(uint16_t gpio_pin);
void SRNOS_ExternalCANRxRouterWorker(void *argument);
void SRNOS_ExternalCANTxRouterWorker(void *argument);
SRNOS_ExternalCANCtx* SRNOS_ExternalCANCtxOf(uint8_t dev);
const SRNOS_ExternalCANDevCfg* SRNOS_ExternalCANCfgOf(uint8_t dev);
int SRNOS_ExternalCANHwInit(uint8_t dev);
void SRNOS_ExternalCANBusLockInit(void);
void SRNOS_ExternalCANBusLock(void);
void SRNOS_ExternalCANBusUnlock(void);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_EXFDCAN_ROUTER_INTERNAL_H
