//
// Created by sirin on 2025/10/4.
//

#ifndef SRNOS_FDCAN_H
#define SRNOS_FDCAN_H

#pragma once
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "SirinEmbedOS/IO/CAN/FD/CANFD.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Public configuration ===== */
#ifndef SRNOS_FDCAN_RX_RING_SIZE
#define SRNOS_FDCAN_RX_RING_SIZE   128u   /* frames per bus; project may override */
#endif

#ifndef SRNOS_FDCAN_ENABLE_RXFIFO1
#define SRNOS_FDCAN_ENABLE_RXFIFO1 0      /* set 1 if you also use FIFO1 */
#endif

#ifndef SRNOS_FDCAN_BUS_COUNT
#define SRNOS_FDCAN_BUS_COUNT 3u
#endif

/* ===== Frame container for RX side ===== */
typedef struct {
    FDCAN_RxHeaderTypeDef hdr;
    uint8_t               data[64];    /* CAN-FD max */
    uint32_t              ts_us;       /* local timestamp (approx) */
} SRNOS_FDCANRxFrame;

/* ===== Error/diag snapshot ===== */
typedef struct {
    volatile uint32_t rx_isr_cnt;
    volatile uint32_t rx_overrun_cnt;
    volatile uint32_t tx_ok_cnt;
    volatile uint32_t tx_busy_cnt;
    volatile uint32_t bus_off_cnt;
    volatile uint32_t last_err_code;   /* FDCAN_PSR.LEC mirror */
    volatile uint32_t rx_error_cnt;
    volatile uint32_t rx_hw_lost_cnt;
    volatile uint32_t recovery_attempts;
    volatile uint32_t recovery_completed;
    volatile uint32_t recovery_errors;
    volatile uint32_t recovery_faults;
    volatile uint32_t recovery_state; /* 0 healthy, 1 backoff, 2 abort, 3 verify, 4 latched */
} SRNOS_FDCANDiag;

extern volatile SRNOS_FDCANDiag SRNOS_FDCANDiagnostics[SRNOS_FDCAN_BUS_COUNT];

/* ===== API ===== */

/* Call once after CubeMX init and controller configuration freeze. Applies
 * the frozen board timing/RAM profile before Start; a failure remains latched
 * until reset and workers cannot start CubeMX's fallback Classic defaults.
 * Disables hardware filter slots and routes standard/extended data to FIFO0.
 */
bool SRNOS_FDCANInitAll(void);
bool SRNOS_FDCANStartAll(void);

/* Start/stop single bus (bus = 1/2/3). */
bool SRNOS_FDCANStart(uint8_t bus);
void SRNOS_FDCANStop(uint8_t bus);
/* TX worker only. Recovery never changes the frozen bit timing or filters.
 * Three attempts, each with 100 ms backoff and bounded hardware confirmation;
 * a latched failure needs a system restart. No blocking recovery in ISR. */
bool SRNOS_FDCANService(uint8_t bus, uint32_t now_ms);
uint32_t SRNOS_FDCANNextServiceWait(uint8_t bus, uint32_t now_ms);
bool SRNOS_FDCANIsReady(uint8_t bus);

/* Non-blocking TX. Returns true if enqueued to Tx FIFO/Q. */
bool SRNOS_FDCANTryTx(uint8_t               bus,
                      const FDCAN_TxHeaderTypeDef *tx_hdr,
                      const uint8_t         *payload);

/* Pop up to max_count frames from RX ring. Returns popped count. */
size_t SRNOS_FDCANRxPop(uint8_t bus,
                        SRNOS_FDCANRxFrame *out,
                        size_t max_count);

/* Append a Standard ID filter (mask mode by default). fifo=0->FIFO0, 1->FIFO1 */
HAL_StatusTypeDef SRNOS_FDCANAddStdFilter(uint8_t bus,
                                           uint16_t id,
                                           uint16_t mask,
                                           uint8_t  fifo);

/* Append an Extended ID filter (mask mode). */
HAL_StatusTypeDef SRNOS_FDCANAddExtFilter(uint8_t bus,
                                           uint32_t id,
                                           uint32_t mask,
                                           uint8_t  fifo);

/* Read diagnostics snapshot for a bus. */
SRNOS_FDCANDiag SRNOS_FDCANGetDiag(uint8_t bus);

/* Only the fallback definitions are weak. A weak declaration here also
 * weakens router implementations which include this header, allowing the
 * linker to select the no-op fallback instead of waking the RX/TX workers. */
void SRNOS_FDCANOnRxISR(uint8_t bus);
void SRNOS_FDCANOnBusoff(uint8_t bus);
void SRNOS_FDCANOnTxEvent(uint8_t bus);
/* One notification after a batch; OnTxEvent remains one accounting call/frame. */
void SRNOS_FDCANOnTxReady(uint8_t bus);

/* Utility: DLC to byte length (0..64). */
uint8_t SRNOS_FDCANDlcToLen(uint32_t dlc);

// 经典 CAN（标准 ID）非阻塞发送；len > 8 或非 Classic 模式被拒绝；无 RTOS 依赖
bool SRNOS_FDCANTxPush(uint8_t bus, uint16_t std_id, const uint8_t* data, uint8_t len);

// 经典 CAN（扩展 ID）非阻塞发送；无 RTOS 依赖
bool SRNOS_FDCANTxPushExt(uint8_t bus, uint32_t ext_id, const uint8_t* data, uint8_t len);
/* FD raw path only; exact wire length, BRS matches the frozen policy. */
bool SRNOS_FDCANTxPushFD(uint8_t bus, const SRNOS_CANFDFrame *frame);


#ifdef __cplusplus
}
#endif

#endif //SRNOS_FDCAN_H
