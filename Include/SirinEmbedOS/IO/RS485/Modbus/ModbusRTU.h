#ifndef SRNOS_MODBUS_RTU_H
#define SRNOS_MODBUS_RTU_H

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/RS485/RS485.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_MODBUS_RTU_MAX_ADU_LEN
#define SRNOS_MODBUS_RTU_MAX_ADU_LEN              (256u)
#endif

#ifndef SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN
#define SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN      (4u)
#endif

enum {
    SRNOS_MODBUS_RTU_OK             = 0,
    SRNOS_MODBUS_RTU_ERR_PARAM      = -1,
    SRNOS_MODBUS_RTU_ERR_STATE      = -2,
    SRNOS_MODBUS_RTU_ERR_FULL       = -3,
    SRNOS_MODBUS_RTU_ERR_TIMEOUT    = -4,
    SRNOS_MODBUS_RTU_ERR_CRC        = -5,
    SRNOS_MODBUS_RTU_ERR_LENGTH     = -6,
    SRNOS_MODBUS_RTU_ERR_EXCEPTION  = -7,
    SRNOS_MODBUS_RTU_ERR_FUNCTION   = -8
};

typedef enum {
    SRNOS_MODBUS_RTU_FC_READ_COILS              = 0x01u,
    SRNOS_MODBUS_RTU_FC_READ_DISCRETE_INPUTS    = 0x02u,
    SRNOS_MODBUS_RTU_FC_READ_HOLDING_REGISTERS  = 0x03u,
    SRNOS_MODBUS_RTU_FC_READ_INPUT_REGISTERS    = 0x04u,
    SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_COIL       = 0x05u,
    SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_REGISTER   = 0x06u,
    SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_COILS    = 0x0Fu,
    SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS = 0x10u
} SRNOS_ModbusRtuFunction;

typedef enum {
    SRNOS_MODBUS_RTU_EX_ILLEGAL_FUNCTION      = 0x01u,
    SRNOS_MODBUS_RTU_EX_ILLEGAL_DATA_ADDRESS  = 0x02u,
    SRNOS_MODBUS_RTU_EX_ILLEGAL_DATA_VALUE    = 0x03u,
    SRNOS_MODBUS_RTU_EX_SLAVE_DEVICE_FAILURE  = 0x04u,
    SRNOS_MODBUS_RTU_EX_ACKNOWLEDGE           = 0x05u,
    SRNOS_MODBUS_RTU_EX_SLAVE_DEVICE_BUSY     = 0x06u,
    SRNOS_MODBUS_RTU_EX_MEMORY_PARITY_ERROR   = 0x08u,
    SRNOS_MODBUS_RTU_EX_GATEWAY_PATH_UNAVAIL  = 0x0Au,
    SRNOS_MODBUS_RTU_EX_GATEWAY_TARGET_FAILED = 0x0Bu
} SRNOS_ModbusRtuException;

typedef struct {
    uint8_t  unit_id;
    uint8_t  function_code;
    uint8_t  exception_code;
    bool     crc_ok;
    bool     is_exception;
    uint16_t adu_len;
    uint32_t timestamp_ms;
    uint8_t  adu[SRNOS_MODBUS_RTU_MAX_ADU_LEN];
} SRNOS_ModbusRtuFrame;

typedef struct {
    SRNOS_RS485RouterHandle router_handle;
    uint8_t port;
    uint8_t unit_id;
    volatile uint8_t rx_head;
    volatile uint8_t rx_tail;
    volatile uint8_t rx_count;
    volatile uint32_t rx_frame_count;
    volatile uint32_t rx_overflow_count;
    volatile void *wait_task;
    SRNOS_ModbusRtuFrame rx_queue[SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN];
} SRNOS_ModbusRtuClient;

uint16_t SRNOS_ModbusRtuCrc16(const uint8_t *data, uint16_t len);

bool SRNOS_ModbusRtuAduIsValid(const uint8_t *adu, uint16_t adu_len);

int SRNOS_ModbusRtuBuildAdu(uint8_t unit_id,
                         const uint8_t *pdu,
                         uint16_t pdu_len,
                         uint8_t *out_adu,
                         uint16_t out_capacity,
                         uint16_t *out_adu_len);

void SRNOS_ModbusRtuClientReset(SRNOS_ModbusRtuClient *client);

int SRNOS_ModbusRtuClientInit(SRNOS_ModbusRtuClient *client, uint8_t port, uint8_t unit_id);

int SRNOS_ModbusRtuClientDeinit(SRNOS_ModbusRtuClient *client);

void SRNOS_ModbusRtuClientFlushRx(SRNOS_ModbusRtuClient *client);

int SRNOS_ModbusRtuClientPopResponse(SRNOS_ModbusRtuClient *client, SRNOS_ModbusRtuFrame *out_frame);

int SRNOS_ModbusRtuClientWaitResponse(SRNOS_ModbusRtuClient *client,
                                    SRNOS_ModbusRtuFrame *out_frame,
                                    uint32_t timeout_ms);

int SRNOS_ModbusRtuClientSendPdu(SRNOS_ModbusRtuClient *client,
                               const uint8_t *pdu,
                               uint16_t pdu_len,
                               uint32_t timeout_ms);

int SRNOS_ModbusRtuReadCoils(SRNOS_ModbusRtuClient *client,
                          uint16_t start_addr,
                          uint16_t quantity,
                          uint32_t timeout_ms);

int SRNOS_ModbusRtuReadDiscreteInputs(SRNOS_ModbusRtuClient *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint32_t timeout_ms);

int SRNOS_ModbusRtuReadHoldingRegisters(SRNOS_ModbusRtuClient *client,
                                      uint16_t start_addr,
                                      uint16_t quantity,
                                      uint32_t timeout_ms);

int SRNOS_ModbusRtuReadInputRegisters(SRNOS_ModbusRtuClient *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint32_t timeout_ms);

int SRNOS_ModbusRtuWriteSingleCoil(SRNOS_ModbusRtuClient *client,
                                 uint16_t coil_addr,
                                 bool on,
                                 uint32_t timeout_ms);

int SRNOS_ModbusRtuWriteSingleRegister(SRNOS_ModbusRtuClient *client,
                                     uint16_t reg_addr,
                                     uint16_t value,
                                     uint32_t timeout_ms);

int SRNOS_ModbusRtuWriteMultipleCoils(SRNOS_ModbusRtuClient *client,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    const uint8_t *coil_bytes,
                                    uint16_t coil_bytes_len,
                                    uint32_t timeout_ms);

int SRNOS_ModbusRtuWriteMultipleRegisters(SRNOS_ModbusRtuClient *client,
                                        uint16_t start_addr,
                                        uint16_t quantity,
                                        const uint16_t *values,
                                        uint32_t timeout_ms);

int SRNOS_ModbusRtuValidateResponse(const SRNOS_ModbusRtuClient *client,
                                 const SRNOS_ModbusRtuFrame *frame,
                                 uint8_t expected_function);

int SRNOS_ModbusRtuParseReadBitsResponse(const SRNOS_ModbusRtuFrame *frame,
                                        uint8_t expected_function,
                                        uint8_t *out_bytes,
                                        uint16_t out_capacity,
                                        uint16_t *out_byte_count);

int SRNOS_ModbusRtuParseReadRegistersResponse(const SRNOS_ModbusRtuFrame *frame,
                                             uint8_t expected_function,
                                             uint16_t *out_regs,
                                             uint16_t out_capacity,
                                             uint16_t *out_reg_count);

int SRNOS_ModbusRtuParseWriteSingleResponse(const SRNOS_ModbusRtuFrame *frame,
                                           uint8_t expected_function,
                                           uint16_t *out_addr,
                                           uint16_t *out_value);

int SRNOS_ModbusRtuParseWriteMultipleResponse(const SRNOS_ModbusRtuFrame *frame,
                                             uint8_t expected_function,
                                             uint16_t *out_start_addr,
                                             uint16_t *out_quantity);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_MODBUS_RTU_H
