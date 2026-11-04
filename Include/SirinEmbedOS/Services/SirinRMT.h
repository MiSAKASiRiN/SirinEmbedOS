#pragma once
#include "SirinEmbedOS/Services/Input.h"
#include "SirinEmbedOS/Services/Message.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_SIRIN_RMT_BYTES 30u
#define SRNOS_SIRIN_RMT_SERVICE 0x21u
#define SRNOS_SIRIN_RMT_MESSAGE 1u
typedef enum {
    SRNOS_SIRIN_RMT_INVALIDATE=0, SRNOS_SIRIN_RMT_CHANNELS=1,
    SRNOS_SIRIN_RMT_BUTTONS=2, SRNOS_SIRIN_RMT_RELATIVE=3,
    SRNOS_SIRIN_RMT_ENABLE=4, SRNOS_SIRIN_RMT_DISABLE=5,
    SRNOS_SIRIN_RMT_PLANAR_VELOCITY=6
    /* Explicit requests; application owns actuator permission. */
} SRNOS_SirinRMTKind;
typedef struct {
    uint32_t session, sequence; /* Session nonzero, sequence monotonic modulo 2^32. */
    SRNOS_SirinRMTKind kind;
    SRNOS_InputFrame frame;
} SRNOS_SirinRMTPacket;
/* Pure codec, exact 30-byte payload. SRMT magic, version 1. No EJOY alias.
 * Channels: one contiguous page of 1..7 channels, -10000..10000.
 * Buttons: 64 validity bits + 64 held bits. Relative: mask + 3 int32 deltas.
 * ENABLE/DISABLE: zero body, publish request identity/time/epoch, no field refresh.
 * DISABLE preserves input validity and cancels an earlier ENABLE request.
 * PLANAR_VELOCITY: three finite LE IEEE754 float32 vx/vy/wz at 14/18/22,
 * then four zero bytes. Body-frame centre: m/s forward/left, rad/s CCW.
 * CHANNELS and PLANAR_VELOCITY are exclusive motion snapshots; switching
 * clears old motion validity/ENABLE and increments motion_epoch. No implicit enable.
 * All reserved bytes must be zero. Channel pages expire independently.
 * Integrity is supplied by the enclosing SirinLink/UART/UDP/VideoLink frame. */
bool SRNOS_SirinRMTDecode(const uint8_t *,size_t,SRNOS_SirinRMTPacket *);
bool SRNOS_SirinRMTEncode(const SRNOS_SirinRMTPacket *,uint8_t out[SRNOS_SIRIN_RMT_BYTES]);
/* Startup helper, at most four transport instances. Built-in startup config
 * registers sirinrmt.host/uart/udp/video. For VT02/VT12, register explicitly
 * with SRNOS_MESSAGE_VT02 and the bound Serial endpoint; the raw 30-byte
 * 0302 payload uses the VT02 custom route instead of service 0x21/type 1.
 * One source per transport endpoint.
 * The first fresh peer+session owns the source until timeout. Another peer or
 * session cannot merge into it; reconnect waits for timeout. This is ownership,
 * not authentication. Invalid/reordered traffic never renews the lease. */
SRNOS_InputResult SRNOS_SirinRMTRegister(const char *name,uint8_t transport,
    uint16_t endpoint,uint32_t timeout_ms,SRNOS_InputSource *);
#ifdef __cplusplus
}
#endif
