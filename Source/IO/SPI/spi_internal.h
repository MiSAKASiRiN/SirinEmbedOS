#pragma once
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/IO/SPI/SPI.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
struct SRNOS_SPIDevice
{
    struct SRNOS_SPIBus *bus;
    GPIO_TypeDef       *cs_port;
    uint16_t            cs_pin;

    uint32_t            max_speed_hz;
    uint8_t             mode;
    uint8_t             bits_per_word;
    uint8_t             imu_priority; /* Fixed at device initialization. */
    uint8_t             can_priority; /* MCP adapter only; fixed before first transfer. */
    SRNOS_SPIStatistics statistics;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Private, synchronous device sequence. prepare runs on the SPI dispatcher
 * after each completed transaction (first call runs on the caller). It may
 * only decode/prepare bounded device bytes: no blocking, routing or callbacks.
 * Return 0 to finish, -1 to fail, or 1..74 bytes for the next transaction.
 * completed is 0..4; at 4 only termination is permitted. RX remains owned
 * until this call returns, including timeout/cancellation acknowledgement. */
#define SRNOS_SPI_SEQUENCE_MAX_STEPS 4u
#define SRNOS_SPI_SEQUENCE_MAX_BYTES 74u
typedef int (*SRNOS_SPISequencePrepare)(void *ctx, unsigned completed,
                                      uint8_t *tx, uint8_t **rx);
SRNOS_SPIStatus SRNOS_SPITransferSequence(SRNOS_SPIDevice *dev,
    SRNOS_SPISequencePrepare prepare, void *ctx);
void SRNOS_SPIInitPre(void);
void SRNOS_SPIInitRtos(void);
SRNOS_SPIStatus SRNOS_SPIDeviceInit(SRNOS_SPIDevice *dev,
                                     SRNOS_SPIBusId bus_id,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin);
#ifdef __cplusplus
}
#endif
