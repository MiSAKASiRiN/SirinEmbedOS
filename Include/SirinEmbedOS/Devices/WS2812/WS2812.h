//
// Created by sirin on 2025/9/29.
//

#ifndef SRNOS_WS2812_H
#define SRNOS_WS2812_H

#pragma once
#include <stdint.h>

/* ================== System Config ================== */

#ifndef SRNOS_WS2812_LED_COUNT
#define SRNOS_WS2812_LED_COUNT      3u
#endif

#ifndef SRNOS_WS2812_RESET_SLOTS
#define SRNOS_WS2812_RESET_SLOTS    80u   // >= 50us
#endif

#ifndef SRNOS_WS2812_BITS_PER_LED
#define SRNOS_WS2812_BITS_PER_LED   24u   // GRB, MSB first
#endif

#ifndef SRNOS_WS2812_BITRATE_HZ
#define SRNOS_WS2812_BITRATE_HZ     800000u // 800kHz
#endif

#ifndef SRNOS_WS2812_T0H_NS
#define SRNOS_WS2812_T0H_NS         350u
#endif
#ifndef SRNOS_WS2812_T1H_NS
#define SRNOS_WS2812_T1H_NS         700u
#endif

/* Fixed GPIO/TIM/DMA configuration is private to Board/Support. */

/* ================== API & Hook ================== */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SRNOS_WS2812_OK=0, SRNOS_WS2812_ERROR=1, SRNOS_WS2812_BUSY=2, SRNOS_WS2812_TIMEOUT=3 } SRNOS_WS2812Result;

typedef void (*SRNOS_WS2812Cb)(void *user);

/**
 * Callbacks run in the DMA ISR and must be bounded/nonblocking. Use only SDK
 * ISR notification APIs. The driver copies this descriptor at submission;
 * user/notify_arg are application contexts that must outlive the transfer.
 */
typedef struct {
    SRNOS_WS2812Cb on_tx_done;
    SRNOS_WS2812Cb on_tx_error;
    void       *user;

    void (*notify_from_isr)(void *arg);
    void *notify_arg;
} SRNOS_WS2812Hooks;

/** Set single pixel */
void SRNOS_WS2812SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/** Fast fill */
void SRNOS_WS2812SetAll(uint8_t r, uint8_t g, uint8_t b);

/** Change Brightness */
void SRNOS_WS2812SetGlobalBrightness(uint8_t scale);

/** Task context, nonblocking. BUSY leaves the in-flight transfer unchanged.
 * Board initialization is owned by the SDK. Coordinate with Indicators before
 * issuing custom updates; the LED hardware has one owner at a time. */
SRNOS_WS2812Result SRNOS_WS2812CommitStartAsync(const SRNOS_WS2812Hooks *hooks);

/** If DMA Busy */
uint8_t SRNOS_WS2812IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif //SRNOS_WS2812_H
