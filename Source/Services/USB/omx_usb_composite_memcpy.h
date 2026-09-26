#ifndef OMX_USB_COMPOSITE_MEMCPY_H
#define OMX_USB_COMPOSITE_MEMCPY_H

#include <stddef.h>
#include <stdint.h>

static inline void *omx_usb_composite_desc_memcpy(void *dst, const void *src, size_t len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;

    while (len > 0u) {
        *d++ = *s++;
        --len;
    }

    return dst;
}

/*
 * AL94 composite descriptor assembly copies from ptr + 0x09 into packed
 * uint8_t arrays. With UNALIGN_TRP enabled, newlib memcpy may fault on the
 * odd source/destination addresses. This header is force-included only for
 * usbd_composite.c.
 */
#define memcpy omx_usb_composite_desc_memcpy

#endif /* OMX_USB_COMPOSITE_MEMCPY_H */
