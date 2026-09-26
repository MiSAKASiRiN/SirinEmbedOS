#ifndef SIRINEMBEDOS_CONFIG_H
#define SIRINEMBEDOS_CONFIG_H

/* All SDK translation units receive the same application overrides. */
#ifdef SIRINEMBEDOS_CONFIG_HEADER
#include SIRINEMBEDOS_CONFIG_HEADER
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

#if SRNOS_APP_STACK_BYTES < 1024 || (SRNOS_APP_STACK_BYTES % 8) != 0
#error "Application stack must be at least 1024 bytes and a multiple of 8"
#endif
#if SRNOS_APP_PRIORITY < 1 || SRNOS_APP_PRIORITY > 55
#error "Application priority must be in the CMSIS-RTOS2 range 1..55"
#endif
#if SRNOS_HEAP_DTCM_BYTES < 1024 || (SRNOS_HEAP_DTCM_BYTES % 32) != 0
#error "DTCM heap must be at least 1024 bytes and a multiple of 32"
#endif
#if SRNOS_HEAP_D1_BYTES < 1024 || (SRNOS_HEAP_D1_BYTES % 32) != 0
#error "D1 heap must be at least 1024 bytes and a multiple of 32"
#endif
#endif
