#ifndef SRNOS_MP257_WBUS_RECEIVER_H
#define SRNOS_MP257_WBUS_RECEIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once from the M0+ bare-metal main with the board clocks configured.
 * Takes ownership of LPUART1 RX, SysTick, IPCC2 shutdown and the M0+ IPC slot. */
void SRNOS_MP257WBusReceiverInit(void);

/* Call repeatedly in main after Init, with interrupts enabled. Waits for an
 * interrupt, then services downlink, system status and trace when due. */
void SRNOS_MP257WBusReceiverWaitAndPoll(void);

#ifdef __cplusplus
}
#endif
#endif
