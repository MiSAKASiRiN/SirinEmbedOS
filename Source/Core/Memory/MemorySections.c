#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "regions_internal.h"

#include <stdint.h>

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgMpuDcache SRNOS_DebugDbgMpuDcache;

static void SRNOS_ZeroRegion(char *begin, char *end)
{
    while (begin < end) {
        *begin++ = 0;
    }
}

void SRNOS_MemInitEarly(void)
{
    zero_tcm_region((uintptr_t)SRNOS_ITCMBssStart, (uintptr_t)SRNOS_ITCMBssEnd);
    zero_tcm_region((uintptr_t)__debug_watch_start__, (uintptr_t)__debug_watch_end__);
    SRNOS_ZeroRegion(__dtcm_bss_start__, __dtcm_bss_end__);
    SRNOS_ZeroRegion(__dma_nc_start__, __dma_nc_end__);
    SRNOS_ZeroRegion(__dma_nc_buf_start__, __dma_nc_buf_end__);
    SRNOS_ZeroRegion(__bdma_buf_start__, __bdma_buf_end__);
    SRNOS_ZeroRegion(__eth_dma_start__, __eth_dma_end__);
    /* heap_5 initializes metadata itself; retained panic is deliberately untouched. */

    if ((uintptr_t)__itcm_text_start__ != (uintptr_t)__itcm_text_load_start__) {
        copy_tcm_region((uintptr_t)__itcm_text_start__,
                        (uintptr_t)__itcm_text_load_start__,
                        (uintptr_t)__itcm_text_end__);
    }

    SRNOS_DbgZero(&SRNOS_DebugDbgMpuDcache, sizeof(SRNOS_DebugDbgMpuDcache));
    SRNOS_DbgInit(&SRNOS_DebugDbgMpuDcache.h,
                 SRNOS_DBG_MODULE_MPU_DCACHE,
                 0u,
                 (uint16_t)sizeof(SRNOS_DebugDbgMpuDcache));
    SRNOS_DebugDbgMpuDcache.mem_sections_enabled = SRNOS_MEM_SECTIONS_ENABLED;
    SRNOS_DebugDbgMpuDcache.init_count++;
    SRNOS_DebugDbgMpuDcache.dma_nc_size = (uint32_t)SRNOS_DMA_NC_SIZE();
    SRNOS_DebugDbgMpuDcache.dma_nc_buf_size = (uint32_t)SRNOS_DMA_NC_BUF_SIZE();
    SRNOS_DebugDbgMpuDcache.bdma_buf_size = (uint32_t)SRNOS_BDMA_BUF_SIZE();
    SRNOS_DebugDbgMpuDcache.eth_dma_size = (uint32_t)SRNOS_ETH_DMA_SIZE();
    SRNOS_DebugDbgMpuDcache.dtcm_bss_size = (uint32_t)SRNOS_DTCM_BSS_SIZE();
    SRNOS_DebugDbgMpuDcache.dtcm_heap_size = (uint32_t)SRNOS_DTCM_HEAP_SIZE();
    SRNOS_DebugDbgMpuDcache.debug_watch_size = (uint32_t)SRNOS_DEBUG_WATCH_SIZE();
    SRNOS_DebugDbgMpuDcache.itcm_text_size = (uint32_t)SRNOS_ITCM_TEXT_SIZE();
    SRNOS_DebugDbgMpuDcache.itcm_bss_size = (uint32_t)SRNOS_ITCM_BSS_SIZE();
    SRNOS_DbgEnd(&SRNOS_DebugDbgMpuDcache.h, 0u, 0u);
}
