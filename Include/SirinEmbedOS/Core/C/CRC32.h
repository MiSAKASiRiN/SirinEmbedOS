#ifndef SRNOS_CORE_CRC32_H
#define SRNOS_CORE_CRC32_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Reflected CRC-32/ISO-HDLC, polynomial 0xEDB88320. Update takes unfinalized
 * state; start with INIT and XOR FINAL_XOR exactly once after the last chunk. */
#define SRNOS_CRC32_INIT UINT32_C(0xFFFFFFFF)
#define SRNOS_CRC32_FINAL_XOR UINT32_C(0xFFFFFFFF)
uint32_t SRNOS_CRC32Update(uint32_t state, const void *data, size_t length);
/* NULL/nonzero length returns zero; NULL/zero length is an empty CRC. */
uint32_t SRNOS_CRC32(const void *data, size_t length);
#ifdef __cplusplus
}
#endif
#endif
