#ifndef SRNOS_CONFIG_H
#define SRNOS_CONFIG_H

/* All SDK translation units receive the same application overrides. */
#ifdef SRNOS_CONFIG_HEADER
#include SRNOS_CONFIG_HEADER
#endif

#ifndef SRNOS_APP_STACK_BYTES
#define SRNOS_APP_STACK_BYTES 4096U
#endif
#ifndef SRNOS_APP_PRIORITY
/* CMSIS-RTOS2 osPriorityNormal; FreeRTOS priorities range from 0 to 55. */
#define SRNOS_APP_PRIORITY 24U
#endif
#ifndef SRNOS_HEAP_DTCM_BYTES
#define SRNOS_HEAP_DTCM_BYTES (64U * 1024U)
#endif
#ifndef SRNOS_HEAP_D1_BYTES
#define SRNOS_HEAP_D1_BYTES (128U * 1024U)
#endif

/* Fixed capacity for recoverable runtime application objects. */
#ifndef SRNOS_APP_OBJECT_POOL_BYTES
#define SRNOS_APP_OBJECT_POOL_BYTES (32U * 1024U)
#endif
#ifndef SRNOS_APP_THREAD_LIMIT
#define SRNOS_APP_THREAD_LIMIT 8U
#endif
#ifndef SRNOS_APP_MUTEX_LIMIT
#define SRNOS_APP_MUTEX_LIMIT 8U
#endif
#ifndef SRNOS_APP_SEMAPHORE_LIMIT
#define SRNOS_APP_SEMAPHORE_LIMIT 8U
#endif
#ifndef SRNOS_APP_QUEUE_LIMIT
#define SRNOS_APP_QUEUE_LIMIT 8U
#endif
#ifndef SRNOS_APP_EVENT_LIMIT
#define SRNOS_APP_EVENT_LIMIT 8U
#endif
#if SRNOS_APP_OBJECT_POOL_BYTES < 4096 || SRNOS_APP_OBJECT_POOL_BYTES % 256
#error "Application object pool must be a multiple of 256 bytes and at least 4096"
#endif
#if SRNOS_APP_THREAD_LIMIT < 1 || SRNOS_APP_THREAD_LIMIT > 24
#error "Application thread capacity must be 1..24 (64 total trace slots)"
#endif

#if SRNOS_APP_STACK_BYTES < 1024 || (SRNOS_APP_STACK_BYTES % 8) != 0
#error "Application stack must be at least 1024 bytes and a multiple of 8"
#endif
#if SRNOS_APP_PRIORITY < 1 || SRNOS_APP_PRIORITY > 39
#error "Application priority must be in the range 1..39"
#endif
#if SRNOS_HEAP_DTCM_BYTES < 1024 || (SRNOS_HEAP_DTCM_BYTES % 32) != 0
#error "DTCM heap must be at least 1024 bytes and a multiple of 32"
#endif
#if SRNOS_HEAP_D1_BYTES < 1024 || (SRNOS_HEAP_D1_BYTES % 32) != 0
#error "D1 heap must be at least 1024 bytes and a multiple of 32"
#endif
#endif
