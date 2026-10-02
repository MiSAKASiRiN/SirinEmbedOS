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
    SRNOS_SPIStatistics statistics;
};
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_SPIInitPre(void);
void SRNOS_SPIInitRtos(void);
SRNOS_SPIStatus SRNOS_SPIDeviceInit(SRNOS_SPIDevice *dev,
                                     SRNOS_SPIBusId bus_id,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin);
#ifdef __cplusplus
}
#endif
