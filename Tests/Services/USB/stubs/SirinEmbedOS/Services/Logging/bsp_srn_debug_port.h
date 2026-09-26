#ifndef TEST_BSP_SRN_DEBUG_PORT_H
#define TEST_BSP_SRN_DEBUG_PORT_H

#include <stdint.h>

void SRN_DebugPort_OnUsbDisconnect(void);
void SRN_DebugPort_OnUsbControlLineState(uint16_t state);
void SRN_DebugPort_OnUsbReceive(const uint8_t *data, uint32_t length);
void SRN_DebugPort_OnUsbTransmitComplete(void);

#endif
