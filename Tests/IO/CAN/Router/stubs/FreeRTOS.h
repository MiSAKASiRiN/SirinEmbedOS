#ifndef SRNOS_CAN_ROUTER_TEST_FREERTOS_H
#define SRNOS_CAN_ROUTER_TEST_FREERTOS_H

#ifdef SRNOS_CAN_HOST_COUNT_CRITICAL
extern unsigned SRNOS_TestCANCriticalEntries;
#define taskENTER_CRITICAL() do { ++SRNOS_TestCANCriticalEntries; } while (0)
#else
#define taskENTER_CRITICAL() do { } while (0)
#endif
#define taskEXIT_CRITICAL() do { } while (0)

#endif
