#ifndef SRNOS_BLACKBOX_RING_H
#define SRNOS_BLACKBOX_RING_H
#include "SirinEmbedOS/Services/Blackbox.h"

/* Private, synchronous single-owner engine. No RTOS, allocation or device
 * dependencies. Every callback runs in the service worker, never an ISR.
 * program must not cross a 256-byte page; erase always means exactly 4 KiB.
 * A false IO return is terminal for this mounted session. yield is optional
 * and must not re-enter the ring. All addresses are absolute NOR offsets. */
typedef struct {
    void *context;
    bool (*read)(void *context, uint32_t address, void *data, size_t bytes);
    bool (*program)(void *context, uint32_t address, const void *data, size_t bytes);
    bool (*erase)(void *context, uint32_t address);
    void (*yield)(void *context);
} SRNOS_BlackboxRingIO;
typedef struct { uint32_t base, size; } SRNOS_BlackboxRingConfig;
typedef struct {
    uint64_t oldest_sequence, newest_sequence, next_sequence;
    uint32_t capacity_records, data_sectors;
    bool mounted, volume_ready;
    SRNOS_BlackboxStatus fault;
} SRNOS_BlackboxRingSnapshot;
typedef struct {
    SRNOS_BlackboxRingIO io;
    SRNOS_BlackboxRingConfig config;
    uint64_t generation, oldest_sequence, newest_sequence;
    uint32_t data_sectors;
    uint8_t next_slot;
    bool mounted, volume_ready, has_sector;
    SRNOS_BlackboxStatus fault;
    uint8_t page[256], verify[256];
} SRNOS_BlackboxRing;

/* Production config is {PARTITION_BASE, PARTITION_BYTES}; smaller geometries
 * are supported for host tests, entirely inside that partition, with at least
 * three sectors (one permanent volume + at least two circular data sectors).
 * Mount is read-only. An unformatted/compatible interrupted volume can be
 * initialized by the first Append only after the rest is proven erased.
 * Once the volume is valid it owns the ring: damaged data headers are skipped
 * and may subsequently be erased as an ordinary circular target. A committed
 * record under an invalid header that proves a generation at/above the newest
 * valid header instead returns CORRUPT without mutation, preventing sequence
 * reuse. Mount can read the whole data area when headers are invalid/blank. */
SRNOS_BlackboxStatus SRNOS_BlackboxRingMount(SRNOS_BlackboxRing *ring,
    const SRNOS_BlackboxRingIO *io, const SRNOS_BlackboxRingConfig *config);
SRNOS_BlackboxStatus SRNOS_BlackboxRingAppend(SRNOS_BlackboxRing *ring,
    uint16_t type, uint64_t timestamp_us, const void *data, size_t length,
    uint64_t *sequence);
SRNOS_BlackboxStatus SRNOS_BlackboxRingReadAfter(SRNOS_BlackboxRing *ring,
    uint64_t after_sequence, SRNOS_BlackboxRecord *record);
void SRNOS_BlackboxRingGetSnapshot(const SRNOS_BlackboxRing *ring,
    SRNOS_BlackboxRingSnapshot *snapshot);
#endif
