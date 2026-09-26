#ifndef OMX_USB_CDC_LOG_TRANSPORT_H
#define OMX_USB_CDC_LOG_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void omx_usb_cdc_log_transport_init(void);
bool omx_usb_cdc_log_submit(void *user,
                            const uint8_t *data,
                            size_t length);
void omx_usb_cdc_log_service(void *user);
bool omx_usb_cdc_log_is_connected(void);

void omx_usb_cdc_log_on_receive(uint32_t length);
void omx_usb_cdc_log_on_transmit_complete(void);
void omx_usb_cdc_log_on_disconnect(void);
void omx_usb_cdc_log_on_control_line_state(uint16_t state);

#ifdef __cplusplus
}
#endif

#endif /* OMX_USB_CDC_LOG_TRANSPORT_H */
