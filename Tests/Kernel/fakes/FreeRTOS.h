#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
typedef int BaseType_t;typedef unsigned UBaseType_t;typedef uint32_t TickType_t;typedef uint32_t StackType_t;
typedef struct fake_task {pthread_t thread;void (*entry)(void *);void *arg;uint32_t notify[2];bool suspended,deleted;char name[16];} StaticTask_t;
typedef struct fake_queue {unsigned kind,count,max,size,head,tail,depth;uint8_t *bytes;StaticTask_t *owner;} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;
typedef struct {uint32_t bits;} StaticEventGroup_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000u
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5u
#define configMAX_TASK_NAME_LEN 16
#define configASSERT(x) assert(x)
void test_enter(void);void test_exit(void);void test_yield(void);
#define taskENTER_CRITICAL() test_enter()
#define taskEXIT_CRITICAL() test_exit()
#define taskENTER_CRITICAL_FROM_ISR() (test_enter(),0u)
#define taskEXIT_CRITICAL_FROM_ISR(x) ((void)(x),test_exit())
#define taskYIELD() test_yield()
#define portYIELD_FROM_ISR(x) do{if(x)test_yield();}while(0)
void *pvPortMalloc(size_t);void vPortFree(void *);
size_t xPortGetFreeHeapSize(void);size_t xPortGetMinimumEverFreeHeapSize(void);
