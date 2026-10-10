#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t rx_isr, rx_overrun, rx_hw_lost, tx_enqueued, tx_busy, bus_off;
    uint32_t tec, rec, error_logging, tx_pending, rx_fifo_lost;
    uint32_t kernel_hz, nominal_bps, data_bps, frame_format;
    uint32_t nbtp, dbtp, tdcr, cccr, psr;
} SRNOS_TestCANHardware;
typedef struct { char name[16]; uint32_t free_bytes, priority; } SRNOS_TestCANStack;
void SRNOS_TestCANHardwareRead(unsigned bus, SRNOS_TestCANHardware *out);
void SRNOS_TestIdleRead(uint64_t *cycles, uint64_t *idle);
unsigned SRNOS_TestCANStacks(SRNOS_TestCANStack *out, unsigned capacity);
#ifdef __cplusplus
}
#endif
