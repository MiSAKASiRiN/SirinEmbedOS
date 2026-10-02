#ifndef SRNOS_TEST_DEBUG_PORT_H
#define SRNOS_TEST_DEBUG_PORT_H

#include <stdint.h>

void SRNOS_DebugPortOnUsbDisconnect(void);
void SRNOS_DebugPortOnUsbControlLineState(uint16_t state);
void SRNOS_DebugPortOnUsbReceive(const uint8_t *data, uint32_t length);
void SRNOS_DebugPortOnUsbTransmitComplete(void);

#endif
