#include "SirinEmbedOS/Kernel/System.h"
static SRNOS_SerialChannel *raw_channel;
static bool bind_channels(void *) {
    raw_channel = SRNOS_SerialGetChannel(SRNOS_SERIAL_UART9);
    return SRNOS_SerialClaim(raw_channel, &raw_channel) == SRNOS_SERIAL_OK;
}
extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->logging_enabled = false;
    c->host_link_enabled = false;
    c->usb_enabled = false;
    c->load_parameters = false;
    c->serial_count = 1;
    c->serial[0] = {SRNOS_SERIAL_UART9,115200,8,1,SRNOS_SERIAL_PARITY_NONE,SRNOS_SERIAL_DUPLEX,true,true,false};
    c->bind_channels = bind_channels;
}
