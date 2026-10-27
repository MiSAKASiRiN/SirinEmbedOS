#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_INPUT_SOURCES 8u
#define SRNOS_INPUT_CHANNELS 16u
#define SRNOS_INPUT_EVENT_DEPTH 8u
#define SRNOS_INPUT_INVALID_SOURCE UINT8_MAX
typedef uint8_t SRNOS_InputSource;
typedef enum {
    SRNOS_INPUT_OK=0, SRNOS_INPUT_INVALID=-1, SRNOS_INPUT_FROZEN=-2,
    SRNOS_INPUT_FULL=-3, SRNOS_INPUT_CONTEXT=-4, SRNOS_INPUT_EMPTY=-5,
    SRNOS_INPUT_OVERRUN=-6, SRNOS_INPUT_STALE=-7
} SRNOS_InputResult;
typedef enum {
    SRNOS_INPUT_CHANNEL_RAW=0, SRNOS_INPUT_CHANNEL_AXIS, SRNOS_INPUT_CHANNEL_POSITION
} SRNOS_InputChannelKind;
typedef struct {
    int16_t minimum, center, maximum; /* Strictly ordered enables normalization. */
    uint8_t kind; /* POSITION retains device position codes in raw[]. */
    bool inverted;
} SRNOS_InputChannelDescriptor;
typedef struct {
    char name[32];
    uint32_t timeout_ms;
    uint64_t buttons; /* Supported button IDs; never infer support from value. */
    uint8_t channel_count, relative_mask;
    SRNOS_InputChannelDescriptor channels[SRNOS_INPUT_CHANNELS];
    bool planar_velocity; /* Supports body-frame SI velocity snapshots. */
} SRNOS_InputDescriptor;
typedef struct { float vx,vy,wz; } SRNOS_InputPlanarVelocity;
typedef enum {
    SRNOS_INPUT_MOTION_NONE=0, SRNOS_INPUT_MOTION_CHANNELS=1,
    SRNOS_INPUT_MOTION_PLANAR_VELOCITY=2
} SRNOS_InputMotionKind;
enum {
    SRNOS_INPUT_FRAME_LOST=1u, SRNOS_INPUT_FAILSAFE=2u, SRNOS_INPUT_DISABLED=4u,
    SRNOS_INPUT_EVENT_SYNC=8u
};
/* Stable button IDs; other IDs are profile-defined. */
typedef enum {
    SRNOS_INPUT_KEY_W=0, SRNOS_INPUT_KEY_S, SRNOS_INPUT_KEY_A, SRNOS_INPUT_KEY_D,
    SRNOS_INPUT_KEY_SHIFT, SRNOS_INPUT_KEY_CTRL, SRNOS_INPUT_KEY_Q, SRNOS_INPUT_KEY_E,
    SRNOS_INPUT_KEY_R, SRNOS_INPUT_KEY_F, SRNOS_INPUT_KEY_G, SRNOS_INPUT_KEY_Z,
    SRNOS_INPUT_KEY_X, SRNOS_INPUT_KEY_C, SRNOS_INPUT_KEY_V, SRNOS_INPUT_KEY_B,
    SRNOS_INPUT_MOUSE_LEFT, SRNOS_INPUT_MOUSE_RIGHT, SRNOS_INPUT_MOUSE_MIDDLE,
    SRNOS_INPUT_VT03_PAUSE, SRNOS_INPUT_VT03_FN1, SRNOS_INPUT_VT03_FN2, SRNOS_INPUT_VT03_TRIGGER,
    SRNOS_INPUT_WBUS_DIGITAL17=32, SRNOS_INPUT_WBUS_DIGITAL18
} SRNOS_InputButton;
#define SRNOS_INPUT_BUTTON_MASK(id) (UINT64_C(1) << (id))
typedef struct {
    uint16_t channel_mask;
    int16_t raw[SRNOS_INPUT_CHANNELS];
    bool update_buttons;
    uint64_t buttons_valid, buttons;
    uint8_t relative_mask;
    int32_t relative[3]; /* Delta, applied once; never repeat from a snapshot. */
    uint8_t flags; /* LOST/FAILSAFE/DISABLED, no application arming semantics. */
    bool request_enable; /* Explicit intent only; application owns permission. */
    bool request_disable; /* Preserve fields, cancel earlier enable; mutually exclusive. */
    bool update_planar_velocity; /* Exclusive with channel_mask. All three axes at once. */
    SRNOS_InputPlanarVelocity planar_velocity; /* m/s forward, m/s left, rad/s CCW. */
} SRNOS_InputFrame;
typedef struct {
    uint32_t sequence, epoch, received_ms, event_sequence;
    uint16_t channels_valid, normalized_valid;
    int16_t raw[SRNOS_INPUT_CHANNELS];
    int16_t normalized[SRNOS_INPUT_CHANNELS]; /* -10000..10000 = -100.00..100.00%. */
    uint32_t channel_ms[SRNOS_INPUT_CHANNELS];
    uint64_t buttons_valid, buttons;
    uint32_t buttons_ms, relative_ms, relative_total[3]; /* Modulo 2^32 counters. */
    /* Non-consuming request identity. Only an explicit request increments it.
     * enable_epoch==epoch identifies a request in this live source generation.
     * No channel/button update renews enable_ms; invalidation clears enable_epoch. */
    uint32_t enable_sequence, enable_ms, enable_epoch;
    /* Independent stop request identity; survives subsequent input/enable frames.
     * Invalidation/resync clears disable_epoch, not the monotonic counter. */
    uint32_t disable_sequence, disable_ms, disable_epoch;
    uint8_t relative_valid, flags;
    bool seen, online;
    SRNOS_InputPlanarVelocity planar_velocity;
    uint32_t planar_velocity_ms, motion_epoch; /* Epoch increments on each kind change. */
    uint8_t motion_kind;
    bool planar_velocity_valid; /* Independently expires; other fields never refresh it. */
} SRNOS_InputSnapshot;
typedef struct {
    uint32_t sequence, epoch, received_ms;
    uint64_t pressed, released;
    int32_t relative[3];
    uint8_t relative_mask, flags;
} SRNOS_InputEvent;
/* Startup only: copies descriptor, unique name, registration freezes in Kernel.
 * Storage is allocated once after kernel initialization, never on publication. */
SRNOS_InputResult SRNOS_InputRegister(const SRNOS_InputDescriptor *, SRNOS_InputSource *);
uint8_t SRNOS_InputCount(void);
SRNOS_InputSource SRNOS_InputFind(const char *name);
bool SRNOS_InputDescribe(SRNOS_InputSource, SRNOS_InputDescriptor *);
/* Task-only, one producer per source. received_ms MUST use SRNOS_TimeMs().
 * Updates can be partial; each field group expires independently. Invalid frames
 * invalidate the source. First frame/recovery produces SYNC without key edges.
 * Duplicate/order/session validation belongs to the producer's wire protocol. */
SRNOS_InputResult SRNOS_InputPublish(SRNOS_InputSource, const SRNOS_InputFrame *, uint32_t received_ms);
SRNOS_InputResult SRNOS_InputInvalidate(SRNOS_InputSource, uint8_t flags);
/* Non-consuming coherent data + status snapshot. Expired raw values remain
 * inspectable but their valid bits are cleared. No source arbitration. */
SRNOS_InputResult SRNOS_InputReadSnapshot(SRNOS_InputSource, SRNOS_InputSnapshot *);
/* Each reader owns a cursor (last consumed event sequence). Set it to a current
 * snapshot.event_sequence to start now; zero replays from startup if retained.
 * OVERRUN advances cursor to just before oldest retained event, leaves out
 * untouched; next call resumes. No guarantee for events lost upstream. */
SRNOS_InputResult SRNOS_InputReadEvent(SRNOS_InputSource, uint32_t *cursor, SRNOS_InputEvent *);
bool SRNOS_InputNormalize(const SRNOS_InputChannelDescriptor *, int16_t raw, int16_t *normalized);
#ifdef __cplusplus
}
#endif
