#ifndef H723VG_V2_FREERTOS_LIB_MODBUS_RTU_H
#define H723VG_V2_FREERTOS_LIB_MODBUS_RTU_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/RS485/RS485_Router.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MODBUS_RTU_MAX_ADU_LEN
#define MODBUS_RTU_MAX_ADU_LEN              (256u)
#endif

#ifndef MODBUS_RTU_CLIENT_RX_QUEUE_LEN
#define MODBUS_RTU_CLIENT_RX_QUEUE_LEN      (4u)
#endif

enum {
    MODBUS_RTU_OK             = 0,
    MODBUS_RTU_ERR_PARAM      = -1,
    MODBUS_RTU_ERR_STATE      = -2,
    MODBUS_RTU_ERR_FULL       = -3,
    MODBUS_RTU_ERR_TIMEOUT    = -4,
    MODBUS_RTU_ERR_CRC        = -5,
    MODBUS_RTU_ERR_LENGTH     = -6,
    MODBUS_RTU_ERR_EXCEPTION  = -7,
    MODBUS_RTU_ERR_FUNCTION   = -8
};

typedef enum {
    MODBUS_RTU_FC_READ_COILS              = 0x01u,
    MODBUS_RTU_FC_READ_DISCRETE_INPUTS    = 0x02u,
    MODBUS_RTU_FC_READ_HOLDING_REGISTERS  = 0x03u,
    MODBUS_RTU_FC_READ_INPUT_REGISTERS    = 0x04u,
    MODBUS_RTU_FC_WRITE_SINGLE_COIL       = 0x05u,
    MODBUS_RTU_FC_WRITE_SINGLE_REGISTER   = 0x06u,
    MODBUS_RTU_FC_WRITE_MULTIPLE_COILS    = 0x0Fu,
    MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS = 0x10u
} modbus_rtu_function_t;

typedef enum {
    MODBUS_RTU_EX_ILLEGAL_FUNCTION      = 0x01u,
    MODBUS_RTU_EX_ILLEGAL_DATA_ADDRESS  = 0x02u,
    MODBUS_RTU_EX_ILLEGAL_DATA_VALUE    = 0x03u,
    MODBUS_RTU_EX_SLAVE_DEVICE_FAILURE  = 0x04u,
    MODBUS_RTU_EX_ACKNOWLEDGE           = 0x05u,
    MODBUS_RTU_EX_SLAVE_DEVICE_BUSY     = 0x06u,
    MODBUS_RTU_EX_MEMORY_PARITY_ERROR   = 0x08u,
    MODBUS_RTU_EX_GATEWAY_PATH_UNAVAIL  = 0x0Au,
    MODBUS_RTU_EX_GATEWAY_TARGET_FAILED = 0x0Bu
} modbus_rtu_exception_t;

typedef struct {
    uint8_t  unit_id;
    uint8_t  function_code;
    uint8_t  exception_code;
    bool     crc_ok;
    bool     is_exception;
    uint16_t adu_len;
    uint32_t timestamp_ms;
    uint8_t  adu[MODBUS_RTU_MAX_ADU_LEN];
} modbus_rtu_frame_t;

typedef struct {
    rs485_router_handle_t router_handle;
    uint8_t port;
    uint8_t unit_id;
    volatile uint8_t rx_head;
    volatile uint8_t rx_tail;
    volatile uint8_t rx_count;
    volatile uint32_t rx_frame_count;
    volatile uint32_t rx_overflow_count;
    volatile void *wait_task;
    modbus_rtu_frame_t rx_queue[MODBUS_RTU_CLIENT_RX_QUEUE_LEN];
} modbus_rtu_client_t;

uint16_t modbus_rtu_crc16(const uint8_t *data, uint16_t len);

bool modbus_rtu_adu_is_valid(const uint8_t *adu, uint16_t adu_len);

int modbus_rtu_build_adu(uint8_t unit_id,
                         const uint8_t *pdu,
                         uint16_t pdu_len,
                         uint8_t *out_adu,
                         uint16_t out_capacity,
                         uint16_t *out_adu_len);

void modbus_rtu_client_reset(modbus_rtu_client_t *client);

int modbus_rtu_client_init(modbus_rtu_client_t *client, uint8_t port, uint8_t unit_id);

int modbus_rtu_client_deinit(modbus_rtu_client_t *client);

void modbus_rtu_client_flush_rx(modbus_rtu_client_t *client);

int modbus_rtu_client_pop_response(modbus_rtu_client_t *client, modbus_rtu_frame_t *out_frame);

int modbus_rtu_client_wait_response(modbus_rtu_client_t *client,
                                    modbus_rtu_frame_t *out_frame,
                                    uint32_t timeout_ms);

int modbus_rtu_client_send_pdu(modbus_rtu_client_t *client,
                               const uint8_t *pdu,
                               uint16_t pdu_len,
                               uint32_t timeout_ms);

int modbus_rtu_read_coils(modbus_rtu_client_t *client,
                          uint16_t start_addr,
                          uint16_t quantity,
                          uint32_t timeout_ms);

int modbus_rtu_read_discrete_inputs(modbus_rtu_client_t *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint32_t timeout_ms);

int modbus_rtu_read_holding_registers(modbus_rtu_client_t *client,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      uint32_t timeout_ms);

int modbus_rtu_read_input_registers(modbus_rtu_client_t *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint32_t timeout_ms);

int modbus_rtu_write_single_coil(modbus_rtu_client_t *client,
                                 uint16_t coil_addr,
                                 bool on,
                                 uint32_t timeout_ms);

int modbus_rtu_write_single_register(modbus_rtu_client_t *client,
                                     uint16_t reg_addr,
                                     uint16_t value,
                                     uint32_t timeout_ms);

int modbus_rtu_write_multiple_coils(modbus_rtu_client_t *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    const uint8_t *coil_bytes,
                                    uint16_t coil_bytes_len,
                                    uint32_t timeout_ms);

int modbus_rtu_write_multiple_registers(modbus_rtu_client_t *client,
                                        uint16_t start_addr,
                                        uint16_t quantity,
                                        const uint16_t *values,
                                        uint32_t timeout_ms);

int modbus_rtu_validate_response(const modbus_rtu_client_t *client,
                                 const modbus_rtu_frame_t *frame,
                                 uint8_t expected_function);

int modbus_rtu_parse_read_bits_response(const modbus_rtu_frame_t *frame,
                                        uint8_t expected_function,
                                        uint8_t *out_bytes,
                                        uint16_t out_capacity,
                                        uint16_t *out_byte_count);

int modbus_rtu_parse_read_registers_response(const modbus_rtu_frame_t *frame,
                                             uint8_t expected_function,
                                             uint16_t *out_regs,
                                             uint16_t out_capacity,
                                             uint16_t *out_reg_count);

int modbus_rtu_parse_write_single_response(const modbus_rtu_frame_t *frame,
                                           uint8_t expected_function,
                                           uint16_t *out_addr,
                                           uint16_t *out_value);

int modbus_rtu_parse_write_multiple_response(const modbus_rtu_frame_t *frame,
                                             uint8_t expected_function,
                                             uint16_t *out_start_addr,
                                             uint16_t *out_quantity);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_LIB_MODBUS_RTU_H
