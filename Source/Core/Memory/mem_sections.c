#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"

#include <stdint.h>

OMX_DEBUG_WATCH_BSS volatile omx_dbg_mpu_dcache_t g_omx_dbg_mpu_dcache;

static void omx_zero_region(char *begin, char *end)
{
    while (begin < end) {
        *begin++ = 0;
    }
}

static void omx_copy_region(char *dst, const char *src, char *end)
{
    while (dst < end) {
        *dst++ = *src++;
    }
}

void omx_mem_init_early(void)
{
    omx_zero_region(__dtcm_bss_start__, __dtcm_bss_end__);
    omx_zero_region(__dma_nc_start__, __dma_nc_end__);
    omx_zero_region(__dma_nc_buf_start__, __dma_nc_buf_end__);
    omx_zero_region(__bdma_buf_start__, __bdma_buf_end__);
    omx_zero_region(__eth_dma_start__, __eth_dma_end__);
    omx_zero_region(__debug_watch_start__, __debug_watch_end__);
    /* heap_5 initializes metadata itself; retained panic is deliberately untouched. */

    if (__itcm_text_start__ != __itcm_text_load_start__) {
        omx_copy_region(__itcm_text_start__,
                        __itcm_text_load_start__,
                        __itcm_text_end__);
    }

    omx_dbg_zero(&g_omx_dbg_mpu_dcache, sizeof(g_omx_dbg_mpu_dcache));
    omx_dbg_init(&g_omx_dbg_mpu_dcache.h,
                 OMX_DBG_MODULE_MPU_DCACHE,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_mpu_dcache));
    g_omx_dbg_mpu_dcache.mem_sections_enabled = OMX_MEM_SECTIONS_ENABLED;
    g_omx_dbg_mpu_dcache.init_count++;
    g_omx_dbg_mpu_dcache.dma_nc_size = (uint32_t)OMX_DMA_NC_SIZE();
    g_omx_dbg_mpu_dcache.dma_nc_buf_size = (uint32_t)OMX_DMA_NC_BUF_SIZE();
    g_omx_dbg_mpu_dcache.bdma_buf_size = (uint32_t)OMX_BDMA_BUF_SIZE();
    g_omx_dbg_mpu_dcache.eth_dma_size = (uint32_t)OMX_ETH_DMA_SIZE();
    g_omx_dbg_mpu_dcache.dtcm_bss_size = (uint32_t)OMX_DTCM_BSS_SIZE();
    g_omx_dbg_mpu_dcache.dtcm_heap_size = (uint32_t)OMX_DTCM_HEAP_SIZE();
    g_omx_dbg_mpu_dcache.debug_watch_size = (uint32_t)OMX_DEBUG_WATCH_SIZE();
    omx_dbg_end(&g_omx_dbg_mpu_dcache.h, 0u, 0u);
}
