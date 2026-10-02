#include "SirinEmbedOS/Devices/H12H/H12H.h"
#include <stdbool.h>
#include <string.h>
static inline uint16_t SRNOS_SerialServoH12hArgToU16(const uint8_t* p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void SRNOS_SerialServoH12hU16ToArg(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

static uint8_t SRNOS_SerialServoH12hChecksum(const uint8_t* frame)
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
  uint8_t n=data[i+3];if(n<3||n>11){m->last_error=SRNOS_SERIAL_SERVO_H12H_ERR_LEN;continue;}
  if(i+n+3>size)continue;
  if(data[i+4]!=m->cmd){m->last_error=SRNOS_SERIAL_SERVO_H12H_ERR_CMD;continue;}
  if(m->id!=0xff&&data[i+2]!=m->id){m->last_error=SRNOS_SERIAL_SERVO_H12H_ERR_ID;continue;}
  if(SRNOS_SerialServoH12hChecksum(data+i)!=data[i+n+2]){m->last_error=SRNOS_SERIAL_SERVO_H12H_ERR_CHECKSUM;continue;}
  m->offset=i;return (int)(i+n+3);
 }
 return 0;
}
static int SRNOS_SerialServoH12hTransceive(SRNOS_SerialServoH12h *self,
 uint8_t tx_id,uint8_t tx_cmd,const uint8_t *args,uint8_t arg_len,
 bool need_resp,uint8_t expected_rx_id,uint8_t *rx_id,uint8_t *rx_args,uint8_t *rx_arg_len) {
 if(!self||!self->channel||arg_len>10||(arg_len&&!args))return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
 uint8_t tx[16]={0x55,0x55,tx_id,(uint8_t)(arg_len+3),tx_cmd};
 if(arg_len)memcpy(tx+5,args,arg_len);tx[arg_len+5]=SRNOS_SerialServoH12hChecksum(tx);
 uint8_t rx[64];size_t n=0;h12h_match_t match={expected_rx_id,tx_cmd,0,SRNOS_SERIAL_SERVO_H12H_ERR_RX_TIMEOUT};
 SRNOS_SerialTransaction t={.tx_data=tx,.tx_size=arg_len+6u,
 .rx_data=rx,.rx_capacity=need_resp?sizeof(rx):0,
 .queue_timeout_ms=self->timeout_ms,.response_timeout_ms=self->timeout_ms,
 .silence_us=SRNOS_SERIAL_SERVO_H12H_INTER_FRAME_GAP_MS*1000u,.match=match_frame,.match_context=&match};
 SRNOS_SerialStatus ret=SRNOS_SerialTransceive(self->channel,&t,&n);
 if(ret==SRNOS_SERIAL_TIMEOUT)return match.last_error;
 if(ret!=SRNOS_SERIAL_OK)return SRNOS_SERIAL_SERVO_H12H_ERR_TX;
 if(need_resp){const uint8_t *f=rx+match.offset;uint8_t count=f[3]-3;
  if(rx_id)*rx_id=f[2];if(rx_arg_len)*rx_arg_len=count;if(rx_args&&count)memcpy(rx_args,f+5,count);}
 return SRNOS_SERIAL_SERVO_H12H_OK;
}
int SRNOS_SerialServoH12hInit(SRNOS_SerialServoH12h *self,SRNOS_SerialChannel *channel,uint32_t timeout_ms) {
 const SRNOS_SerialConfig *cfg=SRNOS_SerialGetConfig(channel);
 if(!self||!cfg||!cfg->rx_enabled||!cfg->tx_enabled||!timeout_ms)return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
 if(SRNOS_SerialClaim(channel,self)!=SRNOS_SERIAL_OK)return SRNOS_SERIAL_SERVO_H12H_ERR_LOCK;
 self->channel=channel;self->timeout_ms=timeout_ms;return SRNOS_SERIAL_SERVO_H12H_OK;
}
int SRNOS_SerialServoH12hReadId(SRNOS_SerialServoH12h* self, uint8_t query_id, uint8_t* out_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_id = 0;
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  uint8_t expect_id = (query_id == SRNOS_SERIAL_SERVO_H12H_BROADCAST_ID) ? 0xFFu : query_id;
  int ret = SRNOS_SerialServoH12hTransceive(self, query_id, SRNOS_SERIAL_SERVO_H12H_CMD_ID_READ, NULL, 0u, true, expect_id, &rx_id, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  if (out_id != NULL) {
    *out_id = rx_args[0];
  }
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hWriteId(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t new_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = new_id;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ID_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hSetPosition(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  if (position > SRNOS_SERIAL_SERVO_H12H_POS_MAX) {
    position = SRNOS_SERIAL_SERVO_H12H_POS_MAX;
  }
  SRNOS_SerialServoH12hU16ToArg(&args[0], position);
  SRNOS_SerialServoH12hU16ToArg(&args[2], duration_ms);
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hMoveTimeRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms)
{
  if (self == NULL || position == NULL || duration_ms == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = SRNOS_SerialServoH12hArgToU16(&rx_args[0]);
  *duration_ms = SRNOS_SerialServoH12hArgToU16(&rx_args[2]);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hMoveTimeWaitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t position, uint16_t duration_ms)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  if (position > SRNOS_SERIAL_SERVO_H12H_POS_MAX) {
    position = SRNOS_SERIAL_SERVO_H12H_POS_MAX;
  }
  SRNOS_SerialServoH12hU16ToArg(&args[0], position);
  SRNOS_SerialServoH12hU16ToArg(&args[2], duration_ms);
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hMoveTimeWaitRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* position, uint16_t* duration_ms)
{
  if (self == NULL || position == NULL || duration_ms == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_TIME_WAIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = SRNOS_SerialServoH12hArgToU16(&rx_args[0]);
  *duration_ms = SRNOS_SerialServoH12hArgToU16(&rx_args[2]);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hMoveStart(SRNOS_SerialServoH12h* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_START, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hMoveStop(SRNOS_SerialServoH12h* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_MOVE_STOP, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadPosition(SRNOS_SerialServoH12h* self, uint8_t servo_id, int16_t* position)
{
  if (self == NULL || position == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_POS_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *position = (int16_t)SRNOS_SerialServoH12hArgToU16(rx_args);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hReadDeviation(SRNOS_SerialServoH12h* self, uint8_t servo_id, int8_t* deviation)
{
  if (self == NULL || deviation == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *deviation = (int8_t)rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hAngleOffsetAdjust(SRNOS_SerialServoH12h* self, uint8_t servo_id, int8_t offset)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = (uint8_t)offset;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_ADJUST, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hAngleOffsetWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_OFFSET_WRITE, NULL, 0u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hAngleLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t min, uint16_t max)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  SRNOS_SerialServoH12hU16ToArg(&args[0], min);
  SRNOS_SerialServoH12hU16ToArg(&args[2], max);
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadAngleLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* min, uint16_t* max)
{
  if (self == NULL || min == NULL || max == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_ANGLE_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *min = SRNOS_SerialServoH12hArgToU16(&rx_args[0]);
  *max = SRNOS_SerialServoH12hArgToU16(&rx_args[2]);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hVinLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t min_mv, uint16_t max_mv)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  SRNOS_SerialServoH12hU16ToArg(&args[0], min_mv);
  SRNOS_SerialServoH12hU16ToArg(&args[2], max_mv);
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_VIN_LIMIT_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadVinLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* min_mv, uint16_t* max_mv)
{
  if (self == NULL || min_mv == NULL || max_mv == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_VIN_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *min_mv = SRNOS_SerialServoH12hArgToU16(&rx_args[0]);
  *max_mv = SRNOS_SerialServoH12hArgToU16(&rx_args[2]);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hReadVin(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* vin_mv)
{
  if (self == NULL || vin_mv == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_VIN_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *vin_mv = SRNOS_SerialServoH12hArgToU16(rx_args);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hTempLimitWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t temp_limit)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = temp_limit;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadTempLimit(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* temp_limit)
{
  if (self == NULL || temp_limit == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_MAX_LIMIT_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *temp_limit = rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hReadTemp(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* temp_c)
{
  if (self == NULL || temp_c == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_TEMP_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *temp_c = rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hLoadWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t load_unload)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = load_unload;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadLoad(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* load_unload)
{
  if (self == NULL || load_unload == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LOAD_OR_UNLOAD_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *load_unload = rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hLedCtrlWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t led_ctrl)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = led_ctrl;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LED_CTRL_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hLedCtrlRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* led_ctrl)
{
  if (self == NULL || led_ctrl == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LED_CTRL_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *led_ctrl = rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hLedErrorWrite(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t led_error)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[1];
  args[0] = led_error;
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LED_ERROR_WRITE, args, 1u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hLedErrorRead(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint8_t* led_error)
{
  if (self == NULL || led_error == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_LED_ERROR_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 1u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *led_error = rx_args[0];
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hReadDistance(SRNOS_SerialServoH12h* self, uint8_t servo_id, uint16_t* distance)
{
  if (self == NULL || distance == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_DIS_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 2u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *distance = SRNOS_SerialServoH12hArgToU16(rx_args);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hSetModePosition(SRNOS_SerialServoH12h* self, uint8_t servo_id)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4] = {0, 0, 0, 0};
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hSetModeMotor(SRNOS_SerialServoH12h* self, uint8_t servo_id, int16_t speed)
{
  if (self == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t args[4];
  args[0] = 1u;
  args[1] = 0u;
  SRNOS_SerialServoH12hU16ToArg(&args[2], (uint16_t)speed);
  return SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_WRITE, args, 4u, false, 0u, NULL, NULL, NULL);
}

int SRNOS_SerialServoH12hReadMode(SRNOS_SerialServoH12h* self, uint8_t servo_id, SRNOS_SerialServoH12hMode* mode, int16_t* speed)
{
  if (self == NULL || mode == NULL || speed == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  uint8_t rx_args[8] = {0};
  uint8_t rx_len = 0;
  int ret = SRNOS_SerialServoH12hTransceive(self, servo_id, SRNOS_SERIAL_SERVO_H12H_CMD_OR_MOTOR_MODE_READ, NULL, 0u, true, servo_id, NULL, rx_args, &rx_len);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) {
    return ret;
  }
  if (rx_len < 4u) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_LEN;
  }
  *mode = (rx_args[0] == 0u) ? SRNOS_SERIAL_SERVO_H12H_MODE_POSITION : SRNOS_SERIAL_SERVO_H12H_MODE_MOTOR;
  *speed = (int16_t)SRNOS_SerialServoH12hArgToU16(&rx_args[2]);
  return SRNOS_SERIAL_SERVO_H12H_OK;
}

int SRNOS_SerialServoH12hReadAllInfo(SRNOS_SerialServoH12h* self, uint8_t servo_id, SRNOS_SerialServoH12hInfo* info)
{
  if (self == NULL || info == NULL) {
    return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
  }
  memset(info, 0, sizeof(*info));

  int ret = SRNOS_SERIAL_SERVO_H12H_OK;
  ret = SRNOS_SerialServoH12hReadId(self, servo_id, &info->id);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hMoveTimeRead(self, servo_id, &info->move_time_pos, &info->move_time_ms);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hMoveTimeWaitRead(self, servo_id, &info->wait_time_pos, &info->wait_time_ms);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadPosition(self, servo_id, &info->position);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadDeviation(self, servo_id, &info->deviation);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadAngleLimit(self, servo_id, &info->angle_limit_min, &info->angle_limit_max);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadVinLimit(self, servo_id, &info->vin_limit_min, &info->vin_limit_max);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadVin(self, servo_id, &info->vin_mv);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadTempLimit(self, servo_id, &info->temp_limit);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadTemp(self, servo_id, &info->temp_c);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadLoad(self, servo_id, &info->load_unload);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadMode(self, servo_id, &info->mode, &info->motor_speed);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hLedCtrlRead(self, servo_id, &info->led_ctrl);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hLedErrorRead(self, servo_id, &info->led_error);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;
  ret = SRNOS_SerialServoH12hReadDistance(self, servo_id, &info->distance);
  if (ret != SRNOS_SERIAL_SERVO_H12H_OK) return ret;

  return SRNOS_SERIAL_SERVO_H12H_OK;
}

float SRNOS_SerialServoH12hPositionToDeg(int16_t position)
{
  return ((float)position) * 240.0f / 1000.0f;
}



