#ifndef SRNOS_CAN_CONTROLLER_H
#define SRNOS_CAN_CONTROLLER_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_CAN_CONTROLLER_COUNT 4u
typedef enum {
    SRNOS_CAN_MODE_CLASSIC = 0,
    SRNOS_CAN_MODE_FD = 1
} SRNOS_CANMode;
typedef struct {
    SRNOS_CANMode mode;
    uint32_t nominal_bitrate; /* bit/s: 125000, 250000, 500000 or 1000000. */
    uint32_t data_bitrate;    /* Classic: 0. FD without BRS: nominal_bitrate. */
    bool brs;                /* Classic: false. TX policy fixed at startup; RX reports actual BRS. */
} SRNOS_CANControllerConfig;
/* Pure validation, channels 1..4. FD+BRS data rates: 500k, 1M, 2M, 4M, 5M,
 * 8M, at least nominal_bitrate. Physical transceiver capability is board-dependent. */
bool SRNOS_CANControllerConfigValid(uint8_t channel, const SRNOS_CANControllerConfig *config);
/* Frozen copy only; false before configuration freeze or for invalid arguments. */
bool SRNOS_CANControllerGetConfig(uint8_t channel, SRNOS_CANControllerConfig *out);
#ifdef __cplusplus
}
#endif
#endif
