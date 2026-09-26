#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRN_DEBUG_PORT_WRITE_OK 0
int SRN_DebugPort_WriteExact(const void *, size_t);
#ifdef __cplusplus
}
#endif
