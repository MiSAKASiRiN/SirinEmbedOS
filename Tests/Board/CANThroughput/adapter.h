#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t rx_isr, rx_overrun, tx_enqueued, tx_busy, bus_off;
    uint32_t tec, rec, error_logging, tx_pending, rx_fifo_lost;
    uint32_t nominal_bps;
} SRNOS_TestCANHardware;
void SRNOS_TestCANHardwareRead(unsigned bus, SRNOS_TestCANHardware *out);
void SRNOS_TestIdleRead(uint64_t *cycles, uint64_t *idle);
#ifdef __cplusplus
}
#endif
