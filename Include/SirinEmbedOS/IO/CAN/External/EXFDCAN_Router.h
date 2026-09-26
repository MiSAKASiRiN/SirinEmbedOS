//
// EXFDCAN_Router.h - MCP2518FD backend for the shared CAN router
//
#ifndef H723VG_V2_FREERTOS_EXFDCAN_ROUTER_H
#define H723VG_V2_FREERTOS_EXFDCAN_ROUTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXFDCAN_MAX_DATA_BYTES   (8u)

typedef struct
{
    uint16_t sid;
    uint8_t  dlc;
    uint8_t  data[EXFDCAN_MAX_DATA_BYTES];
    uint32_t timestamp_ms;
} exfdcan_rx_frame_t;

typedef omx_dbg_exfdcan_t exfdcan_stats_t;

// Debug stats array for debugger watch (index = device id).
extern volatile exfdcan_stats_t g_omx_dbg_exfdcan[];

// Called from HAL_GPIO_EXTI_Callback; only sets flags.
void exfdcan_on_exti(uint16_t gpio_pin);

// Optional readout helpers for debugging.
const volatile exfdcan_stats_t* exfdcan_get_stats(uint8_t dev);
size_t exfdcan_rx_read(uint8_t dev, exfdcan_rx_frame_t *out, size_t max);

can_channel_t exfdcan_channel_from_dev(uint8_t dev);
int exfdcan_dev_from_channel(can_channel_t ch);

// Router task entries (argument: device index).
void Start_EXFDCAN_RxRouter(void *argument);
void Start_EXFDCAN_TxRouter(void *argument);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_EXFDCAN_ROUTER_H
