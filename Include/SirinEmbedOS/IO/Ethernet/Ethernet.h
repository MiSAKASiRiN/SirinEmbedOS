#ifndef SRNOS_ETHERNET_H
#define SRNOS_ETHERNET_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_ETHERNET_FRAME_MAX 1518u
#define SRNOS_ETHERNET_QUEUE_DEPTH 4u

typedef struct {
    bool enabled; /* Startup-only. Default disabled; no PHY release or worker. */
    uint8_t mac[6]; /* All zero selects a UID-derived locally administered MAC. */
} SRNOS_EthernetConfig;
typedef enum {
    SRNOS_ETHERNET_DISABLED, SRNOS_ETHERNET_STARTING,
    SRNOS_ETHERNET_LINK_DOWN, SRNOS_ETHERNET_LINK_UP, SRNOS_ETHERNET_ERROR
} SRNOS_EthernetState;
typedef enum {
    SRNOS_ETHERNET_OK = 0, SRNOS_ETHERNET_INVALID = -1,
    SRNOS_ETHERNET_CONTEXT = -2, SRNOS_ETHERNET_NOT_READY = -3,
    SRNOS_ETHERNET_FULL = -4, SRNOS_ETHERNET_OWNED = -6, /* IPv4 service owns raw RX/TX. */
    SRNOS_ETHERNET_EMPTY = -5
} SRNOS_EthernetResult;
typedef struct {
    uint32_t received_ms; /* Monotonic modulo-2^32 SDK milliseconds. */
    uint16_t length;
    uint8_t data[SRNOS_ETHERNET_FRAME_MAX];
} SRNOS_EthernetFrame;
typedef struct {
    SRNOS_EthernetState state;
    uint8_t mac[6], phy_address;
    bool full_duplex;
    uint16_t speed_mbps, phy_id1, phy_id2;
    uint16_t phy_bcr, phy_bsr, phy_special, phy_anar, phy_anlpar, phy_modes;
    uint32_t ref_clock_hz; /* RCC-derived, not a measured waveform. */
    uint32_t link_epoch, link_changes, recoveries, last_error;
    uint32_t rx_frames, rx_dropped, rx_errors;
    uint32_t tx_accepted, tx_completed, tx_dropped, tx_errors;
} SRNOS_EthernetStatus;

bool SRNOS_EthernetConfigValid(const SRNOS_EthernetConfig *config);
/* Task context only. Bounded copying, no blocking, allocation or HAL access.
 * Send copies a complete Ethernet frame (14..1518 bytes, no FCS), whose source
 * must equal the configured MAC. Hardware adds padding/FCS. OK means queued,
 * not delivered. One RX consumer; multiple TX producers. Unicast to our MAC
 * and broadcast accepted; no promiscuous/multicast/IP stack. Receive includes
 * Ethernet padding, excludes FCS. Frames expire after 100 ms or a link change.
 * Both queue depths are four. Full queues drop/reject newest data. */
SRNOS_EthernetResult SRNOS_EthernetSend(const uint8_t *frame, uint16_t length);
SRNOS_EthernetResult SRNOS_EthernetReceive(SRNOS_EthernetFrame *frame);
bool SRNOS_EthernetGetStatus(SRNOS_EthernetStatus *status);
#ifdef __cplusplus
}
#endif
#endif
