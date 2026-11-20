#ifndef SRNOS_MP257_IPC_ABI_H
#define SRNOS_MP257_IPC_ABI_H

/* Little-endian AArch64/Arm EABI. All shared accesses are aligned 32-bit words. */
#define SRNOS_IPC_MAGIC 0x53495043u
#define SRNOS_IPC_VERSION 1u
#define SRNOS_IPC_M33 33u
#define SRNOS_IPC_M0 0u
#define SRNOS_IPC_M33_ADDRESS 0x81201000u
#define SRNOS_IPC_M0_ADDRESS  0x200c3c00u
#define SRNOS_IPC_CHANNELS 16u
#define SRNOS_IPC_WORDS 63u
#define SRNOS_IPC_MAX_TTL_MS 5000u
#define SRNOS_IPC_HELLO 1u
#define SRNOS_IPC_TARGET 2u
#define SRNOS_IPC_UART 3u
#define SRNOS_IPC_ANGLE_PREVIEW 4u
#define SRNOS_IPC_ANGLE_DRIVE 5u
#define SRNOS_IPC_CONTROL_MAX_TTL_MS 100u
#define SRNOS_IPC_STATUS_OFFSET 256u

typedef unsigned int SRNOS_IpcWord;
typedef struct {
    SRNOS_IpcWord op, ttl_ms, mask_or_length;
    /* TARGET: 16 signed millidegree bit patterns. UART: up to 64 opaque bytes. */
    SRNOS_IpcWord data[SRNOS_IPC_CHANNELS];
} SRNOS_IpcRequest;
typedef struct {
    SRNOS_IpcWord session[2], issued_ms;
    SRNOS_IpcRequest request;
} SRNOS_IpcCommand;
typedef struct {
    SRNOS_IpcWord uptime_ms, accepted, expired, rejected, active;
    SRNOS_IpcWord can_ready_mask, can_rx[3], can_tx_done[3];
    SRNOS_IpcWord uart_bytes, uart_errors, frames, discarded;
    SRNOS_IpcWord uart_dry_run_len, uart_dry_run_data[SRNOS_IPC_CHANNELS];
} SRNOS_IpcStatus;
typedef struct {
    SRNOS_IpcWord frame_ms, frames, flags, channels[SRNOS_IPC_CHANNELS];
} SRNOS_IpcRadio;
typedef struct {
    SRNOS_IpcWord cycle_ms, accepted, expired, active_mask;
    SRNOS_IpcWord dry_run_mdeg[SRNOS_IPC_CHANNELS];
    SRNOS_IpcWord measured_count;
    SRNOS_IpcWord profile, state, fault, feedback_age_ms;
    SRNOS_IpcWord measured_mdeg, rpm, reference_mdeg, limited_offset_mdeg, output_raw;
    SRNOS_IpcWord trip_sources, trip_cycle_ms, trip_rpm, trip_speed_mdeg_s, trip_offset_mdeg, trip_output_raw;
} SRNOS_IpcMotor;
typedef union {
    SRNOS_IpcWord words[SRNOS_IPC_WORDS];
    SRNOS_IpcCommand command;
    SRNOS_IpcRadio radio;
    SRNOS_IpcMotor motor;
    SRNOS_IpcStatus status;
} SRNOS_IpcData;
typedef struct {
    SRNOS_IpcWord mark;
    SRNOS_IpcData data;
} SRNOS_IpcSlot;
typedef struct {
    SRNOS_IpcWord magic, version, core, size, now_ms, session[2], reserved[9];
    SRNOS_IpcSlot up, down, status;
} SRNOS_IpcPage;
/* pread returns one coherent slot including its mark, plus the peer clock. */
typedef struct {
    SRNOS_IpcSlot slot;
    SRNOS_IpcWord now_ms;
} SRNOS_IpcRead;
_Static_assert(sizeof(SRNOS_IpcWord) == 4 && sizeof(SRNOS_IpcSlot) == 256, "IPC wire ABI");
_Static_assert(sizeof(SRNOS_IpcPage) == 832 && sizeof(SRNOS_IpcRead) == 260, "IPC layout");
#endif
