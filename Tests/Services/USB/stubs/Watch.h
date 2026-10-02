#ifndef SRNOS_TEST_WATCH_H
#define SRNOS_TEST_WATCH_H

#include <stdint.h>

typedef struct {
    uint32_t configured;
    uint32_t dtr;
    uint32_t tx_active;
    uint32_t rx_events;
    uint32_t rx_bytes;
    uint32_t rx_drop;
    uint32_t tx_events;
    uint32_t tx_bytes;
    uint32_t tx_complete;
    uint32_t tx_busy;
    uint32_t tx_errors;
    uint32_t tx_drop;
    uint32_t disconnects;
    uint32_t last_rx_len;
    uint32_t last_tx_len;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t replay_active;
    uint32_t replay_restarts;
} SRNOS_DbgUSBCdc;

extern volatile SRNOS_DbgUSBCdc SRNOS_DebugDbgUSBCdc[];

#endif
