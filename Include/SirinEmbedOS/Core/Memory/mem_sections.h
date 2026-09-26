//
// mem_sections.h
// STM32H7 memory placement helpers.
//

#ifndef OMX_MEM_SECTIONS_H
#define OMX_MEM_SECTIONS_H

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize manually managed sections.
 *
 * Call before enabling I-Cache/D-Cache when .itcm_text is used. This function
 * clears DMA, Watch and .dtcm_bss, and copies .itcm_text from Flash to ITCM.
 * It never clears retained panic state or initializes heap payloads.
 */
void omx_mem_init_early(void);

#if defined(__GNUC__) && defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#define OMX_MEM_SECTIONS_ENABLED 1
#define OMX_ALIGN32              __attribute__((aligned(32)))
#define OMX_USED                 __attribute__((used))

/* Non-cacheable DMA sections.
 *
 * These sections are NOLOAD/NOBITS in the linker fragment. Objects placed
 * here must be initialized at runtime before DMA uses them; static initializers
 * are intentionally not copied into RAM.
 */
#define OMX_DMA_NC_DESC_BSS      __attribute__((section(".dma_nc"))) OMX_ALIGN32
#define OMX_DMA_NC_BUF_BSS       __attribute__((section(".dma_nc_buf"))) OMX_ALIGN32
#define OMX_BDMA_BUF_BSS         __attribute__((section(".bdma_buf"))) OMX_ALIGN32
#define OMX_ETH_DMA_BSS          __attribute__((section(".eth_dma"))) OMX_ALIGN32
#define OMX_DEBUG_WATCH_BSS      __attribute__((section(".debug_watch"))) OMX_ALIGN32 OMX_USED

/* CPU-only high-speed memory. Never use DTCM sections for DMA buffers. */
#define OMX_DTCM_BSS             __attribute__((section(".dtcm_bss"))) OMX_ALIGN32
#define OMX_DTCM_HEAP            __attribute__((section(".dtcm_heap"))) OMX_ALIGN32 OMX_USED

/* Optional hot code section. noinline keeps placement explicit; long_call
   avoids range problems when Flash code calls ITCM-resident functions. */
#define OMX_ITCM_TEXT            __attribute__((section(".itcm_text"), noinline))
#else
#define OMX_MEM_SECTIONS_ENABLED 0
#define OMX_ALIGN32
#define OMX_USED
#define OMX_DMA_NC_DESC_BSS
#define OMX_DMA_NC_BUF_BSS
#define OMX_BDMA_BUF_BSS
#define OMX_ETH_DMA_BSS
#define OMX_DEBUG_WATCH_BSS
#define OMX_DTCM_BSS
#define OMX_DTCM_HEAP
#define OMX_ITCM_TEXT
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

#define OMX_DMA_NC_SIZE()       ((size_t)(__dma_nc_end__     - __dma_nc_start__))
#define OMX_DMA_NC_BUF_SIZE()   ((size_t)(__dma_nc_buf_end__ - __dma_nc_buf_start__))
#define OMX_BDMA_BUF_SIZE()     ((size_t)(__bdma_buf_end__   - __bdma_buf_start__))
#define OMX_ETH_DMA_SIZE()      ((size_t)(__eth_dma_end__    - __eth_dma_start__))
#define OMX_DEBUG_WATCH_SIZE()  ((size_t)(__debug_watch_end__ - __debug_watch_start__))
#define OMX_DTCM_BSS_SIZE()     ((size_t)(__dtcm_bss_end__   - __dtcm_bss_start__))
#define OMX_DTCM_HEAP_SIZE()    ((size_t)(__dtcm_heap_end__  - __dtcm_heap_start__))
#define OMX_ITCM_TEXT_SIZE()    ((size_t)(__itcm_text_end__  - __itcm_text_start__))
#else
#define OMX_DMA_NC_SIZE()       ((size_t)0u)
#define OMX_DMA_NC_BUF_SIZE()   ((size_t)0u)
#define OMX_BDMA_BUF_SIZE()     ((size_t)0u)
#define OMX_ETH_DMA_SIZE()      ((size_t)0u)
#define OMX_DEBUG_WATCH_SIZE()  ((size_t)0u)
#define OMX_DTCM_BSS_SIZE()     ((size_t)0u)
#define OMX_DTCM_HEAP_SIZE()    ((size_t)0u)
#define OMX_ITCM_TEXT_SIZE()    ((size_t)0u)
#endif

/* Backward-compatible aliases. New code should use OMX_* names above. */
#ifndef ALIGN32
#define ALIGN32              OMX_ALIGN32
#endif
#ifndef SEC_DMA_NC
#define SEC_DMA_NC           OMX_DMA_NC_DESC_BSS
#endif
#ifndef SEC_DMA_NC_BUF
#define SEC_DMA_NC_BUF       OMX_DMA_NC_BUF_BSS
#endif
#ifndef SEC_BDMA_BUF
#define SEC_BDMA_BUF         OMX_BDMA_BUF_BSS
#endif
#ifndef SEC_ETH_DMA
#define SEC_ETH_DMA          OMX_ETH_DMA_BSS
#endif
#ifndef SEC_DEBUG_WATCH
#define SEC_DEBUG_WATCH      OMX_DEBUG_WATCH_BSS
#endif
#ifndef SEC_DTCM_BSS
#define SEC_DTCM_BSS         OMX_DTCM_BSS
#endif
#ifndef SEC_DTCM_HEAP
#define SEC_DTCM_HEAP        OMX_DTCM_HEAP
#endif
#ifndef SEC_ITCM_TEXT
#define SEC_ITCM_TEXT        OMX_ITCM_TEXT
#endif

#define DMA_NC_SIZE()        OMX_DMA_NC_SIZE()
#define DMA_NC_BUF_SIZE()    OMX_DMA_NC_BUF_SIZE()
#define BDMA_BUF_SIZE()      OMX_BDMA_BUF_SIZE()
#define ETH_DMA_SIZE()       OMX_ETH_DMA_SIZE()
#define DEBUG_WATCH_SIZE()   OMX_DEBUG_WATCH_SIZE()
#define DTCM_BSS_SIZE()      OMX_DTCM_BSS_SIZE()
#define DTCM_HEAP_SIZE()     OMX_DTCM_HEAP_SIZE()
#define ITCM_TEXT_SIZE()     OMX_ITCM_TEXT_SIZE()

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OMX_MEM_SECTIONS_H */
