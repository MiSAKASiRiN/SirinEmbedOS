#ifndef LIB_SERIAL_SERVO_H12H_H
#define LIB_SERIAL_SERVO_H12H_H

#include "SirinEmbedOS/IO/Serial/Serial.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERIAL_SERVO_H12H_OK                          0
#define SERIAL_SERVO_H12H_ERR_ARG                     -1
#define SERIAL_SERVO_H12H_ERR_LOCK                    -2
#define SERIAL_SERVO_H12H_ERR_TX                      -3
#define SERIAL_SERVO_H12H_ERR_RX_TIMEOUT              -4
#define SERIAL_SERVO_H12H_ERR_CHECKSUM                -5
#define SERIAL_SERVO_H12H_ERR_LEN                     -6
#define SERIAL_SERVO_H12H_ERR_ID                      -7
#define SERIAL_SERVO_H12H_ERR_CMD                     -8

#define SERIAL_SERVO_H12H_BROADCAST_ID               0xFEu
#define SERIAL_SERVO_H12H_FRAME_HEADER               0x55u

#ifndef SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS
#define SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS         1u
#endif

#ifndef SERIAL_SERVO_H12H_POS_MAX
#define SERIAL_SERVO_H12H_POS_MAX                    1500u
#endif

#define SERIAL_SERVO_H12H_CMD_MOVE_TIME_WRITE        1u
#define SERIAL_SERVO_H12H_CMD_MOVE_TIME_READ         2u
#define SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_WRITE   7u
#define SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_READ    8u
#define SERIAL_SERVO_H12H_CMD_MOVE_START             11u
#define SERIAL_SERVO_H12H_CMD_MOVE_STOP              12u
#define SERIAL_SERVO_H12H_CMD_ID_WRITE               13u
#define SERIAL_SERVO_H12H_CMD_ID_READ                14u
#define SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_ADJUST    17u
#define SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_WRITE     18u
#define SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_READ      19u
#define SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_WRITE      20u
#define SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_READ       21u
#define SERIAL_SERVO_H12H_CMD_VIN_LIMIT_WRITE        22u
#define SERIAL_SERVO_H12H_CMD_VIN_LIMIT_READ         23u
#define SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_WRITE   24u
#define SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_READ    25u
#define SERIAL_SERVO_H12H_CMD_TEMP_READ              26u
#define SERIAL_SERVO_H12H_CMD_VIN_READ               27u
#define SERIAL_SERVO_H12H_CMD_POS_READ               28u
#define SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE    29u
#define SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_READ     30u
#define SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_WRITE   31u
#define SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_READ    32u
#define SERIAL_SERVO_H12H_CMD_LED_CTRL_WRITE         33u
#define SERIAL_SERVO_H12H_CMD_LED_CTRL_READ          34u
#define SERIAL_SERVO_H12H_CMD_LED_ERROR_WRITE        35u
#define SERIAL_SERVO_H12H_CMD_LED_ERROR_READ         36u
#define SERIAL_SERVO_H12H_CMD_DIS_READ               48u

typedef enum {
  SERIAL_SERVO_H12H_MODE_POSITION = 0,
  SERIAL_SERVO_H12H_MODE_MOTOR = 1
} serial_servo_h12h_mode_t;

/* One protocol bus instance serves all servo IDs. Initialize before SerialStart.
 * Uses a board-validated full-duplex adapter or fixed RS485 path; does not
 * change a GPIO into a speculative one-wire electrical interface. */
typedef struct { SRNOS_SerialChannel *channel; uint32_t timeout_ms; } serial_servo_h12h_t;

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
  serial_servo_h12h_mode_t mode;
  int16_t motor_speed;
  uint16_t move_time_pos;
  uint16_t move_time_ms;
  uint16_t wait_time_pos;
  uint16_t wait_time_ms;
  uint8_t led_ctrl;
  uint8_t led_error;
  uint16_t distance;
} serial_servo_h12h_info_t;

int serial_servo_h12h_init(serial_servo_h12h_t* self, SRNOS_SerialChannel* channel, uint32_t timeout_ms);

int serial_servo_h12h_read_id(serial_servo_h12h_t* self, uint8_t query_id, uint8_t* out_id);
int serial_servo_h12h_write_id(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t new_id);
int serial_servo_h12h_set_position(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms);
int serial_servo_h12h_move_time_read(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms);
int serial_servo_h12h_move_time_wait_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms);
int serial_servo_h12h_move_time_wait_read(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms);
int serial_servo_h12h_move_start(serial_servo_h12h_t* self, uint8_t servo_id);
int serial_servo_h12h_move_stop(serial_servo_h12h_t* self, uint8_t servo_id);
int serial_servo_h12h_read_position(serial_servo_h12h_t* self, uint8_t servo_id, int16_t* position);
int serial_servo_h12h_angle_offset_adjust(serial_servo_h12h_t* self, uint8_t servo_id, int8_t offset);
int serial_servo_h12h_angle_offset_write(serial_servo_h12h_t* self, uint8_t servo_id);
int serial_servo_h12h_read_deviation(serial_servo_h12h_t* self, uint8_t servo_id, int8_t* deviation);
int serial_servo_h12h_angle_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t min, uint16_t max);
int serial_servo_h12h_read_angle_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* min, uint16_t* max);
int serial_servo_h12h_vin_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t min_mv, uint16_t max_mv);
int serial_servo_h12h_read_vin_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* min_mv, uint16_t* max_mv);
int serial_servo_h12h_read_vin(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* vin_mv);
int serial_servo_h12h_temp_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t temp_limit);
int serial_servo_h12h_read_temp_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* temp_limit);
int serial_servo_h12h_read_temp(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* temp_c);
int serial_servo_h12h_load_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t load_unload);
int serial_servo_h12h_read_load(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* load_unload);
int serial_servo_h12h_led_ctrl_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t led_ctrl);
int serial_servo_h12h_led_ctrl_read(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* led_ctrl);
int serial_servo_h12h_led_error_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t led_error);
int serial_servo_h12h_led_error_read(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* led_error);
int serial_servo_h12h_read_distance(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* distance);

int serial_servo_h12h_set_mode_position(serial_servo_h12h_t* self, uint8_t servo_id);
int serial_servo_h12h_set_mode_motor(serial_servo_h12h_t* self, uint8_t servo_id, int16_t speed);
int serial_servo_h12h_read_mode(serial_servo_h12h_t* self, uint8_t servo_id, serial_servo_h12h_mode_t* mode, int16_t* speed);

int serial_servo_h12h_read_all_info(serial_servo_h12h_t* self, uint8_t servo_id, serial_servo_h12h_info_t* info);
float serial_servo_h12h_position_to_deg(int16_t position);

#ifdef __cplusplus
}
#endif

#endif



