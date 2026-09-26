#ifndef H723VG_V2_FREERTOS_RS485_ROUTER_H
#define H723VG_V2_FREERTOS_RS485_ROUTER_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS485_ROUTER_PORT_COUNT        (1u)
#define RS485_ROUTER_PORT_UART7        (1u)
#define RS485_ROUTER_INVALID_HANDLE    (0u)

enum {
    RS485_ROUTER_OK         = 0,
    RS485_ROUTER_ERR_PARAM  = -1,
    RS485_ROUTER_ERR_STATE  = -2,
    RS485_ROUTER_ERR_FULL   = -3,
    RS485_ROUTER_ERR_MODE   = -4,
    RS485_ROUTER_ERR_CRC    = -5,
    RS485_ROUTER_ERR_DUP    = -6
};

typedef enum {
    RS485_ROUTER_PROTO_RAW = 0,
    RS485_ROUTER_PROTO_MODBUS_RTU = 1
} rs485_router_proto_t;

typedef uint32_t rs485_router_handle_t;

typedef struct {
    uint8_t port;
    rs485_router_proto_t proto;
    const uint8_t *data;
    uint16_t len;
    uint32_t timestamp_ms;
    bool crc_ok;
    uint8_t unit_id;
    uint8_t function_code;
} rs485_router_rx_frame_t;

typedef bool (*rs485_router_raw_match_fn)(const rs485_router_rx_frame_t *frame, void *user);
typedef void (*rs485_router_on_rx_fn)(rs485_router_handle_t handle,
                                      const rs485_router_rx_frame_t *frame,
                                      void *user);

typedef struct {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t rx_crc_error;
    uint32_t rx_unhandled;
    uint32_t tx_busy;
    uint32_t uart_errors;
} rs485_router_stats_t;

extern volatile rs485_router_stats_t g_rs485_router_stats[RS485_ROUTER_PORT_COUNT];

int rs485_router_register_raw(uint8_t port,
                              void *owner,
                              rs485_router_raw_match_fn match,
                              rs485_router_on_rx_fn on_rx,
                              void *user,
                              rs485_router_handle_t *out_handle);

int rs485_router_register_modbus(uint8_t port,
                                 uint8_t unit_id,
                                 void *owner,
                                 rs485_router_on_rx_fn on_rx,
                                 void *user,
                                 rs485_router_handle_t *out_handle);

int rs485_router_unregister(rs485_router_handle_t handle);

int rs485_router_send_raw(rs485_router_handle_t handle,
                          const void *data,
                          uint16_t len,
                          uint32_t timeout_ms);

int rs485_router_send_modbus_adu(rs485_router_handle_t handle,
                                 const void *adu,
                                 uint16_t len,
                                 uint32_t timeout_ms);

bool rs485_router_is_ready(uint8_t port);

const rs485_router_stats_t *rs485_router_get_stats(uint8_t port);

int rs485_router_force_rearm_rx(uint8_t port);

/* Register endpoints before startup; registrations freeze even when no endpoint exists.
 * Queue timeout is supplied per send, response timeout is frozen here. */
int SRNOS_RS485Start(uint32_t response_timeout_ms);
bool SRNOS_RS485IsFrozen(void);
void TSKPTT_RS485_Router_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_RS485_ROUTER_H
