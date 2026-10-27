#pragma once
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_INS_STREAM_ATTITUDE 0x0102u
#define SRNOS_INS_STREAM_MAGNETIC 0x0103u
#define SRNOS_INS_STREAM_PRESSURE 0x0104u
#define SRNOS_INS_TELEMETRY_STATUS_MESSAGE 0x09u
#define SRNOS_INS_ATTITUDE_RECORD_BYTES 108u
#define SRNOS_INS_MAGNETIC_RECORD_BYTES 52u
#define SRNOS_INS_PRESSURE_RECORD_BYTES 44u
SRNOS_SirinLinkResult SRNOS_INSTelemetryRegister(SRNOS_SirinLinkRegistration *,void *);
/* INS worker only: bounded copies, never encoding or transport I/O. */
void SRNOS_INSTelemetryObserve(const SRNOS_INSSnapshot *);
/* Portable wire encoders; capacity includes one record only. */
bool SRNOS_INSEncodeAttitude(const SRNOS_INSAttitude *,SRNOS_INSPhase,uint8_t *,size_t);
bool SRNOS_INSEncodeMagnetic(const SRNOS_MagneticSample *,uint8_t *,size_t);
bool SRNOS_INSEncodePressure(const SRNOS_PressureSample *,uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
