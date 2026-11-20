#ifndef SRNOS_MP257_MOTOR_EXCHANGE_H
#define SRNOS_MP257_MOTOR_EXCHANGE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
enum {
    SRNOS_MP257_MOTOR_IDLE = 0,
    SRNOS_MP257_MOTOR_PREVIEW = 1,
    SRNOS_MP257_MOTOR_DRIVE = 2
};
typedef struct {
    uint32_t now_ms, session, continuity, mode, valid_mask;
    int32_t target_mdeg[16];
} SRNOS_MP257MotorInput;

/* Optional last speed trip. sources bit 0: reported rpm, bit 1: filtered
 * encoder speed. A zero mask means no capture. The application retains this
 * sample across subsequent cycles. output_raw is the last posted command. */
typedef struct {
    uint32_t sources, cycle_ms;
    int32_t rpm, speed_mdeg_s, offset_mdeg, output_raw;
} SRNOS_MP257MotorSpeedTrip;

typedef struct {
    uint32_t profile, state, fault, feedback_age_ms;
    int32_t measured_mdeg, rpm, reference_mdeg, limited_offset_mdeg, output_raw;
    uint32_t measured_count;
    SRNOS_MP257MotorSpeedTrip speed_trip;
} SRNOS_MP257MotorObservation;

/* Optional M33 application callback on the 1 ms IPC thread. Buffers live only
 * for this call. A new writer changes session. TTL expiry changes continuity.
 * Only an explicit drive request yields DRIVE. Handle IDLE every cycle and
 * bound application output locally. The default implementation produces no TX. */
void SRNOS_MP257MotorStep(const SRNOS_MP257MotorInput *input, SRNOS_MP257MotorObservation *out);
#ifdef __cplusplus
}
#endif
#endif
