#include "SirinEmbedOS/Devices/H12H/lib_serial_servo_h12h.h"
#include <stdbool.h>
#include <string.h>
static inline uint16_t serial_servo_h12h_arg_to_u16(const uint8_t* p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void serial_servo_h12h_u16_to_arg(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

static uint8_t serial_servo_h12h_checksum(const uint8_t* frame)
{
  uint16_t sum = 0;
  uint8_t i = 0;
  for (i = 2; i < (uint8_t)(frame[3] + 2u); ++i) {
    sum += frame[i];
  }
  return (uint8_t)(~sum);
}


typedef struct {uint8_t id,cmd;size_t offset;int last_error;} h12h_match_t;
static int match_frame(const uint8_t *data,size_t size,void *arg) {
 h12h_match_t *m=arg;
 for(size_t i=0;i+6<=size;i++){
  if(data[i]!=0x55||data[i+1]!=0x55)continue;
  uint8_t n=data[i+3];if(n<3||n>11){m->last_error=SERIAL_SERVO_H12H_ERR_LEN;continue;}
  if(i+n+3>size)continue;
  if(data[i+4]!=m->cmd){m->last_error=SERIAL_SERVO_H12H_ERR_CMD;continue;}
  if(m->id!=0xff&&data[i+2]!=m->id){m->last_error=SERIAL_SERVO_H12H_ERR_ID;continue;}
  if(serial_servo_h12h_checksum(data+i)!=data[i+n+2]){m->last_error=SERIAL_SERVO_H12H_ERR_CHECKSUM;continue;}
  m->offset=i;return (int)(i+n+3);
 }
 return 0;
}
static int serial_servo_h12h_transceive(serial_servo_h12h_t *self,
 uint8_t tx_id,uint8_t tx_cmd,const uint8_t *args,uint8_t arg_len,
 bool need_resp,uint8_t expected_rx_id,uint8_t *rx_id,uint8_t *rx_args,uint8_t *rx_arg_len) {
 if(!self||!self->channel||arg_len>10||(arg_len&&!args))return SERIAL_SERVO_H12H_ERR_ARG;
 uint8_t tx[16]={0x55,0x55,tx_id,(uint8_t)(arg_len+3),tx_cmd};
 if(arg_len)memcpy(tx+5,args,arg_len);tx[arg_len+5]=serial_servo_h12h_checksum(tx);
 uint8_t rx[64];size_t n=0;h12h_match_t match={expected_rx_id,tx_cmd,0,SERIAL_SERVO_H12H_ERR_RX_TIMEOUT};
 SRNOS_SerialTransaction t={.tx_data=tx,.tx_size=arg_len+6u,
 .rx_data=rx,.rx_capacity=need_resp?sizeof(rx):0,
 .queue_timeout_ms=self->timeout_ms,.response_timeout_ms=self->timeout_ms,
 .silence_us=SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS*1000u,.match=match_frame,.match_context=&match};
 SRNOS_SerialStatus ret=SRNOS_SerialTransceive(self->channel,&t,&n);
 if(ret==SRNOS_SERIAL_TIMEOUT)return match.last_error;
 if(ret!=SRNOS_SERIAL_OK)return SERIAL_SERVO_H12H_ERR_TX;
 if(need_resp){const uint8_t *f=rx+match.offset;uint8_t count=f[3]-3;
  if(rx_id)*rx_id=f[2];if(rx_arg_len)*rx_arg_len=count;if(rx_args&&count)memcpy(rx_args,f+5,count);}
 return SERIAL_SERVO_H12H_OK;
}
int serial_servo_h12h_init(serial_servo_h12h_t *self,SRNOS_SerialChannel *channel,uint32_t timeout_ms) {
 const SRNOS_SerialConfig *cfg=SRNOS_SerialGetConfig(channel);
 if(!self||!cfg||!cfg->rx_enabled||!cfg->tx_enabled||!timeout_ms)return SERIAL_SERVO_H12H_ERR_ARG;
 if(SRNOS_SerialClaim(channel,self)!=SRNOS_SERIAL_OK)return SERIAL_SERVO_H12H_ERR_LOCK;
 self->channel=channel;self->timeout_ms=timeout_ms;return SERIAL_SERVO_H12H_OK;
}
int serial_servo_h12h_read_id(serial_servo_h12h_t* self, uint8_t query_id, uint8_t* out_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_id = 0;
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  uint8_t expect_id = (query_id == SERIAL_SERVO_H12H_BROADCAST_ID) ? 0xFFu : query_id;
  int ret = serial_servo_h12h_transceive(self, query_id, SERIAL_SERVO_H12H_CMD_ID_READ, NULL, 0u, true, expect_id, &rx_id, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  if (out_id != NULL) {
    *out_id = rx_args[0];
  }
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_write_id(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t new_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = new_id;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ID_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_set_position(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  if (position > SERIAL_SERVO_H12H_POS_MAX) {
    position = SERIAL_SERVO_H12H_POS_MAX;
  }
  serial_servo_h12h_u16_to_arg(&args[0], position);
  serial_servo_h12h_u16_to_arg(&args[2], duration_ms);
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_TIME_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_move_time_read(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms)
{
  if (self == NULL || position == NULL || duration_ms == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_TIME_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = serial_servo_h12h_arg_to_u16(&rx_args[0]);
  *duration_ms = serial_servo_h12h_arg_to_u16(&rx_args[2]);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_move_time_wait_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  if (position > SERIAL_SERVO_H12H_POS_MAX) {
    position = SERIAL_SERVO_H12H_POS_MAX;
  }
  serial_servo_h12h_u16_to_arg(&args[0], position);
  serial_servo_h12h_u16_to_arg(&args[2], duration_ms);
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_move_time_wait_read(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms)
{
  if (self == NULL || position == NULL || duration_ms == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = serial_servo_h12h_arg_to_u16(&rx_args[0]);
  *duration_ms = serial_servo_h12h_arg_to_u16(&rx_args[2]);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_move_start(serial_servo_h12h_t* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_START, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_move_stop(serial_servo_h12h_t* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_MOVE_STOP, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_position(serial_servo_h12h_t* self, uint8_t servo_id, int16_t* position)
{
  if (self == NULL || position == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_POS_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = (int16_t)serial_servo_h12h_arg_to_u16(rx_args);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_read_deviation(serial_servo_h12h_t* self, uint8_t servo_id, int8_t* deviation)
{
  if (self == NULL || deviation == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *deviation = (int8_t)rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_angle_offset_adjust(serial_servo_h12h_t* self, uint8_t servo_id, int8_t offset)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = (uint8_t)offset;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_ADJUST, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_angle_offset_write(serial_servo_h12h_t* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_WRITE, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_angle_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t min, uint16_t max)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  serial_servo_h12h_u16_to_arg(&args[0], min);
  serial_servo_h12h_u16_to_arg(&args[2], max);
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_angle_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* min, uint16_t* max)
{
  if (self == NULL || min == NULL || max == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *min = serial_servo_h12h_arg_to_u16(&rx_args[0]);
  *max = serial_servo_h12h_arg_to_u16(&rx_args[2]);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_vin_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t min_mv, uint16_t max_mv)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  serial_servo_h12h_u16_to_arg(&args[0], min_mv);
  serial_servo_h12h_u16_to_arg(&args[2], max_mv);
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_VIN_LIMIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_vin_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* min_mv, uint16_t* max_mv)
{
  if (self == NULL || min_mv == NULL || max_mv == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_VIN_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *min_mv = serial_servo_h12h_arg_to_u16(&rx_args[0]);
  *max_mv = serial_servo_h12h_arg_to_u16(&rx_args[2]);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_read_vin(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* vin_mv)
{
  if (self == NULL || vin_mv == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_VIN_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *vin_mv = serial_servo_h12h_arg_to_u16(rx_args);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_temp_limit_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t temp_limit)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = temp_limit;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_temp_limit(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* temp_limit)
{
  if (self == NULL || temp_limit == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *temp_limit = rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_read_temp(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* temp_c)
{
  if (self == NULL || temp_c == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_TEMP_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *temp_c = rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_load_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t load_unload)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = load_unload;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_load(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* load_unload)
{
  if (self == NULL || load_unload == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *load_unload = rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_led_ctrl_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t led_ctrl)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = led_ctrl;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LED_CTRL_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_led_ctrl_read(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* led_ctrl)
{
  if (self == NULL || led_ctrl == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LED_CTRL_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *led_ctrl = rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_led_error_write(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t led_error)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = led_error;
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LED_ERROR_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_led_error_read(serial_servo_h12h_t* self, uint8_t servo_id, uint8_t* led_error)
{
  if (self == NULL || led_error == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_LED_ERROR_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *led_error = rx_args[0];
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_read_distance(serial_servo_h12h_t* self, uint8_t servo_id, uint16_t* distance)
{
  if (self == NULL || distance == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_DIS_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *distance = serial_servo_h12h_arg_to_u16(rx_args);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_set_mode_position(serial_servo_h12h_t* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4] = {0, 0, 0, 0};
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_set_mode_motor(serial_servo_h12h_t* self, uint8_t servo_id, int16_t speed)
{
  if (self == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  args[0] = 1u;
  args[1] = 0u;
  serial_servo_h12h_u16_to_arg(&args[2], (uint16_t)speed);
  return serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int serial_servo_h12h_read_mode(serial_servo_h12h_t* self, uint8_t servo_id, serial_servo_h12h_mode_t* mode, int16_t* speed)
{
  if (self == NULL || mode == NULL || speed == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = serial_servo_h12h_transceive(self, servo_id, SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SERIAL_SERVO_H12H_ERR_LEN;
  }
  *mode = (rx_args[0] == 0u) ? SERIAL_SERVO_H12H_MODE_POSITION : SERIAL_SERVO_H12H_MODE_MOTOR;
  *speed = (int16_t)serial_servo_h12h_arg_to_u16(&rx_args[2]);
  return SERIAL_SERVO_H12H_OK;
}

int serial_servo_h12h_read_all_info(serial_servo_h12h_t* self, uint8_t servo_id, serial_servo_h12h_info_t* info)
{
  if (self == NULL || info == NULL) {
    return SERIAL_SERVO_H12H_ERR_ARG;
  }
  memset(info, 0, sizeof(*info));

  int ret = SERIAL_SERVO_H12H_OK;
  ret = serial_servo_h12h_read_id(self, servo_id, &info->id);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_move_time_read(self, servo_id, &info->move_time_pos, &info->move_time_ms);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_move_time_wait_read(self, servo_id, &info->wait_time_pos, &info->wait_time_ms);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_position(self, servo_id, &info->position);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_deviation(self, servo_id, &info->deviation);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_angle_limit(self, servo_id, &info->angle_limit_min, &info->angle_limit_max);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_vin_limit(self, servo_id, &info->vin_limit_min, &info->vin_limit_max);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_vin(self, servo_id, &info->vin_mv);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_temp_limit(self, servo_id, &info->temp_limit);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_temp(self, servo_id, &info->temp_c);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_load(self, servo_id, &info->load_unload);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_mode(self, servo_id, &info->mode, &info->motor_speed);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_led_ctrl_read(self, servo_id, &info->led_ctrl);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_led_error_read(self, servo_id, &info->led_error);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;
  ret = serial_servo_h12h_read_distance(self, servo_id, &info->distance);
  if (ret != SERIAL_SERVO_H12H_OK) return ret;

  return SERIAL_SERVO_H12H_OK;
}

float serial_servo_h12h_position_to_deg(int16_t position)
{
  return ((float)position) * 240.0f / 1000.0f;
}



