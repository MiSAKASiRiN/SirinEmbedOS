#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
#define SRNOS_SERIAL_RX_DMA_SIZE 256u
#define SRNOS_SERIAL_WORKER_STACK_BYTES 2048u
#define SRNOS_SERIAL_WORKER_RESERVE_BYTES 1024u
#define SRNOS_SERIAL_CHANNEL_HEAP_RESERVE_BYTES 2560u
uint32_t SRNOS_SerialHeapBudget(size_t channel_count);
/* Board owns DMA buffers and HAL; callbacks provide already coherent spans. */
const SRNOS_SerialConfig *SRNOS_SerialBoardConfig(SRNOS_SerialPortId);
int SRNOS_SerialBoardIsInISR(void);
int SRNOS_SerialBoardStart(const SRNOS_SerialConfig *);
int SRNOS_SerialBoardTransmit(SRNOS_SerialPortId, const uint8_t *, size_t);
int SRNOS_SerialBoardRecover(SRNOS_SerialPortId);
void SRNOS_SerialBoardAbortTx(SRNOS_SerialPortId);
void SRNOS_SerialOnRx(SRNOS_SerialPortId, const uint8_t *, size_t);
void SRNOS_SerialOnTxComplete(SRNOS_SerialPortId);
void SRNOS_SerialOnError(SRNOS_SerialPortId);
/* Cursor is normalized to [0,size); duplicate IDLE/TC positions emit nothing.
 * HT must remain enabled so a complete lap cannot be invisible. */
size_t SRNOS_SerialDmaDelta(size_t *cursor, size_t position, size_t size);

typedef struct { size_t position; bool wrap_accounted; } SRNOS_SerialDmaCursor;
size_t SRNOS_SerialDmaAdvance(SRNOS_SerialDmaCursor *,size_t position,size_t capacity,bool complete);
