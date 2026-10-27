#ifndef SRNOS_SERVICES_BLACKBOX_H
#define SRNOS_SERVICES_BLACKBOX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_BLACKBOX_PAYLOAD_MAX 224u
#define SRNOS_BLACKBOX_PARTITION_BASE 0x00100000u
#define SRNOS_BLACKBOX_PARTITION_BYTES (12u * 1024u * 1024u)
#define SRNOS_BLACKBOX_QUEUE_DEFAULT 32u

typedef enum {
    SRNOS_BLACKBOX_OK = 0, SRNOS_BLACKBOX_DISABLED, SRNOS_BLACKBOX_NOT_READY,
    SRNOS_BLACKBOX_FULL, SRNOS_BLACKBOX_BUSY, SRNOS_BLACKBOX_EMPTY,
    SRNOS_BLACKBOX_INVALID, SRNOS_BLACKBOX_CONTEXT, SRNOS_BLACKBOX_IO_ERROR,
    SRNOS_BLACKBOX_CORRUPT, SRNOS_BLACKBOX_INCOMPATIBLE
} SRNOS_BlackboxStatus;
typedef enum {
    SRNOS_BLACKBOX_STATE_DISABLED = 0, SRNOS_BLACKBOX_STATE_STARTING,
    SRNOS_BLACKBOX_STATE_READY, SRNOS_BLACKBOX_STATE_FAULT
} SRNOS_BlackboxState;
typedef struct {
    bool enabled;          /* Default false. Startup-only, copied and frozen. */
    uint16_t queue_depth;  /* 8..64 records; default 32. No runtime allocation. */
} SRNOS_BlackboxConfig;
typedef struct {
    uint64_t sequence;     /* Persistent ordering; gaps are valid. */
    uint64_t timestamp_us; /* Producer monotonic time; resets at each boot. */
    uint16_t type;         /* Nonzero, application-defined schema identifier. */
    uint16_t length;
    uint8_t data[SRNOS_BLACKBOX_PAYLOAD_MAX];
} SRNOS_BlackboxRecord;
typedef struct {
    SRNOS_BlackboxState state;
    SRNOS_BlackboxStatus last_status;
    uint32_t jedec_id;
    uint32_t capacity_records;
    uint32_t accepted, committed, dropped_full, rejected_not_ready;
    uint32_t discarded_fault; /* Accepted records abandoned after a device fault. */
    uint32_t write_errors, read_completed, read_errors;
    uint16_t queued, queue_capacity, queue_high_water;
    uint64_t oldest_sequence, newest_sequence;
} SRNOS_BlackboxSnapshot;

void SRNOS_BlackboxDefaultConfig(SRNOS_BlackboxConfig *config);
/* Task context with interrupts enabled only. Bounded copy, no Flash access,
 * waiting or allocation. OK means queued, NOT durable. Full drops the new
 * record; the caller may immediately reuse its buffer on every return path.
 * The SDK worker alone mounts, reads, programs and erases the fixed partition.
 * Store boot ID/schema/time-validity in the payload when cross-boot decoding
 * needs them; timestamp_us alone does not establish a common time origin. */
SRNOS_BlackboxStatus SRNOS_BlackboxAppend(uint16_t type, uint64_t timestamp_us,
    const void *data, size_t length);
/* One asynchronous read slot across all callers. Read the first surviving,
 * valid record with sequence > after_sequence; 0 starts at the oldest. Ring
 * overwrite, interrupted writes and corrupt records can produce gaps. There
 * is no pinned snapshot: later reads can miss data overwritten in between.
 * The request copies the cursor and retains NO caller pointers. BUSY means
 * another request/result occupies the slot. A nonzero ticket identifies it.
 * Poll returns BUSY while queued/in progress. OK copies a record, EMPTY marks
 * the current end; any completed result consumes the slot. INVALID (including
 * a NULL output) does not consume it. Cancel releases the ticket immediately;
 * an already-running read may finish internally but cannot publish to it.
 * Do not poll in a busy loop; wait/yield in the application's normal cadence. */
SRNOS_BlackboxStatus SRNOS_BlackboxRequestRead(uint64_t after_sequence, uint32_t *ticket);
SRNOS_BlackboxStatus SRNOS_BlackboxPollRead(uint32_t ticket, SRNOS_BlackboxRecord *record);
SRNOS_BlackboxStatus SRNOS_BlackboxCancelRead(uint32_t ticket);
/* Task-context RAM snapshot only, never Flash I/O. No device health claim is
 * implied by service startup; use state/last_status and committed counters. */
bool SRNOS_BlackboxGetSnapshot(SRNOS_BlackboxSnapshot *out);

#ifdef __cplusplus
}
#endif
#endif
