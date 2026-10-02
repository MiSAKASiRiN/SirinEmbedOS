#ifndef SRNOS_TELEMETRY_H
#define SRNOS_TELEMETRY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_TELEMETRY_CAPACITY 32u
#define SRNOS_TELEMETRY_NAME_MAX 63u
#define SRNOS_TELEMETRY_UNIT_MAX 15u
#define SRNOS_TELEMETRY_RATE_MAX_HZ 1000u
#define SRNOS_TELEMETRY_INVALID_HANDLE UINT16_MAX
typedef uint16_t SRNOS_TelemetryHandle;
typedef enum { SRNOS_TELEMETRY_FLOAT32=1, SRNOS_TELEMETRY_INT32=2,
    SRNOS_TELEMETRY_UINT32=3, SRNOS_TELEMETRY_BOOL=4 } SRNOS_TelemetryType;
typedef union { float f32; int32_t i32; uint32_t u32; bool boolean; } SRNOS_TelemetryValue;
typedef enum { SRNOS_TELEMETRY_OK=0, SRNOS_TELEMETRY_INVALID=-1,
    SRNOS_TELEMETRY_FULL=-2, SRNOS_TELEMETRY_FROZEN=-3,
    SRNOS_TELEMETRY_CONTEXT=-4 } SRNOS_TelemetryResult;
typedef struct {
    const char *name; const char *unit; SRNOS_TelemetryType type; uint16_t max_hz;
} SRNOS_TelemetryDescriptor;
typedef struct {
    SRNOS_TelemetryValue value; uint32_t sequence; uint64_t sampled_us; bool valid;
} SRNOS_TelemetrySample;
/* Startup only, before directory freeze. SDK copies name/unit and descriptor.
 * Handles live for the firmware lifetime; no runtime unregister or pointer reads. */
SRNOS_TelemetryResult SRNOS_TelemetryRegister(const SRNOS_TelemetryDescriptor *, SRNOS_TelemetryHandle *);
/* Task context after freeze. Copies latest value/metadata under a short critical
 * section. Does not wake HostComm, allocate, log, perform I/O or wait for USB. */
SRNOS_TelemetryResult SRNOS_TelemetryPublish(SRNOS_TelemetryHandle, SRNOS_TelemetryValue,
    uint64_t sampled_us, bool valid);
#ifdef __cplusplus
}
#endif
#endif
