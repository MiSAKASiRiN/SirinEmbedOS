#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"

/* Reference: prototype Tasks/Config/omx_uart_router_config.h.
 * UART8 is Referee; UART9 is VT03 (not the reverse). No H12H binding.
 * The SDK defaults are supplied before this hook; this example changes only
 * connector usage and leaves fixed CAN/IMU bring-up with the SDK. */
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->serial[0] = (SRNOS_SerialConfig){
        .port=SRNOS_SERIAL_USART6, .baud_rate=100000, .data_bits=8,
        .stop_bits=2, .parity=SRNOS_SERIAL_PARITY_EVEN,
        .mode=SRNOS_SERIAL_DUPLEX, .rx_enabled=true, .rx_inverted=true,
    };
    config->serial[1] = (SRNOS_SerialConfig){
        .port=SRNOS_SERIAL_UART8, .baud_rate=115200, .data_bits=8,
        .stop_bits=1, .parity=SRNOS_SERIAL_PARITY_NONE,
        .mode=SRNOS_SERIAL_DUPLEX, .rx_enabled=true, .tx_enabled=true,
    };
    config->serial[2] = (SRNOS_SerialConfig){
        .port=SRNOS_SERIAL_UART9, .baud_rate=921600, .data_bits=8,
        .stop_bits=1, .parity=SRNOS_SERIAL_PARITY_NONE,
        .mode=SRNOS_SERIAL_DUPLEX, .rx_enabled=true,
    };
    config->serial[3] = (SRNOS_SerialConfig){
        .port=SRNOS_SERIAL_USART1, .baud_rate=115200, .data_bits=8,
        .stop_bits=1, .parity=SRNOS_SERIAL_PARITY_NONE,
        .mode=SRNOS_SERIAL_DUPLEX, .tx_enabled=true,
    };
    config->serial_count=4;
    config->remote.dr16_port=SRNOS_SERIAL_USART6;
    config->remote.vt03_port=SRNOS_SERIAL_UART9;
    config->referee_port=SRNOS_SERIAL_UART8;
    config->logging_enabled=true;
    config->log_port=SRNOS_SERIAL_USART1;
    config->log_backends|=SRN_LOG_BACKEND_UART;
    /* UART7 is a fixed RS485 connector. Add it explicitly and declare bus
     * endpoints in config->bind_channels when an application needs them.
     * This example registers/enables no external actuator. */
}
