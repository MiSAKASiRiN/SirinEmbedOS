#ifndef SRNOS_SIRIN_LINK_IMU_CALIBRATION_PROTOCOL_H
#define SRNOS_SIRIN_LINK_IMU_CALIBRATION_PROTOCOL_H
/* CONFIG service. Request [version=1, sensor_mask=1/2/3]. Final response after
 * sampling AND verified storage; no early success response. Status request is
 * empty. Both response bodies are 68 bytes, following the SirinLink status u16. */
#define SRNOS_IMU_CALIBRATION_COMMAND_MESSAGE 0x09u
#define SRNOS_IMU_CALIBRATION_STATUS_MESSAGE 0x0Au
#define SRNOS_IMU_CALIBRATION_PROTOCOL_VERSION 1u
#define SRNOS_IMU_CALIBRATION_RESPONSE_BYTES 68u
#endif
