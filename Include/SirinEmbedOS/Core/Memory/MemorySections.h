//
// mem_sections.h
// STM32H7 memory placement helpers.
//

#ifndef SRNOS_MEM_SECTIONS_H
#define SRNOS_MEM_SECTIONS_H

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize manually managed sections.
 *
 * Call before enabling I-Cache/D-Cache when .itcm_text is used. This function
 * clears DMA, Watch, .itcm_bss and .dtcm_bss, and copies .itcm_text from Flash
 * to ITCM. Dedicated sections must not be used by C++ global constructors.
 * It never clears retained panic state or initializes heap payloads.
 */
void SRNOS_MemInitEarly(void);

#if defined(__GNUC__) && defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#define SRNOS_MEM_SECTIONS_ENABLED 1
#define SRNOS_ALIGN32              __attribute__((aligned(32)))
#define SRNOS_USED                 __attribute__((used))

/* Non-cacheable DMA sections.
 *
 * These sections are NOLOAD/NOBITS in the linker fragment. Objects placed
 * here must be initialized at runtime before DMA uses them; static initializers
 * are intentionally not copied into RAM.
 */
#define SRNOS_DMA_NC_DESC_BSS      __attribute__((section(".dma_nc"))) SRNOS_ALIGN32
#define SRNOS_DMA_NC_BUF_BSS       __attribute__((section(".dma_nc_buf"))) SRNOS_ALIGN32
#define SRNOS_BDMA_BUF_BSS         __attribute__((section(".bdma_buf"))) SRNOS_ALIGN32
#define SRNOS_ETH_DMA_BSS          __attribute__((section(".eth_dma"))) SRNOS_ALIGN32
#define SRNOS_DEBUG_WATCH_BSS      __attribute__((section(".debug_watch"))) SRNOS_ALIGN32 SRNOS_USED

/* CPU-only memory. Never use either TCM section for DMA buffers.
 * ITCM BSS must have no nonzero initializer and is cleared before SDK setup,
 * after global constructors. Its capacity is shared with ITCM code and Watch. */
#define SRNOS_ITCM_BSS             __attribute__((section(".itcm_bss"))) SRNOS_ALIGN32
#define SRNOS_DTCM_BSS             __attribute__((section(".dtcm_bss"))) SRNOS_ALIGN32
#define SRNOS_DTCM_HEAP            __attribute__((section(".dtcm_heap"))) SRNOS_ALIGN32 SRNOS_USED

/* Optional hot code section. noinline keeps placement explicit; the linker
   supplies veneers for calls between Flash and ITCM. */
#define SRNOS_ITCM_TEXT            __attribute__((section(".itcm_text"), noinline))
#else
#define SRNOS_MEM_SECTIONS_ENABLED 0
#define SRNOS_ALIGN32
#define SRNOS_USED
#define SRNOS_DMA_NC_DESC_BSS
#define SRNOS_DMA_NC_BUF_BSS
#define SRNOS_BDMA_BUF_BSS
#define SRNOS_ETH_DMA_BSS
#define SRNOS_DEBUG_WATCH_BSS
#define SRNOS_ITCM_BSS
#define SRNOS_DTCM_BSS
#define SRNOS_DTCM_HEAP
#define SRNOS_ITCM_TEXT
#endif

#if defined(__GNUC__) && defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
extern char __dma_nc_start__[];
extern char __dma_nc_end__[];
extern char __dma_nc_buf_start__[];
extern char __dma_nc_buf_end__[];
extern char __bdma_buf_start__[];
extern char __bdma_buf_end__[];
extern char __eth_dma_start__[];
extern char __eth_dma_end__[];
extern char __debug_watch_start__[];
extern char __debug_watch_end__[];
extern char __dtcm_bss_start__[];
extern char __dtcm_bss_end__[];
extern char __dtcm_heap_start__[];
extern char __dtcm_heap_end__[];
extern char __itcm_text_start__[];
extern char __itcm_text_end__[];
extern char __itcm_text_load_start__[];
extern char SRNOS_ITCMBssStart[];
extern char SRNOS_ITCMBssEnd[];

#define SRNOS_DMA_NC_SIZE()       ((size_t)(__dma_nc_end__     - __dma_nc_start__))
#define SRNOS_DMA_NC_BUF_SIZE()   ((size_t)(__dma_nc_buf_end__ - __dma_nc_buf_start__))
#define SRNOS_BDMA_BUF_SIZE()     ((size_t)(__bdma_buf_end__   - __bdma_buf_start__))
#define SRNOS_ETH_DMA_SIZE()      ((size_t)(__eth_dma_end__    - __eth_dma_start__))
#define SRNOS_DEBUG_WATCH_SIZE()  ((size_t)(__debug_watch_end__ - __debug_watch_start__))
#define SRNOS_DTCM_BSS_SIZE()     ((size_t)(__dtcm_bss_end__   - __dtcm_bss_start__))
#define SRNOS_DTCM_HEAP_SIZE()    ((size_t)(__dtcm_heap_end__  - __dtcm_heap_start__))
#define SRNOS_ITCM_TEXT_SIZE()    ((size_t)(__itcm_text_end__  - __itcm_text_start__))
#define SRNOS_ITCM_BSS_SIZE()     ((size_t)(SRNOS_ITCMBssEnd - SRNOS_ITCMBssStart))
#else
#define SRNOS_DMA_NC_SIZE()       ((size_t)0u)
#define SRNOS_DMA_NC_BUF_SIZE()   ((size_t)0u)
#define SRNOS_BDMA_BUF_SIZE()     ((size_t)0u)
#define SRNOS_ETH_DMA_SIZE()      ((size_t)0u)
#define SRNOS_DEBUG_WATCH_SIZE()  ((size_t)0u)
#define SRNOS_DTCM_BSS_SIZE()     ((size_t)0u)
#define SRNOS_DTCM_HEAP_SIZE()    ((size_t)0u)
#define SRNOS_ITCM_TEXT_SIZE()    ((size_t)0u)
#define SRNOS_ITCM_BSS_SIZE()     ((size_t)0u)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SRNOS_MEM_SECTIONS_H */
