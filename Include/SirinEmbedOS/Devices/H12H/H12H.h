#ifndef SRNOS_SERIAL_SERVO_H12H_H
#define SRNOS_SERIAL_SERVO_H12H_H

#include "SirinEmbedOS/IO/Serial/Serial.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_SERIAL_SERVO_H12H_OK                          0
#define SRNOS_SERIAL_SERVO_H12H_ERR_ARG                     -1
#define SRNOS_SERIAL_SERVO_H12H_ERR_LOCK                    -2
#define SRNOS_SERIAL_SERVO_H12H_ERR_TX                      -3
#define SRNOS_SERIAL_SERVO_H12H_ERR_RX_TIMEOUT              -4
#define SRNOS_SERIAL_SERVO_H12H_ERR_CHECKSUM                -5
#define SRNOS_SERIAL_SERVO_H12H_ERR_LEN                     -6
#define SRNOS_SERIAL_SERVO_H12H_ERR_ID                      -7
#define SRNOS_SERIAL_SERVO_H12H_ERR_CMD                     -8

#define SRNOS_SERIAL_SERVO_H12H_BROADCAST_ID               0xFEu
#define SRNOS_SERIAL_SERVO_H12H_FRAME_HEADER               0x55u

#ifndef SRNOS_SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS
#define SRNOS_SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS         1u
#endif

#ifndef SRNOS_SERIAL_SERVO_H12H_POS_MAX
#define SRNOS_SERIAL_SERVO_H12H_POS_MAX                    1500u
#endif

#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WRITE        1u
#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_READ         2u
#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_WRITE   7u
#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_READ    8u
#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_START             11u
#define SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_STOP              12u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ID_WRITE               13u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ID_READ                14u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_ADJUST    17u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_WRITE     18u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_READ      19u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_WRITE      20u
#define SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_READ       21u
#define SRNOS_SERIAL_SERVO_H12H_CMD_VIN_LIMIT_WRITE        22u
#define SRNOS_SERIAL_SERVO_H12H_CMD_VIN_LIMIT_READ         23u
#define SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_WRITE   24u
#define SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_READ    25u
#define SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_READ              26u
#define SRNOS_SERIAL_SERVO_H12H_CMD_VIN_READ               27u
#define SRNOS_SERIAL_SERVO_H12H_CMD_POS_READ               28u
#define SRNOS_SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE    29u
#define SRNOS_SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_READ     30u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_WRITE   31u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_READ    32u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LED_CTRL_WRITE         33u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LED_CTRL_READ          34u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LED_ERROR_WRITE        35u
#define SRNOS_SERIAL_SERVO_H12H_CMD_LED_ERROR_READ         36u
#define SRNOS_SERIAL_SERVO_H12H_CMD_DIS_READ               48u

typedef enum {
  SRNOS_SERIAL_SERVO_H12H_MODE_POSITION = 0,
  SRNOS_SERIAL_SERVO_H12H_MODE_MOTOR = 1
} SRNOS_SerialServoH12hMode;

/* One protocol bus instance serves all servo IDs. Initialize before SerialStart.
 * Uses a board-validated full-duplex adapter or fixed RS485 path; does not
 * change a GPIO into a speculative one-wire electrical interface. */
typedef struct { SRNOS_SerialChannel *channel; uint32_t timeout_ms; } SRNOS_SerialServoH12h;

typedef struct {
  uint8_t id;
  int16_t position;
  int8_t deviation;
  uint16_t angle_limit_min;
  uint16_t angle_limit_max;
  uint16_t vin_limit_min;
  uint16_t vin_limit_max;
  uint16_t vin_mv;
  uint8_t temp_limit;
  uint8_t temp_c;
  uint8_t load_unload;
  SRNOS_SerialServoH12hMode mode;
  int16_t motor_speed;
  uint16_t move_time_pos;
  uint16_t move_time_ms;
  uint16_t wait_time_pos;
  uint16_t wait_time_ms;
  uint8_t led_ctrl;
  uint8_t led_error;
  uint16_t distance;
} SRNOS_SerialServoH12hInfo;

int SRNOS_SerialServoH12hInit(SRNOS_SerialServoH12h* self, SRNOS_SerialChannel* channel, uint32_t timeout_ms);

int SRNOS_SerialServoH12hReadId(SRNOS_SerialServoH12h* self, uint8_t query_id, uint8_t* out_id);
int SRNOS_SerialServoH12hWriteId(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t new_id);
int SRNOS_SerialServoH12hSetPosition(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms);
int SRNOS_SerialServoH12hMoveTimeRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms);
int SRNOS_SerialServoH12hMoveTimeWaitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms);
int SRNOS_SerialServoH12hMoveTimeWaitRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms);
int SRNOS_SerialServoH12hMoveStart(SRNOS_SerialServoH12h* self, uint8_t servo_id);
int SRNOS_SerialServoH12hMoveStop(SRNOS_SerialServoH12h* self, uint8_t servo_id);
int SRNOS_SerialServoH12hReadPosition(SRNOS_SerialServoH12h* self, uint8_t servo_id, int16_t* position);
int SRNOS_SerialServoH12hAngleOffsetAdjust(SRNOS_SerialServoH12h* self, uint8_t servo_id, int8_t offset);
int SRNOS_SerialServoH12hAngleOffsetWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id);
int SRNOS_SerialServoH12hReadDeviation(SRNOS_SerialServoH12h* self, uint8_t servo_id, int8_t* deviation);
int SRNOS_SerialServoH12hAngleLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t min, uint16_t max);
int SRNOS_SerialServoH12hReadAngleLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* min, uint16_t* max);
int SRNOS_SerialServoH12hVinLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t min_mv, uint16_t max_mv);
int SRNOS_SerialServoH12hReadVinLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* min_mv, uint16_t* max_mv);
int SRNOS_SerialServoH12hReadVin(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* vin_mv);
int SRNOS_SerialServoH12hTempLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t temp_limit);
int SRNOS_SerialServoH12hReadTempLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* temp_limit);
int SRNOS_SerialServoH12hReadTemp(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* temp_c);
int SRNOS_SerialServoH12hLoadWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t load_unload);
int SRNOS_SerialServoH12hReadLoad(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* load_unload);
int SRNOS_SerialServoH12hLedCtrlWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t led_ctrl);
int SRNOS_SerialServoH12hLedCtrlRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* led_ctrl);
int SRNOS_SerialServoH12hLedErrorWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t led_error);
int SRNOS_SerialServoH12hLedErrorRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* led_error);
int SRNOS_SerialServoH12hReadDistance(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* distance);

int SRNOS_SerialServoH12hSetModePosition(SRNOS_SerialServoH12h* self, uint8_t servo_id);
int SRNOS_SerialServoH12hSetModeMotor(SRNOS_SerialServoH12h* self, uint8_t servo_id, int16_t speed);
int SRNOS_SerialServoH12hReadMode(SRNOS_SerialServoH12h* self, uint8_t servo_id, SRNOS_SerialServoH12hMode* mode, int16_t* speed);

int SRNOS_SerialServoH12hReadAllInfo(SRNOS_SerialServoH12h* self, uint8_t servo_id, SRNOS_SerialServoH12hInfo* info);
float SRNOS_SerialServoH12hPositionToDeg(int16_t position);

#ifdef __cplusplus
}
#endif

#endif



