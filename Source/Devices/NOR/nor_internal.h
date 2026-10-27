#ifndef SRNOS_NOR_INTERNAL_H
#define SRNOS_NOR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_NOR_SIZE_BYTES (16U * 1024U * 1024U)
#define SRNOS_NOR_PAGE_BYTES 256U
#define SRNOS_NOR_SECTOR_BYTES 4096U

typedef enum SRNOS_NORStatus {
    SRNOS_NOR_OK,
    SRNOS_NOR_INVALID,
    SRNOS_NOR_UNSUPPORTED,
    SRNOS_NOR_PROTECTED,
    SRNOS_NOR_TIMEOUT,
    SRNOS_NOR_IO_ERROR
} SRNOS_NORStatus;

/* Private, single blackbox-worker owner. Call after the scheduler starts, with
 * interrupts enabled. Initialize is attempted once per boot; it cannot clear
 * a transport/write fault. No allocation, DMA, IRQ callbacks or chip erase.
 * jedec_id receives the observed 24-bit ID (zero if it could not be read).
 * Operational errors latch writes off until reboot; argument errors do not. */
SRNOS_NORStatus SRNOS_NORInitialize(uint32_t *jedec_id);
SRNOS_NORStatus SRNOS_NORRead(uint32_t address, void *data, size_t length);
/* Exactly 1..256 bytes, contained in one page; caller must erase beforehand
 * when changing any bit from 0 to 1. Success includes exact readback. */
SRNOS_NORStatus SRNOS_NORProgram(uint32_t address, const void *data, size_t length);
/* 4 KiB aligned; success includes verification of the entire erased sector. */
SRNOS_NORStatus SRNOS_NOREraseSector(uint32_t address);

#ifdef __cplusplus
}
#endif
#endif
