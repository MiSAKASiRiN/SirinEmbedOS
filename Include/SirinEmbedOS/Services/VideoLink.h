#ifndef SRNOS_VIDEO_LINK_H
#define SRNOS_VIDEO_LINK_H
#include "SirinEmbedOS/Services/Telemetry.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_VIDEO_LINK_DOWNLINK_BYTES 30u
#define SRNOS_VIDEO_LINK_UPLINK_BYTES 300u
#define SRNOS_VIDEO_LINK_CAPACITY 32u
/* Reserve forwarding headroom below the transport protocol's 50 Hz limit. */
#define SRNOS_VIDEO_LINK_TX_INTERVAL_MS 25u
typedef enum {
    SRNOS_VIDEO_LINK_OK=0, SRNOS_VIDEO_LINK_INVALID=-1,
    SRNOS_VIDEO_LINK_DISABLED=-2, SRNOS_VIDEO_LINK_BUSY=-3,
    SRNOS_VIDEO_LINK_FROZEN=-4, SRNOS_VIDEO_LINK_CONTEXT=-5
} SRNOS_VideoLinkResult;
typedef struct {
    uint32_t rx_messages, rx_dropped, tx_frames, tx_busy, tx_errors;
    uint32_t sample_packets, descriptor_packets, last_rx_ms, last_tx_ms;
} SRNOS_VideoLinkStats;
/* Startup only, after TelemetryRegister and before configuration freezes.
 * The SDK copies this route; it sends the latest published sample automatically.
 * wire_id is a stable application ID, 1..65535. period_ms: 25..60000, multiple
 * of 25. max_age_ms: 20..60000. Invalid/stale samples remain explicitly invalid.
 * Names/units/types come from the existing Telemetry descriptor. No motor I/O. */
SRNOS_VideoLinkResult SRNOS_VideoLinkRegisterTelemetry(SRNOS_TelemetryHandle handle,
    uint16_t wire_id, uint16_t period_ms, uint16_t max_age_ms);
/* Custom 0311 messages are dispatched through Message.h. SRVL directory/ping
 * requests stay service-owned. Task-only Send copies exactly 300 bytes to Serial
 * with a shared 40 Hz ceiling. Caller may reuse its buffer on return. */
SRNOS_VideoLinkResult SRNOS_VideoLinkSend(const uint8_t data[SRNOS_VIDEO_LINK_UPLINK_BYTES]);
SRNOS_VideoLinkResult SRNOS_VideoLinkGetStats(SRNOS_VideoLinkStats *out);
#ifdef __cplusplus
}
#endif
#endif
