#ifndef SRNOS_BOARD_STARTUP_H
#define SRNOS_BOARD_STARTUP_H
void SRNOS_BoardEarlyInitialize(void);
__attribute__((noreturn)) void SRNOS_BoardStartKernel(void);
#endif
