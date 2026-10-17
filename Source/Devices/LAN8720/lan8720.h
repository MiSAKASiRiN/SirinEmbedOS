#ifndef SRNOS_LAN8720_INTERNAL_H
#define SRNOS_LAN8720_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
/* Microchip DS00002165: Clause 22 registers; PHY address is strap-selected. */
#define SRNOS_LAN8720_BCR 0u
#define SRNOS_LAN8720_BSR 1u
#define SRNOS_LAN8720_ID1 2u
#define SRNOS_LAN8720_ID2 3u
#define SRNOS_LAN8720_ANAR 4u
#define SRNOS_LAN8720_ANLPAR 5u
#define SRNOS_LAN8720_MODES 18u
#define SRNOS_LAN8720_SPECIAL 31u
#define SRNOS_LAN8720_RESTART_AUTONEG 0x1200u
static inline bool SRNOS_LAN8720Identify(uint32_t id1, uint32_t id2)
{ return id1 == 0x0007u && (id2 & 0xfff0u) == 0xc0f0u; }
static inline bool SRNOS_LAN8720Link(uint32_t bsr, uint32_t special,
                                    uint16_t *speed, bool *full)
{
    if ((bsr & 0x24u) != 0x24u || !(special & 0x1000u)) return false;
    switch ((special >> 2) & 7u) {
    case 1: *speed = 10; *full = false; return true;
    case 5: *speed = 10; *full = true; return true;
    case 2: *speed = 100; *full = false; return true;
    case 6: *speed = 100; *full = true; return true;
    default: return false;
    }
}
#endif
