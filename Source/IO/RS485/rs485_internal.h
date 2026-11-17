#ifndef SRNOS_RS485_INTERNAL_H
#define SRNOS_RS485_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* SDK startup only. Registration freezes even without an endpoint. */
int SRNOS_RS485Start(uint32_t response_timeout_ms);
bool SRNOS_RS485IsFrozen(void);
uint32_t SRNOS_RS485HeapBudget(bool configured);
#ifdef __cplusplus
}
#endif
#endif
