#ifndef SRNOS_NETWORK_H
#define SRNOS_NETWORK_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_UDP_PORT_LIMIT 4u
#define SRNOS_UDP_PAYLOAD_MAX 1472u
#define SRNOS_UDP_TX_DEPTH 2u
#define SRNOS_UDP_LIFETIME_MS 100u
typedef enum {
    SRNOS_NETWORK_OK = 0, SRNOS_NETWORK_INVALID = -1,
    SRNOS_NETWORK_CONTEXT = -2, SRNOS_NETWORK_NOT_READY = -3,
    SRNOS_NETWORK_FULL = -4, SRNOS_NETWORK_EMPTY = -5
} SRNOS_NetworkResult;
typedef struct {
    uint8_t source_address[4];
    uint16_t source_port, length;
    uint32_t received_ms;
    uint8_t data[SRNOS_UDP_PAYLOAD_MAX];
} SRNOS_UDPDatagram;
typedef enum {
    SRNOS_UDP_RX_DROP = 0, SRNOS_UDP_RX_KEEP_FIRST,
    SRNOS_UDP_RX_LATEST, SRNOS_UDP_RX_LATCH
} SRNOS_UDPReceiveAction;
/** Optional pure selector, called by the Network worker outside critical sections.
 * Both pointers are borrowed for this call only; queued may be NULL or may have
 * been consumed since its snapshot. Must be bounded/nonblocking: no allocation,
 * logging, network API calls, or application control-state mutation.
 * LATCH is for idempotent events that remain valid indefinitely: it replaces the
 * queued value and cannot be displaced by ordinary packets. Repeated LATCH
 * events coalesce to the latest event. Do not use LATCH for motion/setpoints. */
typedef SRNOS_UDPReceiveAction (*SRNOS_UDPReceiveSelector)(
    const SRNOS_UDPDatagram *incoming, const SRNOS_UDPDatagram *queued);
/** Optional SNTPv4 client for a trusted local NTP server (UDP 123). No DNS,
 * broadcast, authentication, RTC or persistence. Configuration is frozen.
 * poll_interval_ms: 0 = stop after first success; otherwise 15000..86400000.
 * All-zero defaults leave NTP disabled; enabled requires a routable server. */
typedef struct {
    bool enabled;
    uint8_t server_address[4];
    uint32_t poll_interval_ms;
} SRNOS_NTPConfig;
typedef struct {
    uint32_t requests, accepted, rejected, timeouts, send_errors, kiss_of_death;
    bool pending, denied;
} SRNOS_NTPStatus;
/** Startup-only configuration. Defaults: disabled, 192.168.1.50/24, no gateway,
 * no UDP listeners. Enable Ethernet too. Ports are unique, nonzero, frozen.
 * IPv4 unicast only; no TCP, DHCP, DNS, IPv6, forwarding or IP fragmentation. */
typedef struct {
    bool enabled;
    uint8_t address[4], netmask[4], gateway[4];
    uint16_t udp_ports[SRNOS_UDP_PORT_LIMIT];
    uint8_t udp_port_count;
    /** Per-port opt-in. NULL preserves full-drops-newest behavior. */
    SRNOS_UDPReceiveSelector udp_rx_select[SRNOS_UDP_PORT_LIMIT];
    SRNOS_NTPConfig ntp;
} SRNOS_NetworkConfig;
typedef struct {
    bool enabled, ready, link_up;
    uint8_t address[4], netmask[4], gateway[4];
    uint32_t link_epoch;
    uint32_t rx_frames, rx_dropped, udp_received, udp_rx_dropped;
    uint32_t udp_accepted, udp_submitted, udp_tx_dropped;
    uint32_t tx_frames, tx_errors;
    uint32_t udp_rx_replaced, udp_rx_latched;
    SRNOS_NTPStatus ntp;
} SRNOS_NetworkStatus;
bool SRNOS_NetworkConfigValid(const SRNOS_NetworkConfig *config);
/** Task-only, nonblocking copy. OK means queued, not delivered. local_port must
 * be declared at startup. Reject multicast/broadcast, zero destination port,
 * oversized payloads. data may be NULL only for a zero-length datagram.
 * Queued/ARP-pending UDP expires after 100 ms or a link epoch change; Ethernet
 * then has its own <=100 ms queue lifetime. No network delivery guarantee. */
SRNOS_NetworkResult SRNOS_UDPSend(uint16_t local_port, const uint8_t destination[4],
    uint16_t destination_port, const void *data, uint16_t length);
/** One task consumer per port; never blocks. Default single-slot RX drops newest
 * when full, and expires after 100 ms/on link changes. An optional startup
 * selector can reject, replace latest, or latch an event. A latched event blocks
 * ordinary packets until consumed, survives age/link changes, and can be read
 * while the link is down. Its received_ms remains the original receipt time.
 * This protects only datagrams delivered by the stack, not upstream packet loss. */
SRNOS_NetworkResult SRNOS_UDPReceive(uint16_t local_port, SRNOS_UDPDatagram *datagram);
/** Task-only snapshot. submitted means accepted by stack/Ethernet queue, not
 * transmitted or acknowledged. See EthernetGetStatus for MAC completion. */
bool SRNOS_NetworkGetStatus(SRNOS_NetworkStatus *status);
#ifdef __cplusplus
}
#endif
#endif
