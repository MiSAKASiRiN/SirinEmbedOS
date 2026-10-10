#ifndef SRNOS_CAN_FD_H
#define SRNOS_CAN_FD_H
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_CANFD_MAX_DATA_BYTES 64u
enum {
    SRNOS_CANFD_FRAME_FLAG_EXT = 1u << 0,
    SRNOS_CANFD_FRAME_FLAG_BRS = 1u << 1,
    SRNOS_CANFD_FRAME_FLAG_ESI = 1u << 2
};
/* This type always represents FDF=1, including frames with <=8 bytes. No RTR.
 * length is an exact wire byte count: 0..8, 12,16,20,24,32,48,64.
 * TX BRS must match the frozen controller policy; ESI is RX-only. */
typedef struct {
    uint32_t id;
    uint8_t length;
    uint8_t flags;
    uint8_t _pad[2];
    uint8_t data[SRNOS_CANFD_MAX_DATA_BYTES];
} SRNOS_CANFDFrame;
typedef void (*SRNOS_CANFDRxHandler)(SRNOS_CANChannel channel,
                                   const SRNOS_CANFDFrame *frame, void *user);
typedef enum {
    SRNOS_CANFD_OK = 0,
    SRNOS_CANFD_INVALID = -1,
    SRNOS_CANFD_FULL = -2,
    SRNOS_CANFD_NOT_FOUND = -3,
    SRNOS_CANFD_FROZEN = -8,
    SRNOS_CANFD_MODE_MISMATCH = -9
} SRNOS_CANFDResult;
typedef struct {
    uint32_t rx_received, rx_delivered, rx_no_subscriber, rx_rejected;
    uint32_t rx_wake_by_isr, rx_wake_by_timeout;
    uint32_t tx_enqueued, tx_queue_full, tx_expired, tx_accepted;
    uint32_t tx_backend_busy;
    /* Native: individual TEF completions. CAN4: confirmed TX FIFO events,
     * which may coalesce and do not count individual completed frames. */
    uint32_t tx_complete_events;
    uint32_t tx_depth, tx_high_water;
    uint32_t route_count;
} SRNOS_CANFDStats;
/* Startup registration only (ApplicationConfigure or bind_channels); callbacks
 * and user storage have firmware lifetime. The final configuration rejects any
 * Classic registration on an FD channel or FD registration on a Classic channel.
 * filter uses only SRNOS_CAN_FRAME_FLAG_EXT; exact identifier matching.
 * RX callback runs outside the routing lock: native RX worker (P0), or the
 * unified CAN4 service (P1). It must be bounded and nonblocking; the
 * frame is valid only during the callback. Callback must be bounded/nonblocking. */
SRNOS_CANFDResult SRNOS_CANFDSubscribeRaw(SRNOS_CANChannel channel,
    SRNOS_CANRxFilter filter, SRNOS_CANFDRxHandler callback, void *user);
SRNOS_CANFDResult SRNOS_CANFDUnsubscribeRaw(SRNOS_CANChannel channel,
    SRNOS_CANRxFilter filter, SRNOS_CANFDRxHandler callback, void *user);
/* Task context only, after freeze/resources ready. Copies the frame; OK means
 * queued, not delivered. Lower numeric priority wins; FIFO among equal values.
 * ttl_ms=0 selects 20 ms, otherwise 1..INT32_MAX; expiry never extends on retry.
 * TTL bounds software queue waiting, checked before backend submission starts.
 * It does not cancel an in-progress SPI/HAL submission or bound hardware FIFO,
 * arbitration/retransmission time; it is not a wire delivery deadline. */
SRNOS_CANFDResult SRNOS_CANFDPostOneshot(SRNOS_CANChannel channel,
    const SRNOS_CANFDFrame *frame, uint8_t priority, uint32_t ttl_ms);
/* Task-context snapshot. No live internal pointers or remote reconfiguration. */
bool SRNOS_CANFDGetStats(SRNOS_CANChannel channel, SRNOS_CANFDStats *out);
#ifdef __cplusplus
}
static_assert(sizeof(SRNOS_CANFDFrame)==72u,"CAN FD frame ABI");
#else
_Static_assert(sizeof(SRNOS_CANFDFrame)==72u,"CAN FD frame ABI");
#endif
#endif
