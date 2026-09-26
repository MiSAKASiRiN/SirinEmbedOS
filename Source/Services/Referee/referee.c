#include "SirinEmbedOS/Services/Referee/referee.h"
#include "SirinEmbedOS/Core/C/Array.h"

#include <string.h>
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Services/Referee/Transmitter/tskptt_referee_tx.h"
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
static uint32_t referee_now_ms(void) {return (uint32_t)((uint64_t)osKernelGetTickCount()*1000u/osKernelGetTickFreq());}

#include "SirinEmbedOS/Core/Memory/mem_sections.h"

#ifndef REFEREE_TIMEOUT_DEFAULT_MS
#define REFEREE_TIMEOUT_DEFAULT_MS 1000u
#endif
#ifndef REFEREE_TIMEOUT_GAME_STATUS_MS
#define REFEREE_TIMEOUT_GAME_STATUS_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_GAME_RESULT_MS
#define REFEREE_TIMEOUT_GAME_RESULT_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_GAME_ROBOT_HP_MS
#define REFEREE_TIMEOUT_GAME_ROBOT_HP_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_EVENT_DATA_MS
#define REFEREE_TIMEOUT_EVENT_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_REFEREE_WARNING_MS
#define REFEREE_TIMEOUT_REFEREE_WARNING_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_DART_INFO_MS
#define REFEREE_TIMEOUT_DART_INFO_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_ROBOT_STATE_MS
#define REFEREE_TIMEOUT_ROBOT_STATE_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_POWER_HEAT_MS
#define REFEREE_TIMEOUT_POWER_HEAT_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_ROBOT_POS_MS
#define REFEREE_TIMEOUT_ROBOT_POS_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_BUFF_MS
#define REFEREE_TIMEOUT_BUFF_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_ROBOT_HURT_MS
#define REFEREE_TIMEOUT_ROBOT_HURT_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_SHOOT_DATA_MS
#define REFEREE_TIMEOUT_SHOOT_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_BULLET_REMAINING_MS
#define REFEREE_TIMEOUT_BULLET_REMAINING_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_RFID_STATUS_MS
#define REFEREE_TIMEOUT_RFID_STATUS_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_DART_CLIENT_CMD_MS
#define REFEREE_TIMEOUT_DART_CLIENT_CMD_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS
#define REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_RADAR_MARK_DATA_MS
#define REFEREE_TIMEOUT_RADAR_MARK_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_SENTRY_INFO_MS
#define REFEREE_TIMEOUT_SENTRY_INFO_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_RADAR_INFO_MS
#define REFEREE_TIMEOUT_RADAR_INFO_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS
#define REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_MAP_COMMAND_MS
#define REFEREE_TIMEOUT_MAP_COMMAND_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_RADAR_MAP_DATA_MS
#define REFEREE_TIMEOUT_RADAR_MAP_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_PATH_DATA_MS
#define REFEREE_TIMEOUT_PATH_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS
#define REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS REFEREE_TIMEOUT_DEFAULT_MS
#endif

// ================================ Section A: protocol constants & packed structs ================================
// Packed structs and cmd_id definitions are in referee.h for public access.

enum {
  REFEREE_HEADER_LEN = 5,
  REFEREE_CMDID_LEN = 2,
  REFEREE_CRC16_LEN = 2,
  REFEREE_FRAME_BUF_SIZE = 256,
  REFEREE_RING_SIZE = 8192,
  REFEREE_DMA_BUF_SIZE = 256,
  REFEREE_TX_BUF_SIZE = 256,
  REFEREE_MAX_DATA_LEN = REFEREE_FRAME_BUF_SIZE - REFEREE_HEADER_LEN - REFEREE_CMDID_LEN - REFEREE_CRC16_LEN
};

#if (REFEREE_EVENT_RING_SIZE == 0u)
  #error "REFEREE_EVENT_RING_SIZE must be > 0"
#endif
#if ((REFEREE_EVENT_RING_SIZE & (REFEREE_EVENT_RING_SIZE - 1u)) != 0u)
  #error "REFEREE_EVENT_RING_SIZE must be power-of-two"
#endif

// ================================ Section B: CRC8 / CRC16 ================================
static uint8_t referee_crc8_calc(const uint8_t *data, uint16_t len)
{
  // CRC-8 (poly 0x31 reflected => 0x8C), init 0xFF, LSB-first
  uint8_t crc = 0xFFu;
  for (uint16_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x01u) ? (uint8_t)((crc >> 1) ^ 0x8Cu) : (uint8_t)(crc >> 1);
    }
  }
  return crc;
}
static uint16_t referee_crc16_calc(const uint8_t *data, uint16_t len)
{
  // CRC-16/CCITT reflected: poly 0x1021 reflected => 0x8408, init 0xFFFF, LSB-first
  uint16_t crc = 0xFFFFu;
  for (uint16_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i]; // XOR into low byte for reflected algorithm
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x0001u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

static bool referee_crc8_verify_header(const uint8_t *hdr5)
{
  return referee_crc8_calc(hdr5, 4) == hdr5[4];
}

static bool referee_crc16_verify_frame(const uint8_t *frame, uint16_t total_len)
{
  if (total_len < (REFEREE_HEADER_LEN + REFEREE_CMDID_LEN + REFEREE_CRC16_LEN)) {
    return false;
  }
  const uint16_t crc_calc = referee_crc16_calc(frame, (uint16_t)(total_len - REFEREE_CRC16_LEN));
  const uint16_t crc_rx = (uint16_t)(frame[total_len - 2] | (frame[total_len - 1] << 8));
  return crc_calc == crc_rx;
}

// ================================ Section C: Ring Buffer (ISR write / Task read) ================================

static referee_snapshot_t g_snapshot;
static volatile uint32_t g_seq = 0;
static volatile uint32_t g_last_game_status_ms = 0;
static volatile uint32_t g_last_game_result_ms = 0;
static volatile uint32_t g_last_game_robot_hp_ms = 0;
static volatile uint32_t g_last_event_data_ms = 0;
static volatile uint32_t g_last_referee_warning_ms = 0;
static volatile uint32_t g_last_dart_info_ms = 0;
static volatile uint32_t g_last_robot_state_ms = 0;
static volatile uint32_t g_last_power_heat_ms = 0;
static volatile uint32_t g_last_robot_pos_ms = 0;
static volatile uint32_t g_last_buff_ms = 0;
static volatile uint32_t g_last_robot_hurt_ms = 0;
static volatile uint32_t g_last_shoot_data_ms = 0;
static volatile uint32_t g_last_bullet_remaining_ms = 0;
static volatile uint32_t g_last_rfid_status_ms = 0;
static volatile uint32_t g_last_dart_client_cmd_ms = 0;
static volatile uint32_t g_last_ground_robot_pos_ms = 0;
static volatile uint32_t g_last_radar_mark_ms = 0;
static volatile uint32_t g_last_sentry_info_ms = 0;
static volatile uint32_t g_last_radar_info_ms = 0;
static volatile uint32_t g_last_robot_interactive_ms = 0;
static volatile uint32_t g_last_map_command_ms = 0;
static volatile uint32_t g_last_radar_map_data_ms = 0;
static volatile uint32_t g_last_path_data_ms = 0;
static volatile uint32_t g_last_robot_map_data_ms = 0;

typedef struct {
  volatile uint32_t stamp; // even: stable(seq<<1), odd: writer in progress
  referee_event_t event;
} referee_event_slot_t;

static referee_event_slot_t g_event_ring[REFEREE_EVENT_RING_SIZE];
static volatile uint32_t g_event_write_seq = 0;

static void referee_event_push_robot_hurt(const uint8_t *hurt_payload, uint32_t rx_tick_ms)
{
  if (hurt_payload == NULL) {
    return;
  }

  const uint32_t seq = __atomic_add_fetch(&g_event_write_seq, 1u, __ATOMIC_ACQ_REL);
  referee_event_slot_t *slot = &g_event_ring[(seq - 1u) & (REFEREE_EVENT_RING_SIZE - 1u)];

  __atomic_store_n(&slot->stamp, (seq << 1u) | 1u, __ATOMIC_RELEASE);
  slot->event.seq = seq;
  slot->event.rx_tick_ms = rx_tick_ms;
  slot->event.cmd_id = (uint16_t)REFEREE_EVENT_TYPE_ROBOT_HURT;
  slot->event.reserved = 0u;
  memcpy(&slot->event.data.robot_hurt, hurt_payload, sizeof(slot->event.data.robot_hurt));
  __atomic_store_n(&slot->stamp, (seq << 1u), __ATOMIC_RELEASE);
}

static void referee_event_push_shoot_data(const uint8_t *shoot_payload, uint32_t rx_tick_ms)
{
  if (shoot_payload == NULL) {
    return;
  }

  const uint32_t seq = __atomic_add_fetch(&g_event_write_seq, 1u, __ATOMIC_ACQ_REL);
  referee_event_slot_t *slot = &g_event_ring[(seq - 1u) & (REFEREE_EVENT_RING_SIZE - 1u)];

  __atomic_store_n(&slot->stamp, (seq << 1u) | 1u, __ATOMIC_RELEASE);
  slot->event.seq = seq;
  slot->event.rx_tick_ms = rx_tick_ms;
  slot->event.cmd_id = (uint16_t)REFEREE_EVENT_TYPE_SHOOT_DATA;
  slot->event.reserved = 0u;
  memcpy(&slot->event.data.shoot_data, shoot_payload, sizeof(slot->event.data.shoot_data));
  __atomic_store_n(&slot->stamp, (seq << 1u), __ATOMIC_RELEASE);
}

// ================================ Section D: caller-bound UART DMA + IDLE receive ================================
static SRNOS_SerialChannel *g_referee_channel = NULL;
static bool g_referee_initialized = false;

// Reserved for future UART TX DMA payload staging.

static volatile uint16_t g_tx_len = 0;
static volatile uint8_t g_tx_busy = 0;
static volatile uint32_t g_last_rx_ms = 0;

volatile referee_snapshot_t g_referee_snapshot_raw_dbg;
volatile uint32_t g_referee_raw_last_rx_ms_dbg = 0;
volatile uint16_t g_referee_last_cmd_id_dbg = 0;
volatile uint16_t g_referee_last_payload_len_dbg = 0;
volatile uint32_t g_referee_last_crc_ok_ms_dbg = 0;
volatile uint32_t g_referee_crc_ok_cnt_dbg = 0;
volatile uint32_t g_referee_crc_fail_cnt_dbg = 0;
volatile uint32_t g_referee_last_robot_state_ms_dbg = 0;

static void referee_parser_reset(void);
bool Referee_BindChannel(SRNOS_SerialChannel *channel) {
 const SRNOS_SerialConfig *cfg=SRNOS_SerialGetConfig(channel);
 if(!cfg||!cfg->rx_enabled||g_referee_initialized||(g_referee_channel&&g_referee_channel!=channel))return false;
 if(SRNOS_SerialClaim(channel,&g_referee_channel)!=SRNOS_SERIAL_OK)return false;
 g_referee_channel=channel;return RefereeTX_BindChannel(channel);
}
SRNOS_SerialChannel *Referee_GetBoundChannel(void){return g_referee_channel;}

bool Referee_Init(void)
{
  if (g_referee_channel == NULL || g_referee_initialized) {
    return false;
  }
  g_last_rx_ms = 0;
  g_tx_len = 0;
  g_tx_busy = 0;
  g_last_game_status_ms = 0;
  g_last_game_result_ms = 0;
  g_last_game_robot_hp_ms = 0;
  g_last_event_data_ms = 0;
  g_last_referee_warning_ms = 0;
  g_last_dart_info_ms = 0;
  g_last_robot_state_ms = 0;
  g_last_power_heat_ms = 0;
  g_last_robot_pos_ms = 0;
  g_last_buff_ms = 0;
  g_last_robot_hurt_ms = 0;
  g_last_shoot_data_ms = 0;
  g_last_bullet_remaining_ms = 0;
  g_last_rfid_status_ms = 0;
  g_last_dart_client_cmd_ms = 0;
  g_last_ground_robot_pos_ms = 0;
  g_last_radar_mark_ms = 0;
  g_last_sentry_info_ms = 0;
  g_last_radar_info_ms = 0;
  g_last_robot_interactive_ms = 0;
  g_last_map_command_ms = 0;
  g_last_radar_map_data_ms = 0;
  g_last_path_data_ms = 0;
  g_last_robot_map_data_ms = 0;
  g_event_write_seq = 0;
  memset(g_event_ring, 0, sizeof(g_event_ring));
  memset(&g_snapshot, 0, sizeof(g_snapshot));
  g_seq = 0;
  referee_parser_reset();
  g_referee_initialized = true;
  return true;
}

bool Referee_Start(void) { return g_referee_initialized; }

// ================================ Section E: Protocol parse state machine ================================
typedef enum {
  REF_PARSE_WAIT_SOF = 0,
  REF_PARSE_READ_HEADER,
  REF_PARSE_READ_CMDID,
  REF_PARSE_READ_DATA,
  REF_PARSE_READ_CRC16
} referee_parse_state_t;

static referee_parse_state_t g_parse_state = REF_PARSE_WAIT_SOF;
static uint16_t g_expected_data_len = 0;
static uint16_t g_frame_idx = 0;
static uint16_t g_cmd_id = 0;
static uint8_t g_frame_buf[REFEREE_FRAME_BUF_SIZE];

static void referee_parser_reset(void)
{
  g_parse_state = REF_PARSE_WAIT_SOF;
  g_expected_data_len = 0;
  g_frame_idx = 0;
  g_cmd_id = 0;
}

static void referee_dispatch(uint16_t cmd_id, const uint8_t *payload, uint16_t len);

static void referee_parse_byte(uint8_t b)
{
  switch (g_parse_state) {
    case REF_PARSE_WAIT_SOF:
      if (b == REFEREE_SOF) {
        g_frame_idx = 0;
        g_frame_buf[g_frame_idx++] = b;
        g_parse_state = REF_PARSE_READ_HEADER;
      }
      break;

    case REF_PARSE_READ_HEADER:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == REFEREE_HEADER_LEN) {
        if (!referee_crc8_verify_header(g_frame_buf)) {
          referee_parser_reset();
          break;
        }
        g_expected_data_len = (uint16_t)(g_frame_buf[1] | (g_frame_buf[2] << 8));
        if (g_expected_data_len > REFEREE_MAX_DATA_LEN) {
          referee_parser_reset();
          break;
        }
        g_parse_state = REF_PARSE_READ_CMDID;
      }
      break;

    case REF_PARSE_READ_CMDID:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (REFEREE_HEADER_LEN + REFEREE_CMDID_LEN)) {
        g_cmd_id = (uint16_t)(g_frame_buf[5] | (g_frame_buf[6] << 8));
        if (g_expected_data_len == 0) {
          g_parse_state = REF_PARSE_READ_CRC16;
        } else {
          g_parse_state = REF_PARSE_READ_DATA;
        }
      }
      break;

    case REF_PARSE_READ_DATA:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (REFEREE_HEADER_LEN + REFEREE_CMDID_LEN + g_expected_data_len)) {
        g_parse_state = REF_PARSE_READ_CRC16;
      }
      break;

    case REF_PARSE_READ_CRC16:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (REFEREE_HEADER_LEN + REFEREE_CMDID_LEN + g_expected_data_len + REFEREE_CRC16_LEN)) {
        if (referee_crc16_verify_frame(g_frame_buf, g_frame_idx)) {
          g_referee_crc_ok_cnt_dbg++;
          g_referee_last_cmd_id_dbg = g_cmd_id;
          g_referee_last_payload_len_dbg = g_expected_data_len;
          g_referee_last_crc_ok_ms_dbg = g_last_rx_ms;
          const uint8_t *payload = &g_frame_buf[REFEREE_HEADER_LEN + REFEREE_CMDID_LEN];
          referee_dispatch(g_cmd_id, payload, g_expected_data_len);
        } else {
          g_referee_crc_fail_cnt_dbg++;
        }
        referee_parser_reset();
      }
      break;

    default:
      referee_parser_reset();
      break;
  }
}

static void referee_parse_bytes(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; ++i) {
    referee_parse_byte(data[i]);
  }
}

void Referee_Feed(const uint8_t *data,size_t size,uint32_t timestamp_ms) {
 if(!g_referee_initialized||!data)return;
 g_last_rx_ms=timestamp_ms;g_referee_raw_last_rx_ms_dbg=timestamp_ms;
 while(size){uint16_t n=(uint16_t)(size>UINT16_MAX?UINT16_MAX:size);referee_parse_bytes(data,n);data+=n;size-=n;}
}

void Referee_Poll(void) {
 uint8_t tmp[128];size_t got;
 for(unsigned budget=0;budget<8;budget++) {
  if(SRNOS_SerialRead(g_referee_channel,tmp,sizeof(tmp),&got,0)!=SRNOS_SERIAL_OK)break;
  Referee_Feed(tmp,got,referee_now_ms());
 }
}

// ================================ Section F: cmd_id dispatch + data storage + seqlock ================================
static void referee_write_begin(void)
{
  taskENTER_CRITICAL();
  g_seq++;
  __DMB();
}

static void referee_write_end(void)
{
  __DMB();
  memcpy((void *)&g_referee_snapshot_raw_dbg, &g_snapshot, sizeof(g_snapshot));
  g_seq++;
  taskEXIT_CRITICAL();
}

static bool referee_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms)
{
  if (last_ms == 0u) {
    return false;
  }
  return (uint32_t)(now_ms - last_ms) <= timeout_ms;
}

static void referee_store_game_status(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_game_status_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.game_status, data, sizeof(referee_game_status_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_status_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_game_result(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_game_result_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.game_result, data, sizeof(referee_game_result_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_result_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_game_robot_hp(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_game_robot_hp_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.game_robot_hp, data, sizeof(referee_game_robot_hp_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_robot_hp_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_event_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_event_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.event_data, data, sizeof(referee_event_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_event_data_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_warning(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_referee_warning_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.referee_warning, data, sizeof(referee_referee_warning_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_referee_warning_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_dart_info(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_dart_info_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.dart_info, data, sizeof(referee_dart_info_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_dart_info_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_robot_state(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_robot_state_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.robot_state, data, sizeof(referee_robot_state_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_state_ms = g_last_rx_ms;
  g_referee_last_robot_state_ms_dbg = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_power_heat(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_power_heat_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.power_heat_data, data, sizeof(referee_power_heat_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_power_heat_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_robot_pos(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_robot_pos_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.robot_pos, data, sizeof(referee_robot_pos_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_pos_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_buff(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_buff_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.buff, data, sizeof(referee_buff_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_buff_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_robot_hurt(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_robot_hurt_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.robot_hurt, data, sizeof(referee_robot_hurt_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_hurt_ms = g_last_rx_ms;
  referee_write_end();
  referee_event_push_robot_hurt(data, g_last_rx_ms);
}

static void referee_store_shoot_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_shoot_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.shoot_data, data, sizeof(referee_shoot_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_shoot_data_ms = g_last_rx_ms;
  referee_write_end();
  referee_event_push_shoot_data(data, g_last_rx_ms);
}

static void referee_store_bullet_remaining(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_bullet_remaining_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.bullet_remaining, data, sizeof(referee_bullet_remaining_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_bullet_remaining_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_rfid_status(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_rfid_status_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.rfid_status, data, sizeof(referee_rfid_status_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_rfid_status_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_dart_client_cmd(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_dart_client_cmd_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.dart_client_cmd, data, sizeof(referee_dart_client_cmd_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_dart_client_cmd_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_ground_robot_position(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_ground_robot_position_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.ground_robot_position, data, sizeof(referee_ground_robot_position_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_ground_robot_pos_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_radar_mark_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_radar_mark_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.radar_mark_data, data, sizeof(referee_radar_mark_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_mark_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_sentry_info(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_sentry_info_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.sentry_info, data, sizeof(referee_sentry_info_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_sentry_info_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_radar_info(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_radar_info_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.radar_info, data, sizeof(referee_radar_info_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_info_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_interactive_data(const uint8_t *data, uint16_t len)
{
  if (len < sizeof(referee_interactive_header_t)) {
    return;
  }

  referee_interactive_header_t hdr;
  memcpy(&hdr, data, sizeof(hdr));

  uint16_t user_len = (uint16_t)(len - sizeof(referee_interactive_header_t));
  if (user_len > sizeof(g_snapshot.robot_interactive.user_data)) {
    user_len = sizeof(g_snapshot.robot_interactive.user_data);
  }

  referee_write_begin();
  g_snapshot.robot_interactive.data_cmd_id = hdr.data_cmd_id;
  g_snapshot.robot_interactive.sender_id = hdr.sender_id;
  g_snapshot.robot_interactive.receiver_id = hdr.receiver_id;
  g_snapshot.robot_interactive.user_data_len = user_len;
  memcpy(g_snapshot.robot_interactive.user_data, data + sizeof(referee_interactive_header_t), user_len);
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_interactive_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_map_command(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_map_command_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.map_command, data, sizeof(referee_map_command_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_map_command_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_radar_map_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_radar_map_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.radar_map_data, data, sizeof(referee_radar_map_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_map_data_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_path_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_path_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.path_data, data, sizeof(referee_path_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_path_data_ms = g_last_rx_ms;
  referee_write_end();
}

static void referee_store_robot_map_data(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(referee_robot_map_data_t)) {
    return;
  }
  referee_write_begin();
  memcpy(&g_snapshot.robot_map_data, data, sizeof(referee_robot_map_data_t));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_map_data_ms = g_last_rx_ms;
  referee_write_end();
}

typedef struct {
  uint16_t cmd_id;
  uint16_t fixed_len;
  uint8_t  variable_len;
  void (*handler)(const uint8_t *data, uint16_t len);
} referee_dispatch_entry_t;

static const referee_dispatch_entry_t g_dispatch_table[] = {
  { REFEREE_CMD_GAME_STATUS,           sizeof(referee_game_status_t), 0, referee_store_game_status },
  { REFEREE_CMD_GAME_RESULT,           sizeof(referee_game_result_t), 0, referee_store_game_result },
  { REFEREE_CMD_GAME_ROBOT_HP,         sizeof(referee_game_robot_hp_t), 0, referee_store_game_robot_hp },
  { REFEREE_CMD_EVENT_DATA,            sizeof(referee_event_data_t), 0, referee_store_event_data },
  { REFEREE_CMD_REFEREE_WARNING,       sizeof(referee_referee_warning_t), 0, referee_store_warning },
  { REFEREE_CMD_DART_INFO,             sizeof(referee_dart_info_t), 0, referee_store_dart_info },
  { REFEREE_CMD_ROBOT_STATE,           sizeof(referee_robot_state_t), 0, referee_store_robot_state },
  { REFEREE_CMD_POWER_HEAT_DATA,       sizeof(referee_power_heat_data_t), 0, referee_store_power_heat },
  { REFEREE_CMD_ROBOT_POS,             sizeof(referee_robot_pos_t), 0, referee_store_robot_pos },
  { REFEREE_CMD_BUFF,                  sizeof(referee_buff_t), 0, referee_store_buff },
  { REFEREE_CMD_ROBOT_HURT,            sizeof(referee_robot_hurt_t), 0, referee_store_robot_hurt },
  { REFEREE_CMD_SHOOT_DATA,            sizeof(referee_shoot_data_t), 0, referee_store_shoot_data },
  { REFEREE_CMD_BULLET_REMAINING,      sizeof(referee_bullet_remaining_t), 0, referee_store_bullet_remaining },
  { REFEREE_CMD_RFID_STATUS,           sizeof(referee_rfid_status_t), 0, referee_store_rfid_status },
  { REFEREE_CMD_DART_CLIENT_CMD,       sizeof(referee_dart_client_cmd_t), 0, referee_store_dart_client_cmd },
  { REFEREE_CMD_GROUND_ROBOT_POSITION, sizeof(referee_ground_robot_position_t), 0, referee_store_ground_robot_position },
  { REFEREE_CMD_RADAR_MARK_DATA,       sizeof(referee_radar_mark_data_t), 0, referee_store_radar_mark_data },
  { REFEREE_CMD_SENTRY_INFO,           sizeof(referee_sentry_info_t), 0, referee_store_sentry_info },
  { REFEREE_CMD_RADAR_INFO,            sizeof(referee_radar_info_t), 0, referee_store_radar_info },
  { REFEREE_CMD_ROBOT_INTERACTIVE_DATA, 0, 1, referee_store_interactive_data },
  { REFEREE_CMD_MAP_COMMAND,           sizeof(referee_map_command_t), 0, referee_store_map_command },
  { REFEREE_CMD_RADAR_MAP_DATA,        sizeof(referee_radar_map_data_t), 0, referee_store_radar_map_data },
  { REFEREE_CMD_PATH_DATA,             sizeof(referee_path_data_t), 0, referee_store_path_data },
  { REFEREE_CMD_ROBOT_MAP_DATA,        sizeof(referee_robot_map_data_t), 0, referee_store_robot_map_data },
};

static void referee_dispatch(uint16_t cmd_id, const uint8_t *payload, uint16_t len)
{
  for (uint32_t i = 0; i < SRNOS_ARRAY_COUNT(g_dispatch_table); ++i) {
    if (g_dispatch_table[i].cmd_id == cmd_id) {
      if (g_dispatch_table[i].variable_len) {
        g_dispatch_table[i].handler(payload, len);
      } else if (len == g_dispatch_table[i].fixed_len) {
        g_dispatch_table[i].handler(payload, len);
      }
      return;
    }
  }
}

void Referee_GetSnapshot(referee_snapshot_t *out)
{
  if (out == NULL) {
    return;
  }

  uint32_t last_rx_ms = 0;
  uint32_t last_game_status_ms = 0;
  uint32_t last_game_result_ms = 0;
  uint32_t last_game_robot_hp_ms = 0;
  uint32_t last_event_data_ms = 0;
  uint32_t last_referee_warning_ms = 0;
  uint32_t last_dart_info_ms = 0;
  uint32_t last_robot_state_ms = 0;
  uint32_t last_power_heat_ms = 0;
  uint32_t last_robot_pos_ms = 0;
  uint32_t last_buff_ms = 0;
  uint32_t last_robot_hurt_ms = 0;
  uint32_t last_shoot_data_ms = 0;
  uint32_t last_bullet_remaining_ms = 0;
  uint32_t last_rfid_status_ms = 0;
  uint32_t last_dart_client_cmd_ms = 0;
  uint32_t last_ground_robot_pos_ms = 0;
  uint32_t last_radar_mark_ms = 0;
  uint32_t last_sentry_info_ms = 0;
  uint32_t last_radar_info_ms = 0;
  uint32_t last_robot_interactive_ms = 0;
  uint32_t last_map_command_ms = 0;
  uint32_t last_radar_map_data_ms = 0;
  uint32_t last_path_data_ms = 0;
  uint32_t last_robot_map_data_ms = 0;

  while (true) {
    const uint32_t seq1 = g_seq;
    if (seq1 & 1u) {
      continue;
    }
    __DMB();
    memcpy(out, &g_snapshot, sizeof(*out));
    last_rx_ms = g_last_rx_ms;
    last_game_status_ms = g_last_game_status_ms;
    last_game_result_ms = g_last_game_result_ms;
    last_game_robot_hp_ms = g_last_game_robot_hp_ms;
    last_event_data_ms = g_last_event_data_ms;
    last_referee_warning_ms = g_last_referee_warning_ms;
    last_dart_info_ms = g_last_dart_info_ms;
    last_robot_state_ms = g_last_robot_state_ms;
    last_power_heat_ms = g_last_power_heat_ms;
    last_robot_pos_ms = g_last_robot_pos_ms;
    last_buff_ms = g_last_buff_ms;
    last_robot_hurt_ms = g_last_robot_hurt_ms;
    last_shoot_data_ms = g_last_shoot_data_ms;
    last_bullet_remaining_ms = g_last_bullet_remaining_ms;
    last_rfid_status_ms = g_last_rfid_status_ms;
    last_dart_client_cmd_ms = g_last_dart_client_cmd_ms;
    last_ground_robot_pos_ms = g_last_ground_robot_pos_ms;
    last_radar_mark_ms = g_last_radar_mark_ms;
    last_sentry_info_ms = g_last_sentry_info_ms;
    last_radar_info_ms = g_last_radar_info_ms;
    last_robot_interactive_ms = g_last_robot_interactive_ms;
    last_map_command_ms = g_last_map_command_ms;
    last_radar_map_data_ms = g_last_radar_map_data_ms;
    last_path_data_ms = g_last_path_data_ms;
    last_robot_map_data_ms = g_last_robot_map_data_ms;
    __DMB();
    if (seq1 == g_seq) {
      break;
    }
  }

  const uint32_t now_ms = referee_now_ms();
  const bool rx_seen = (last_rx_ms != 0u);
  const bool robot_state_fresh = referee_is_fresh(now_ms, last_robot_state_ms, REFEREE_TIMEOUT_ROBOT_STATE_MS);
  if (!rx_seen || !robot_state_fresh) {
    memset(out, 0, sizeof(*out));
    return;
  }

  if (!referee_is_fresh(now_ms, last_game_status_ms, REFEREE_TIMEOUT_GAME_STATUS_MS)) {
    memset(&out->game_status, 0, sizeof(out->game_status));
  }
  if (!referee_is_fresh(now_ms, last_game_result_ms, REFEREE_TIMEOUT_GAME_RESULT_MS)) {
    memset(&out->game_result, 0, sizeof(out->game_result));
  }
  if (!referee_is_fresh(now_ms, last_game_robot_hp_ms, REFEREE_TIMEOUT_GAME_ROBOT_HP_MS)) {
    memset(&out->game_robot_hp, 0, sizeof(out->game_robot_hp));
  }
  if (!referee_is_fresh(now_ms, last_event_data_ms, REFEREE_TIMEOUT_EVENT_DATA_MS)) {
    memset(&out->event_data, 0, sizeof(out->event_data));
  }
  if (!referee_is_fresh(now_ms, last_referee_warning_ms, REFEREE_TIMEOUT_REFEREE_WARNING_MS)) {
    memset(&out->referee_warning, 0, sizeof(out->referee_warning));
  }
  if (!referee_is_fresh(now_ms, last_dart_info_ms, REFEREE_TIMEOUT_DART_INFO_MS)) {
    memset(&out->dart_info, 0, sizeof(out->dart_info));
  }
  if (!referee_is_fresh(now_ms, last_power_heat_ms, REFEREE_TIMEOUT_POWER_HEAT_MS)) {
    memset(&out->power_heat_data, 0, sizeof(out->power_heat_data));
  }
  if (!referee_is_fresh(now_ms, last_robot_pos_ms, REFEREE_TIMEOUT_ROBOT_POS_MS)) {
    memset(&out->robot_pos, 0, sizeof(out->robot_pos));
  }
  if (!referee_is_fresh(now_ms, last_buff_ms, REFEREE_TIMEOUT_BUFF_MS)) {
    memset(&out->buff, 0, sizeof(out->buff));
  }
  if (!referee_is_fresh(now_ms, last_robot_hurt_ms, REFEREE_TIMEOUT_ROBOT_HURT_MS)) {
    memset(&out->robot_hurt, 0, sizeof(out->robot_hurt));
  }
  if (!referee_is_fresh(now_ms, last_shoot_data_ms, REFEREE_TIMEOUT_SHOOT_DATA_MS)) {
    memset(&out->shoot_data, 0, sizeof(out->shoot_data));
  }
  if (!referee_is_fresh(now_ms, last_bullet_remaining_ms, REFEREE_TIMEOUT_BULLET_REMAINING_MS)) {
    memset(&out->bullet_remaining, 0, sizeof(out->bullet_remaining));
  }
  if (!referee_is_fresh(now_ms, last_rfid_status_ms, REFEREE_TIMEOUT_RFID_STATUS_MS)) {
    memset(&out->rfid_status, 0, sizeof(out->rfid_status));
  }
  if (!referee_is_fresh(now_ms, last_dart_client_cmd_ms, REFEREE_TIMEOUT_DART_CLIENT_CMD_MS)) {
    memset(&out->dart_client_cmd, 0, sizeof(out->dart_client_cmd));
  }
  if (!referee_is_fresh(now_ms, last_ground_robot_pos_ms, REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS)) {
    memset(&out->ground_robot_position, 0, sizeof(out->ground_robot_position));
  }
  if (!referee_is_fresh(now_ms, last_radar_mark_ms, REFEREE_TIMEOUT_RADAR_MARK_DATA_MS)) {
    memset(&out->radar_mark_data, 0, sizeof(out->radar_mark_data));
  }
  if (!referee_is_fresh(now_ms, last_sentry_info_ms, REFEREE_TIMEOUT_SENTRY_INFO_MS)) {
    memset(&out->sentry_info, 0, sizeof(out->sentry_info));
  }
  if (!referee_is_fresh(now_ms, last_radar_info_ms, REFEREE_TIMEOUT_RADAR_INFO_MS)) {
    memset(&out->radar_info, 0, sizeof(out->radar_info));
  }
  if (!referee_is_fresh(now_ms, last_robot_interactive_ms, REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS)) {
    memset(&out->robot_interactive, 0, sizeof(out->robot_interactive));
  }
  if (!referee_is_fresh(now_ms, last_map_command_ms, REFEREE_TIMEOUT_MAP_COMMAND_MS)) {
    memset(&out->map_command, 0, sizeof(out->map_command));
  }
  if (!referee_is_fresh(now_ms, last_radar_map_data_ms, REFEREE_TIMEOUT_RADAR_MAP_DATA_MS)) {
    memset(&out->radar_map_data, 0, sizeof(out->radar_map_data));
  }
  if (!referee_is_fresh(now_ms, last_path_data_ms, REFEREE_TIMEOUT_PATH_DATA_MS)) {
    memset(&out->path_data, 0, sizeof(out->path_data));
  }
  if (!referee_is_fresh(now_ms, last_robot_map_data_ms, REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS)) {
    memset(&out->robot_map_data, 0, sizeof(out->robot_map_data));
  }
}

uint32_t Referee_EventCursorNow(void)
{
  return __atomic_load_n(&g_event_write_seq, __ATOMIC_ACQUIRE);
}

bool Referee_EventReadNext(uint32_t *cursor, referee_event_t *out, uint32_t *dropped)
{
  if (cursor == NULL || out == NULL) {
    return false;
  }

  uint32_t dropped_local = 0u;

  while (true) {
    const uint32_t write_seq = __atomic_load_n(&g_event_write_seq, __ATOMIC_ACQUIRE);
    uint32_t next_seq = *cursor + 1u;

    if (next_seq == 0u || write_seq == 0u || next_seq > write_seq) {
      if (dropped != NULL) {
        *dropped = 0u;
      }
      return false;
    }

    const uint32_t oldest_seq = (write_seq > REFEREE_EVENT_RING_SIZE)
                              ? (write_seq - REFEREE_EVENT_RING_SIZE + 1u)
                              : 1u;
    if (next_seq < oldest_seq) {
      dropped_local = oldest_seq - next_seq;
      next_seq = oldest_seq;
    }

    referee_event_slot_t *slot = &g_event_ring[(next_seq - 1u) & (REFEREE_EVENT_RING_SIZE - 1u)];
    const uint32_t expected_stamp = (next_seq << 1u);

    const uint32_t s1 = __atomic_load_n(&slot->stamp, __ATOMIC_ACQUIRE);
    if (s1 != expected_stamp || (s1 & 1u)) {
      continue;
    }

    memcpy(out, (const void *)&slot->event, sizeof(*out));

    const uint32_t s2 = __atomic_load_n(&slot->stamp, __ATOMIC_ACQUIRE);
    if (s1 != s2 || (s2 & 1u) || out->seq != next_seq) {
      continue;
    }

    *cursor = next_seq;
    if (dropped != NULL) {
      *dropped = dropped_local;
    }
    return true;
  }
}

bool Referee_IsOnline(uint32_t timeout_ms)
{
  (void)timeout_ms;
  const uint32_t now_ms = referee_now_ms();
  const uint32_t last_rx_ms = g_last_rx_ms;
  const uint32_t last_robot_state_ms = g_last_robot_state_ms;
  if (last_rx_ms == 0u || last_robot_state_ms == 0u) {
    return false;
  }
  return referee_is_fresh(now_ms, last_robot_state_ms, REFEREE_TIMEOUT_ROBOT_STATE_MS);
}

uint32_t Referee_GetOverflowCnt(void)
{
  SRNOS_SerialStats stats;
  return SRNOS_SerialGetStats(g_referee_channel,&stats)==SRNOS_SERIAL_OK?stats.rx_overflow:0;
}

uint32_t Referee_GetLastRxTickMs(void)
{
  return g_last_rx_ms;
}

void Referee_TxInit(void)
{
  g_tx_len = 0;
  g_tx_busy = 0;
}

bool Referee_TxSend(const uint8_t *data,uint16_t len) {return RefereeTX_Send(data,len,0);}
