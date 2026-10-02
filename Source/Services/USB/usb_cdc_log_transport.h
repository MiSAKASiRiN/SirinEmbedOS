#ifndef SRNOS_USB_CDC_LOG_TRANSPORT_H
#define SRNOS_USB_CDC_LOG_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SRNOS_USBCdcLogTransportInit(void);
bool SRNOS_USBCdcLogSubmit(void *user,
                            const uint8_t *data,
                            size_t length);
void SRNOS_USBCdcLogService(void *user);
bool SRNOS_USBCdcLogIsConnected(void);

void SRNOS_USBCdcLogOnReceive(uint32_t length);
void SRNOS_USBCdcLogOnTransmitComplete(void);
void SRNOS_USBCdcLogOnDisconnect(void);
void SRNOS_USBCdcLogOnControlLineState(uint16_t state);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_USB_CDC_LOG_TRANSPORT_H */
