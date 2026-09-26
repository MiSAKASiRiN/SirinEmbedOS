#ifndef TSKPTT_REFEREE_TX_H
#define TSKPTT_REFEREE_TX_H

#pragma once

#include "FreeRTOS.h"
#include "task.h"
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
} referee_tx_debug_t;

extern volatile referee_tx_debug_t g_referee_tx_debug;

typedef struct {
  SRNOS_SerialChannel *channel;
} tskptt_referee_tx_config_t;

void TSKPTT_Referee_TX_Task(void *argument);

bool RefereeTX_BindChannel(SRNOS_SerialChannel *channel);
size_t RefereeTX_Write(const void *data, size_t len, TickType_t timeout);
size_t RefereeTX_WriteStr(const char *str, TickType_t timeout);
bool RefereeTX_Send(const uint8_t *data, uint16_t len, TickType_t timeout);

#ifdef __cplusplus
}
#endif

#endif // TSKPTT_REFEREE_TX_H
