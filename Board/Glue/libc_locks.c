#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32h7xx.h"
#include <sys/lock.h>
#include <stddef.h>

/* Static storage avoids malloc -> lock-init -> malloc recursion. All libc
 * retarget locks are recursive, which satisfies normal and recursive users.
 * This does not add locking to Picolibc FILE operations that omit it upstream. */
struct __lock {
    StaticSemaphore_t storage;
    SemaphoreHandle_t mutex;
    unsigned allocated;
};
struct __lock __lock___libc_recursive_mutex;
static struct __lock s_dynamic_locks[8];

static void initialize(struct __lock *lock)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (lock->mutex == NULL) {
        lock->mutex = xSemaphoreCreateRecursiveMutexStatic(&lock->storage);
        configASSERT(lock->mutex != NULL);
    }
    __set_PRIMASK(mask);
}

void __retarget_lock_init(_LOCK_T *output)
{
    configASSERT(__get_IPSR() == 0u && output != NULL);
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    for (size_t i = 0; i < sizeof(s_dynamic_locks) / sizeof(s_dynamic_locks[0]); ++i) {
        if (!s_dynamic_locks[i].allocated) {
            s_dynamic_locks[i].allocated = 1;
            initialize(&s_dynamic_locks[i]);
            *output = &s_dynamic_locks[i];
            __set_PRIMASK(mask);
            return;
        }
    }
    __set_PRIMASK(mask);
    SRNOS_KernelFailure("Picolibc lock capacity exceeded", __FILE__, __LINE__);
}
void __retarget_lock_init_recursive(_LOCK_T *lock) { __retarget_lock_init(lock); }

void __retarget_lock_acquire(_LOCK_T lock)
{
    configASSERT(__get_IPSR() == 0u && lock != NULL);
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;
    initialize(lock);
    const TickType_t wait = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING
                            ? portMAX_DELAY : 0;
    configASSERT(xSemaphoreTakeRecursive(lock->mutex, wait) == pdTRUE);
}
void __retarget_lock_acquire_recursive(_LOCK_T lock) { __retarget_lock_acquire(lock); }

void __retarget_lock_release(_LOCK_T lock)
{
    configASSERT(__get_IPSR() == 0u && lock != NULL);
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;
    configASSERT(lock->mutex != NULL);
    configASSERT(xSemaphoreGiveRecursive(lock->mutex) == pdTRUE);
}
void __retarget_lock_release_recursive(_LOCK_T lock) { __retarget_lock_release(lock); }

void __retarget_lock_close(_LOCK_T lock)
{
    configASSERT(__get_IPSR() == 0u && lock != NULL);
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    for (size_t i = 0; i < sizeof(s_dynamic_locks) / sizeof(s_dynamic_locks[0]); ++i) {
        if (lock == &s_dynamic_locks[i]) {
            /* Caller must have ended all use of this object before close. */
            configASSERT(lock->allocated && xSemaphoreGetMutexHolder(lock->mutex) == NULL);
            vSemaphoreDelete(lock->mutex);
            lock->mutex = NULL;
            lock->allocated = 0;
            __set_PRIMASK(mask);
            return;
        }
    }
    __set_PRIMASK(mask);
    SRNOS_KernelFailure("Invalid Picolibc lock close", __FILE__, __LINE__);
}
void __retarget_lock_close_recursive(_LOCK_T lock) { __retarget_lock_close(lock); }
