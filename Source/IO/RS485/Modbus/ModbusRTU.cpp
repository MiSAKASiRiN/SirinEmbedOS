#include "SirinEmbedOS/IO/RS485/Modbus/ModbusRTU.h"

#include "FreeRTOS.h"
#include "task.h"

#include <cstring>

namespace {

constexpr uint16_t kModbusMinAduLen = 4u;
constexpr uint16_t kModbusMaxPduLen = SRNOS_MODBUS_RTU_MAX_ADU_LEN - 3u;

static inline TickType_t SRNOS_ModbusMsToTicks(uint32_t timeout_ms)
{
    if (timeout_ms == 0u) {
        return 0u;
    }
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return (ticks == 0u) ? 1u : ticks;
}

static inline void SRNOS_ModbusPutU16Be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static inline uint16_t SRNOS_ModbusGetU16Be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static inline uint16_t SRNOS_ModbusGetU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static void SRNOS_ModbusRouterOnRx(SRNOS_RS485RouterHandle handle,
                                const SRNOS_RS485RouterRxFrame *frame,
                                void *user)
{
    if ((frame == nullptr) || (user == nullptr)) {
        return;
    }

    SRNOS_ModbusRtuClient *client = (SRNOS_ModbusRtuClient *)user;
    if ((frame->len < kModbusMinAduLen) || (frame->len > SRNOS_MODBUS_RTU_MAX_ADU_LEN)) {
        return;
    }

    bool pushed = false;

    taskENTER_CRITICAL();
    if (client->rx_count < SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN) {
        SRNOS_ModbusRtuFrame *dst = &client->rx_queue[client->rx_tail];
        dst->unit_id = frame->data[0];
        dst->function_code = frame->data[1];
        dst->exception_code = ((frame->data[1] & 0x80u) != 0u && frame->len >= 5u) ? frame->data[2] : 0u;
        dst->crc_ok = frame->crc_ok;
        dst->is_exception = ((frame->data[1] & 0x80u) != 0u);
        dst->adu_len = frame->len;
        dst->timestamp_ms = frame->timestamp_ms;
        (void)memcpy(dst->adu, frame->data, frame->len);

        client->rx_tail = (uint8_t)((client->rx_tail + 1u) % SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN);
        client->rx_count++;
        client->rx_frame_count++;
        pushed = true;
    } else {
        client->rx_overflow_count++;
    }

    void *wait_task = (void *)client->wait_task;
    taskEXIT_CRITICAL();

    if (pushed && (wait_task != nullptr)) {
        xTaskNotifyGive((TaskHandle_t)wait_task);
    }

    (void)handle;
}

static int SRNOS_ModbusPopLocked(SRNOS_ModbusRtuClient *client, SRNOS_ModbusRtuFrame *out_frame)
{
    if ((client == nullptr) || (out_frame == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }
    if (client->rx_count == 0u) {
        return SRNOS_MODBUS_RTU_ERR_TIMEOUT;
    }

    *out_frame = client->rx_queue[client->rx_head];
    client->rx_head = (uint8_t)((client->rx_head + 1u) % SRNOS_MODBUS_RTU_CLIENT_RX_QUEUE_LEN);
    client->rx_count--;
    return SRNOS_MODBUS_RTU_OK;
}

static int SRNOS_ModbusSendReadRequest(SRNOS_ModbusRtuClient *client,
                                    uint8_t function_code,
                                    uint16_t start_addr,
                                    uint16_t quantity,
                                    uint16_t quantity_max,
                                    uint32_t timeout_ms)
{
    if ((quantity == 0u) || (quantity > quantity_max)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    uint8_t pdu[5];
    pdu[0] = function_code;
    SRNOS_ModbusPutU16Be(&pdu[1], start_addr);
    SRNOS_ModbusPutU16Be(&pdu[3], quantity);
    return SRNOS_ModbusRtuClientSendPdu(client, pdu, (uint16_t)sizeof(pdu), timeout_ms);
}

static int SRNOS_ModbusValidateBasicFrame(const SRNOS_ModbusRtuFrame *frame, uint8_t expected_function)
{
    if (frame == nullptr) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }
    if ((!frame->crc_ok) || (frame->adu_len < kModbusMinAduLen) || (!SRNOS_ModbusRtuAduIsValid(frame->adu, frame->adu_len))) {
        return SRNOS_MODBUS_RTU_ERR_CRC;
    }
    if (frame->is_exception) {
        return SRNOS_MODBUS_RTU_ERR_EXCEPTION;
    }
    if (frame->function_code != expected_function) {
        return SRNOS_MODBUS_RTU_ERR_FUNCTION;
    }
    return SRNOS_MODBUS_RTU_OK;
}

} // namespace

extern "C" uint16_t SRNOS_ModbusRtuCrc16(const uint8_t *data, uint16_t len)
{
    if (data == nullptr) {
        return 0u;
    }

    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0u; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

extern "C" bool SRNOS_ModbusRtuAduIsValid(const uint8_t *adu, uint16_t adu_len)
{
    if ((adu == nullptr) || (adu_len < kModbusMinAduLen)) {
        return false;
    }

    const uint16_t rx_crc = SRNOS_ModbusGetU16Le(&adu[adu_len - 2u]);
    return SRNOS_ModbusRtuCrc16(adu, (uint16_t)(adu_len - 2u)) == rx_crc;
}

extern "C" int SRNOS_ModbusRtuBuildAdu(uint8_t unit_id,
                                    const uint8_t *pdu,
                                    uint16_t pdu_len,
                                    uint8_t *out_adu,
                                    uint16_t out_capacity,
                                    uint16_t *out_adu_len)
{
    if ((unit_id == 0u) || (pdu == nullptr) || (pdu_len == 0u) || (out_adu == nullptr) || (out_adu_len == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    if ((pdu_len > kModbusMaxPduLen) || ((uint16_t)(pdu_len + 3u) > out_capacity)) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    out_adu[0] = unit_id;
    (void)memcpy(&out_adu[1], pdu, pdu_len);

    const uint16_t crc = SRNOS_ModbusRtuCrc16(out_adu, (uint16_t)(pdu_len + 1u));
    out_adu[pdu_len + 1u] = (uint8_t)(crc & 0xFFu);
    out_adu[pdu_len + 2u] = (uint8_t)(crc >> 8);
    *out_adu_len = (uint16_t)(pdu_len + 3u);
    return SRNOS_MODBUS_RTU_OK;
}

extern "C" void SRNOS_ModbusRtuClientReset(SRNOS_ModbusRtuClient *client)
{
    if (SRNOS_RS485IsFrozen()) return;
    if (client == nullptr) {
        return;
    }
    (void)memset(client, 0, sizeof(*client));
}

extern "C" int SRNOS_ModbusRtuClientInit(SRNOS_ModbusRtuClient *client, uint8_t port, uint8_t unit_id)
{
    if (SRNOS_RS485IsFrozen()) return SRNOS_MODBUS_RTU_ERR_STATE;
    if ((client == nullptr) || (unit_id == 0u)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    SRNOS_ModbusRtuClientReset(client);
    client->port = port;
    client->unit_id = unit_id;

    int ret = SRNOS_RS485RouterRegisterModbus(
            port,
            unit_id,
            client,
            SRNOS_ModbusRouterOnRx,
            client,
            &client->router_handle);
    if (ret != SRNOS_RS485_ROUTER_OK) {
        SRNOS_ModbusRtuClientReset(client);
        return SRNOS_MODBUS_RTU_ERR_STATE;
    }

    return SRNOS_MODBUS_RTU_OK;
}

extern "C" int SRNOS_ModbusRtuClientDeinit(SRNOS_ModbusRtuClient *client)
{
    if (client == nullptr) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    if (client->router_handle != SRNOS_RS485_ROUTER_INVALID_HANDLE) {
        if (SRNOS_RS485RouterUnregister(client->router_handle) != SRNOS_RS485_ROUTER_OK) return SRNOS_MODBUS_RTU_ERR_STATE;
    }
    SRNOS_ModbusRtuClientReset(client);
    return SRNOS_MODBUS_RTU_OK;
}

extern "C" void SRNOS_ModbusRtuClientFlushRx(SRNOS_ModbusRtuClient *client)
{
    if (client == nullptr) {
        return;
    }

    taskENTER_CRITICAL();
    client->rx_head = 0u;
    client->rx_tail = 0u;
    client->rx_count = 0u;
    taskEXIT_CRITICAL();
}

extern "C" int SRNOS_ModbusRtuClientPopResponse(SRNOS_ModbusRtuClient *client, SRNOS_ModbusRtuFrame *out_frame)
{
    if ((client == nullptr) || (out_frame == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    int ret = SRNOS_MODBUS_RTU_ERR_TIMEOUT;
    taskENTER_CRITICAL();
    ret = SRNOS_ModbusPopLocked(client, out_frame);
    taskEXIT_CRITICAL();
    return ret;
}

extern "C" int SRNOS_ModbusRtuClientWaitResponse(SRNOS_ModbusRtuClient *client,
                                               SRNOS_ModbusRtuFrame *out_frame,
                                               uint32_t timeout_ms)
{
    if ((client == nullptr) || (out_frame == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const bool wait_forever = (timeout_ms == UINT32_MAX);
    const TickType_t timeout_ticks = SRNOS_ModbusMsToTicks(timeout_ms);
    const TickType_t start_tick = xTaskGetTickCount();

    for (;;) {
        int pop_ret = SRNOS_MODBUS_RTU_ERR_TIMEOUT;
        taskENTER_CRITICAL();
        pop_ret = SRNOS_ModbusPopLocked(client, out_frame);
        if (pop_ret == SRNOS_MODBUS_RTU_ERR_TIMEOUT) {
            client->wait_task = (void *)xTaskGetCurrentTaskHandle();
        }
        taskEXIT_CRITICAL();

        if (pop_ret == SRNOS_MODBUS_RTU_OK) {
            taskENTER_CRITICAL();
            if (client->wait_task == (void *)xTaskGetCurrentTaskHandle()) {
                client->wait_task = nullptr;
            }
            taskEXIT_CRITICAL();
            return SRNOS_MODBUS_RTU_OK;
        }

        TickType_t wait_ticks = timeout_ticks;
        if (!wait_forever) {
            const TickType_t elapsed = xTaskGetTickCount() - start_tick;
            if (elapsed >= timeout_ticks) {
                taskENTER_CRITICAL();
                if (client->wait_task == (void *)xTaskGetCurrentTaskHandle()) {
                    client->wait_task = nullptr;
                }
                taskEXIT_CRITICAL();
                return SRNOS_MODBUS_RTU_ERR_TIMEOUT;
            }
            wait_ticks = timeout_ticks - elapsed;
        } else if (wait_ticks == 0u) {
            wait_ticks = portMAX_DELAY;
        }

        if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0u) {
            taskENTER_CRITICAL();
            if (client->wait_task == (void *)xTaskGetCurrentTaskHandle()) {
                client->wait_task = nullptr;
            }
            taskEXIT_CRITICAL();
            return SRNOS_MODBUS_RTU_ERR_TIMEOUT;
        }
    }
}

extern "C" int SRNOS_ModbusRtuClientSendPdu(SRNOS_ModbusRtuClient *client,
                                          const uint8_t *pdu,
                                          uint16_t pdu_len,
                                          uint32_t timeout_ms)
{
    if ((client == nullptr) || (client->router_handle == SRNOS_RS485_ROUTER_INVALID_HANDLE)) {
        return SRNOS_MODBUS_RTU_ERR_STATE;
    }

    uint8_t adu[SRNOS_MODBUS_RTU_MAX_ADU_LEN];
    uint16_t adu_len = 0u;
    int build_ret = SRNOS_ModbusRtuBuildAdu(client->unit_id, pdu, pdu_len, adu, (uint16_t)sizeof(adu), &adu_len);
    if (build_ret != SRNOS_MODBUS_RTU_OK) {
        return build_ret;
    }

    const int router_ret = SRNOS_RS485RouterSendModbusAdu(client->router_handle, adu, adu_len, timeout_ms);
    return (router_ret == SRNOS_RS485_ROUTER_OK) ? SRNOS_MODBUS_RTU_OK : SRNOS_MODBUS_RTU_ERR_STATE;
}

extern "C" int SRNOS_ModbusRtuReadCoils(SRNOS_ModbusRtuClient *client,
                                     uint16_t start_addr,
                                     uint16_t quantity,
                                     uint32_t timeout_ms)
{
    return SRNOS_ModbusSendReadRequest(client, SRNOS_MODBUS_RTU_FC_READ_COILS, start_addr, quantity, 2000u, timeout_ms);
}

extern "C" int SRNOS_ModbusRtuReadDiscreteInputs(SRNOS_ModbusRtuClient *client,
                                               uint16_t start_addr,
                                               uint16_t quantity,
                                               uint32_t timeout_ms)
{
    return SRNOS_ModbusSendReadRequest(client, SRNOS_MODBUS_RTU_FC_READ_DISCRETE_INPUTS, start_addr, quantity, 2000u, timeout_ms);
}

extern "C" int SRNOS_ModbusRtuReadHoldingRegisters(SRNOS_ModbusRtuClient *client,
                                                 uint16_t start_addr,
                                                 uint16_t quantity,
                                                 uint32_t timeout_ms)
{
    return SRNOS_ModbusSendReadRequest(client, SRNOS_MODBUS_RTU_FC_READ_HOLDING_REGISTERS, start_addr, quantity, 125u, timeout_ms);
}

extern "C" int SRNOS_ModbusRtuReadInputRegisters(SRNOS_ModbusRtuClient *client,
                                               uint16_t start_addr,
                                               uint16_t quantity,
                                               uint32_t timeout_ms)
{
    return SRNOS_ModbusSendReadRequest(client, SRNOS_MODBUS_RTU_FC_READ_INPUT_REGISTERS, start_addr, quantity, 125u, timeout_ms);
}

extern "C" int SRNOS_ModbusRtuWriteSingleCoil(SRNOS_ModbusRtuClient *client,
                                            uint16_t coil_addr,
                                            bool on,
                                            uint32_t timeout_ms)
{
    uint8_t pdu[5];
    pdu[0] = SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_COIL;
    SRNOS_ModbusPutU16Be(&pdu[1], coil_addr);
    SRNOS_ModbusPutU16Be(&pdu[3], on ? 0xFF00u : 0x0000u);
    return SRNOS_ModbusRtuClientSendPdu(client, pdu, (uint16_t)sizeof(pdu), timeout_ms);
}

extern "C" int SRNOS_ModbusRtuWriteSingleRegister(SRNOS_ModbusRtuClient *client,
                                                uint16_t reg_addr,
                                                uint16_t value,
                                                uint32_t timeout_ms)
{
    uint8_t pdu[5];
    pdu[0] = SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_REGISTER;
    SRNOS_ModbusPutU16Be(&pdu[1], reg_addr);
    SRNOS_ModbusPutU16Be(&pdu[3], value);
    return SRNOS_ModbusRtuClientSendPdu(client, pdu, (uint16_t)sizeof(pdu), timeout_ms);
}

extern "C" int SRNOS_ModbusRtuWriteMultipleCoils(SRNOS_ModbusRtuClient *client,
                                               uint16_t start_addr,
                                               uint16_t quantity,
                                               const uint8_t *coil_bytes,
                                               uint16_t coil_bytes_len,
                                               uint32_t timeout_ms)
{
    if ((client == nullptr) || (coil_bytes == nullptr) || (quantity == 0u) || (quantity > 1968u)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const uint16_t expected_byte_count = (uint16_t)((quantity + 7u) / 8u);
    if (coil_bytes_len < expected_byte_count) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }
    if ((uint16_t)(6u + expected_byte_count) > kModbusMaxPduLen) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    uint8_t pdu[1u + 2u + 2u + 1u + 246u] = {};
    pdu[0] = SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_COILS;
    SRNOS_ModbusPutU16Be(&pdu[1], start_addr);
    SRNOS_ModbusPutU16Be(&pdu[3], quantity);
    pdu[5] = (uint8_t)expected_byte_count;
    (void)memcpy(&pdu[6], coil_bytes, expected_byte_count);
    return SRNOS_ModbusRtuClientSendPdu(client, pdu, (uint16_t)(6u + expected_byte_count), timeout_ms);
}

extern "C" int SRNOS_ModbusRtuWriteMultipleRegisters(SRNOS_ModbusRtuClient *client,
                                                   uint16_t start_addr,
                                                   uint16_t quantity,
                                                   const uint16_t *values,
                                                   uint32_t timeout_ms)
{
    if ((client == nullptr) || (values == nullptr) || (quantity == 0u) || (quantity > 123u)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const uint16_t byte_count = (uint16_t)(quantity * 2u);
    if ((uint16_t)(6u + byte_count) > kModbusMaxPduLen) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    uint8_t pdu[1u + 2u + 2u + 1u + 246u] = {};
    pdu[0] = SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS;
    SRNOS_ModbusPutU16Be(&pdu[1], start_addr);
    SRNOS_ModbusPutU16Be(&pdu[3], quantity);
    pdu[5] = (uint8_t)byte_count;
    for (uint16_t i = 0u; i < quantity; ++i) {
        SRNOS_ModbusPutU16Be(&pdu[6u + i * 2u], values[i]);
    }
    return SRNOS_ModbusRtuClientSendPdu(client, pdu, (uint16_t)(6u + byte_count), timeout_ms);
}

extern "C" int SRNOS_ModbusRtuValidateResponse(const SRNOS_ModbusRtuClient *client,
                                            const SRNOS_ModbusRtuFrame *frame,
                                            uint8_t expected_function)
{
    if ((client == nullptr) || (frame == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }
    if (frame->unit_id != client->unit_id) {
        return SRNOS_MODBUS_RTU_ERR_STATE;
    }
    return SRNOS_ModbusValidateBasicFrame(frame, expected_function);
}

extern "C" int SRNOS_ModbusRtuParseReadBitsResponse(const SRNOS_ModbusRtuFrame *frame,
                                                   uint8_t expected_function,
                                                   uint8_t *out_bytes,
                                                   uint16_t out_capacity,
                                                   uint16_t *out_byte_count)
{
    if ((frame == nullptr) || (out_bytes == nullptr) || (out_byte_count == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const int ret = SRNOS_ModbusValidateBasicFrame(frame, expected_function);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        return ret;
    }
    if (frame->adu_len < 6u) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    const uint8_t byte_count = frame->adu[2];
    if ((uint16_t)(byte_count + 5u) != frame->adu_len) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }
    if (out_capacity < byte_count) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    (void)memcpy(out_bytes, &frame->adu[3], byte_count);
    *out_byte_count = byte_count;
    return SRNOS_MODBUS_RTU_OK;
}

extern "C" int SRNOS_ModbusRtuParseReadRegistersResponse(const SRNOS_ModbusRtuFrame *frame,
                                                        uint8_t expected_function,
                                                        uint16_t *out_regs,
                                                        uint16_t out_capacity,
                                                        uint16_t *out_reg_count)
{
    if ((frame == nullptr) || (out_regs == nullptr) || (out_reg_count == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const int ret = SRNOS_ModbusValidateBasicFrame(frame, expected_function);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        return ret;
    }
    if (frame->adu_len < 6u) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    const uint8_t byte_count = frame->adu[2];
    if ((byte_count == 0u) || ((byte_count & 0x01u) != 0u)) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }
    if ((uint16_t)(byte_count + 5u) != frame->adu_len) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    const uint16_t reg_count = (uint16_t)(byte_count / 2u);
    if (out_capacity < reg_count) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    for (uint16_t i = 0u; i < reg_count; ++i) {
        out_regs[i] = SRNOS_ModbusGetU16Be(&frame->adu[3u + i * 2u]);
    }
    *out_reg_count = reg_count;
    return SRNOS_MODBUS_RTU_OK;
}

extern "C" int SRNOS_ModbusRtuParseWriteSingleResponse(const SRNOS_ModbusRtuFrame *frame,
                                                      uint8_t expected_function,
                                                      uint16_t *out_addr,
                                                      uint16_t *out_value)
{
    if ((frame == nullptr) || (out_addr == nullptr) || (out_value == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const int ret = SRNOS_ModbusValidateBasicFrame(frame, expected_function);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        return ret;
    }
    if (frame->adu_len != 8u) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    *out_addr = SRNOS_ModbusGetU16Be(&frame->adu[2]);
    *out_value = SRNOS_ModbusGetU16Be(&frame->adu[4]);
    return SRNOS_MODBUS_RTU_OK;
}

extern "C" int SRNOS_ModbusRtuParseWriteMultipleResponse(const SRNOS_ModbusRtuFrame *frame,
                                                        uint8_t expected_function,
                                                        uint16_t *out_start_addr,
                                                        uint16_t *out_quantity)
{
    if ((frame == nullptr) || (out_start_addr == nullptr) || (out_quantity == nullptr)) {
        return SRNOS_MODBUS_RTU_ERR_PARAM;
    }

    const int ret = SRNOS_ModbusValidateBasicFrame(frame, expected_function);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        return ret;
    }
    if (frame->adu_len != 8u) {
        return SRNOS_MODBUS_RTU_ERR_LENGTH;
    }

    *out_start_addr = SRNOS_ModbusGetU16Be(&frame->adu[2]);
    *out_quantity = SRNOS_ModbusGetU16Be(&frame->adu[4]);
    return SRNOS_MODBUS_RTU_OK;
}
