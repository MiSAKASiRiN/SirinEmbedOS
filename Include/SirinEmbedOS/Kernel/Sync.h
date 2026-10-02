#pragma once
#include "SirinEmbedOS/Kernel/Thread.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t SRNOS_Mutex;
typedef uint64_t SRNOS_Semaphore;
typedef uint64_t SRNOS_Queue;
typedef uint64_t SRNOS_Event;
/* Objects use the bounded application pool. Delete returns BUSY while held
 * or in use, including blocked calls. A thread stop does not forcibly cancel
 * a sync/driver wait; prefer finite timeouts and check ShouldStop. */
SRNOS_Result SRNOS_MutexCreate(bool recursive, SRNOS_Mutex *out);
SRNOS_Result SRNOS_MutexLock(SRNOS_Mutex, uint32_t timeout_ms);
SRNOS_Result SRNOS_MutexUnlock(SRNOS_Mutex);
SRNOS_Result SRNOS_MutexDelete(SRNOS_Mutex);
SRNOS_Result SRNOS_SemaphoreCreate(uint32_t maximum, uint32_t initial, SRNOS_Semaphore *out);
SRNOS_Result SRNOS_SemaphoreTake(SRNOS_Semaphore, uint32_t timeout_ms);
SRNOS_Result SRNOS_SemaphoreGive(SRNOS_Semaphore);
SRNOS_Result SRNOS_SemaphoreGiveFromISR(SRNOS_Semaphore);
SRNOS_Result SRNOS_SemaphoreDelete(SRNOS_Semaphore);
SRNOS_Result SRNOS_QueueCreate(uint32_t capacity, uint32_t item_bytes, SRNOS_Queue *out);
SRNOS_Result SRNOS_QueueSend(SRNOS_Queue, const void *item, uint32_t timeout_ms);
SRNOS_Result SRNOS_QueueReceive(SRNOS_Queue, void *item, uint32_t timeout_ms);
SRNOS_Result SRNOS_QueueSendFromISR(SRNOS_Queue, const void *item);
SRNOS_Result SRNOS_QueueReceiveFromISR(SRNOS_Queue, void *item);
SRNOS_Result SRNOS_QueueDelete(SRNOS_Queue);
/* Event bits 0..23. Event operations are task-only; ISR producers can use
 * ThreadNotifyFromISR or the queue/semaphore ISR functions instead. */
SRNOS_Result SRNOS_EventCreate(SRNOS_Event *out);
SRNOS_Result SRNOS_EventSet(SRNOS_Event, uint32_t bits);
SRNOS_Result SRNOS_EventClear(SRNOS_Event, uint32_t bits);
SRNOS_Result SRNOS_EventWait(SRNOS_Event, uint32_t bits, bool all, bool clear,
                             uint32_t timeout_ms, uint32_t *received);
SRNOS_Result SRNOS_EventDelete(SRNOS_Event);
#ifdef __cplusplus
}
#endif
