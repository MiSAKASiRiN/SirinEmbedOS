#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Fatal-context polling transport: takes over USART1/PB14 only on first use. */
bool SRNOS_FaultUSART1WriteByte(uint8_t byte);
void SRNOS_FaultUSART1Flush(void);
