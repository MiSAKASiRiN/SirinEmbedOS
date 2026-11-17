#ifndef SRNOS_RS485_ROUTER_H
#define SRNOS_RS485_ROUTER_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_RS485_ROUTER_PORT_COUNT        (1u)
#define SRNOS_RS485_ROUTER_PORT_UART7        (1u)
#define SRNOS_RS485_ROUTER_INVALID_HANDLE    (0u)

enum {
    SRNOS_RS485_ROUTER_OK         = 0,
    SRNOS_RS485_ROUTER_ERR_PARAM  = -1,
    SRNOS_RS485_ROUTER_ERR_STATE  = -2,
    SRNOS_RS485_ROUTER_ERR_FULL   = -3,
    SRNOS_RS485_ROUTER_ERR_MODE   = -4,
    SRNOS_RS485_ROUTER_ERR_CRC    = -5,
    SRNOS_RS485_ROUTER_ERR_DUP    = -6
};

typedef enum {
    SRNOS_RS485_ROUTER_PROTO_RAW = 0,
    SRNOS_RS485_ROUTER_PROTO_MODBUS_RTU = 1
} SRNOS_RS485RouterProto;

typedef uint32_t SRNOS_RS485RouterHandle;

typedef struct {
    uint8_t port;
    SRNOS_RS485RouterProto proto;
    const uint8_t *data;
    uint16_t len;
    uint32_t timestamp_ms;
    bool crc_ok;
    uint8_t unit_id;
    uint8_t function_code;
} SRNOS_RS485RouterRxFrame;

typedef bool (*SRNOS_RS485RouterRawMatchFn)(const SRNOS_RS485RouterRxFrame *frame, void *user);
typedef void (*SRNOS_RS485RouterOnRxFn)(SRNOS_RS485RouterHandle handle,
                                      const SRNOS_RS485RouterRxFrame *frame,
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
} SRNOS_RS485RouterStats;

int SRNOS_RS485RouterRegisterRaw(uint8_t port,
                              void *owner,
                              SRNOS_RS485RouterRawMatchFn match,
                              SRNOS_RS485RouterOnRxFn on_rx,
                              void *user,
                              SRNOS_RS485RouterHandle *out_handle);

int SRNOS_RS485RouterRegisterModbus(uint8_t port,
                                 uint8_t unit_id,
                                 void *owner,
                                 SRNOS_RS485RouterOnRxFn on_rx,
                                 void *user,
                                 SRNOS_RS485RouterHandle *out_handle);

int SRNOS_RS485RouterUnregister(SRNOS_RS485RouterHandle handle);

/* Task context after scheduler start. Success copies the request into the
 * queue; timeout_ms limits admission, not the frozen response window. */
int SRNOS_RS485RouterSendRaw(SRNOS_RS485RouterHandle handle,
                          const void *data,
                          uint16_t len,
                          uint32_t timeout_ms);

int SRNOS_RS485RouterSendModbusAdu(SRNOS_RS485RouterHandle handle,
                                 const void *adu,
                                 uint16_t len,
                                 uint32_t timeout_ms);

bool SRNOS_RS485RouterIsReady(uint8_t port);

/* Task-only coherent copy. Invalid port/context/output returns false without
 * modifying out. An unused fixed port has zero statistics. */
bool SRNOS_RS485RouterGetStats(uint8_t port, SRNOS_RS485RouterStats *out);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_RS485_ROUTER_H
