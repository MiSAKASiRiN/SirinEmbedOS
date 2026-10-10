#pragma once
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
extern FDCAN_HandleTypeDef fake_handles[3];
extern uint32_t fake_now,fake_isr,rx_remaining,rx_sequence;
extern unsigned stop_calls,start_calls,abort_calls,rx_calls,tx_event_calls;
extern bool fail_rx,refill_rx,fail_stop,abort_ack,clear_bo_on_start;
extern uint32_t aborted_mask;
extern uint32_t fake_clock;
extern unsigned init_calls,tx_calls,fail_init_bus,fail_tdc_bus,fail_filter_bus;
extern uint32_t tdc_offset[3],tdc_filter[3];
extern bool tdc_enabled[3],fake_config_frozen;
extern FDCAN_TxHeaderTypeDef captured_header;
extern uint8_t captured_data[64];
extern bool custom_rx;
extern FDCAN_RxHeaderTypeDef custom_rx_header;
void fake_freeze_classic(void);
