#pragma once
#include "SirinEmbedOS/Services/Telemetry.h"
#ifdef __cplusplus
extern "C" {
#endif
bool SRNOS_TelemetryFreeze(void);
uint16_t SRNOS_TelemetryCount(void);
/* Frozen immutable descriptor; lifetime is firmware lifetime. */
const SRNOS_TelemetryDescriptor *SRNOS_TelemetryDescribe(uint16_t index);
bool SRNOS_TelemetryRead(uint16_t index,SRNOS_TelemetrySample *out);
#ifdef __cplusplus
}
#endif
