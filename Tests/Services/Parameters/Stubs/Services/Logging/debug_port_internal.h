#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_DEBUG_PORT_WRITE_OK 0
int SRNOS_DebugPortWriteExact(const void *, size_t);
#ifdef __cplusplus
}
#endif
