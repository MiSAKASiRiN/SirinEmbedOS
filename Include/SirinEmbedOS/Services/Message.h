#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_MESSAGE_ROUTES 12u
#define SRNOS_MESSAGE_PAYLOAD_MAX 480u
#define SRNOS_MESSAGE_QUEUE_BYTES_MAX 8192u
enum {
    SRNOS_MESSAGE_HOST=1u, SRNOS_MESSAGE_UART=2u, SRNOS_MESSAGE_UDP=4u,
    SRNOS_MESSAGE_VIDEO=8u, SRNOS_MESSAGE_LOCAL=16u
};
typedef uint8_t SRNOS_MessageRoute;
typedef enum {
    SRNOS_MESSAGE_OK=0, SRNOS_MESSAGE_INVALID=-1, SRNOS_MESSAGE_FROZEN=-2,
    SRNOS_MESSAGE_FULL=-3, SRNOS_MESSAGE_CONTEXT=-4, SRNOS_MESSAGE_EMPTY=-5,
    SRNOS_MESSAGE_NO_ROUTE=-6, SRNOS_MESSAGE_BUFFER_SMALL=-7, SRNOS_MESSAGE_IO=-8,
    SRNOS_MESSAGE_REJECTED=-9
} SRNOS_MessageResult;
typedef struct {
    uint8_t transport;
    uint16_t endpoint; /* UART port ID, UDP local port, otherwise zero. */
    uint8_t peer_address[4];
    uint16_t peer_port; /* UDP peer, not authentication. Zero otherwise. */
} SRNOS_MessageOrigin;
typedef struct {
    SRNOS_MessageOrigin origin;
    uint32_t sequence, received_ms; /* Local SRNOS_TimeMs domain. */
    uint16_t length;
    uint8_t service, type;
} SRNOS_MessageInfo;
typedef enum { SRNOS_MESSAGE_FIFO=0, SRNOS_MESSAGE_LATEST=1 } SRNOS_MessagePolicy;
/* Borrowed payload, valid only during callback. Task context, outside SDK lock.
 * Must be bounded/nonblocking, no peripheral I/O, allocation, or recursive
 * dispatch. Different transports may call a handler concurrently; its user state
 * must tolerate that, or register independent handlers. Prefer a mailbox for application work. Callback errors are counted. */
typedef SRNOS_MessageResult (*SRNOS_MessageHandler)(const SRNOS_MessageInfo *,const uint8_t *,void *);
typedef struct {
    uint8_t transports, service, type;
    uint16_t endpoint; /* Zero matches every endpoint on selected transports. */
    uint16_t max_payload, max_age_ms; /* age 0: no expiration, for durable events. */
    uint8_t depth; /* Mailbox: 1..16; LATEST requires 1. Handler: zero. */
    SRNOS_MessagePolicy policy;
    SRNOS_MessageHandler handler;
    void *user;
} SRNOS_MessageRouteConfig;
typedef struct {
    uint32_t delivered, dropped, expired, replaced, handler_errors;
    uint16_t queued, high_water;
} SRNOS_MessageStats;
typedef struct {
    bool host_enabled; /* Existing HostComm transport. */
    SRNOS_SerialPortId uart_port; /* Dedicated COBS/CRC framed stream; NONE off. */
    uint16_t udp_port; /* Dedicated UDP listener; zero off, do not also declare in udp_ports[]. */
} SRNOS_MessageTransportConfig;
/* Startup registration copies config, freezes before workers. Matching routes
 * fan out independently; one task consumer per mailbox, explicit extra route
 * for each additional subscriber. Runtime allocation is forbidden. */
SRNOS_MessageResult SRNOS_MessageRegister(const SRNOS_MessageRouteConfig *,SRNOS_MessageRoute *);
/* Task-only injection. All matching routes are attempted. OK means at least
 * one accepted; inspect each route's stats for partial fanout loss. No callback
 * means copied delivery, not execution. No wire-level reliability guarantee. */
SRNOS_MessageResult SRNOS_MessageDispatch(const SRNOS_MessageInfo *,const void *payload);
SRNOS_MessageResult SRNOS_MessageReceive(SRNOS_MessageRoute,SRNOS_MessageInfo *,void *,size_t capacity);
SRNOS_MessageResult SRNOS_MessageGetStats(SRNOS_MessageRoute,SRNOS_MessageStats *);
/* Task-only, nonblocking copied TX. Host service/type must have a registered
 * HOST route (registers the corresponding TX topic). UART/UDP use SirinLink V2
 * EVENT frames; UART adds COBS + zero delimiter. UDP peer comes from destination.
 * Video uplink carries a V2 frame padded to 300 bytes (max payload 284).
 * UDP TX uses ~2.3 KiB of nested stack; budget >=4 KiB task stack and verify.
 * Success only means locally accepted, never peer receipt/execution. */
SRNOS_MessageResult SRNOS_MessageSend(const SRNOS_MessageOrigin *destination,
    uint8_t service,uint8_t type,uint32_t sequence,const void *payload,uint16_t length);
#ifdef __cplusplus
}
#endif
