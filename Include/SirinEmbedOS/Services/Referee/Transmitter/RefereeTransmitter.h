#ifndef SRNOS_REFEREE_TX_H
#define SRNOS_REFEREE_TX_H

#pragma once

#include "SirinEmbedOS/IO/Serial/Serial.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t write_req_cnt;
  uint32_t write_ok_cnt;
  uint32_t write_fail_cnt;
  uint32_t write_timeout_cnt;

  uint32_t notify_cnt;
  uint32_t service_loop_cnt;
  uint32_t service_busy_skip_cnt;

  uint32_t dma_start_cnt;
  uint32_t dma_cplt_cnt;
  uint32_t dma_requeue_cnt;

  uint32_t queue_cur;
  uint32_t queue_high_water;
  uint16_t last_write_len;
  uint16_t last_dma_len;

  uint8_t dma_busy;
  uint8_t router_inited;
  uint8_t service_task_set;
  uint8_t reserved;
} SRNOS_RefereeTxDebug;

extern volatile SRNOS_RefereeTxDebug SRNOS_DebugRefereeTxDebug;

typedef struct {
  SRNOS_SerialChannel *channel;
} SRNOS_RefereeTxConfig;


bool SRNOS_RefereeTxBindChannel(SRNOS_SerialChannel *channel);
/* Task context; timeout is milliseconds (SRNOS_NO_WAIT/FOREVER supported).
 * Includes Serial lock contention and queue submission, not wire completion.
 * Input is copied before successful return; no private RTOS types are exposed. */
size_t SRNOS_RefereeTxWrite(const void *data, size_t len, uint32_t timeout);
size_t SRNOS_RefereeTxWriteStr(const char *str, uint32_t timeout);
bool SRNOS_RefereeTransmit(const uint8_t *data, uint16_t len, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_REFEREE_TX_H
