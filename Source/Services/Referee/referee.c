#include "SirinEmbedOS/Services/Referee/referee.h"
#include "SirinEmbedOS/Core/C/Array.h"

#include <string.h>
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Services/Referee/Transmitter/RefereeTransmitter.h"
#define SRNOS_REFEREE_MEMORY_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
static uint32_t SRNOS_RefereeNowMs(void) {return (uint32_t)((uint64_t)osKernelGetTickCount()*1000u/osKernelGetTickFreq());}

#include "SirinEmbedOS/Core/Memory/MemorySections.h"

#ifndef SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#define SRNOS_REFEREE_TIMEOUT_DEFAULT_MS 1000u
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_GAME_STATUS_MS
#define SRNOS_REFEREE_TIMEOUT_GAME_STATUS_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_GAME_RESULT_MS
#define SRNOS_REFEREE_TIMEOUT_GAME_RESULT_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_GAME_ROBOT_HP_MS
#define SRNOS_REFEREE_TIMEOUT_GAME_ROBOT_HP_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_EVENT_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_EVENT_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_REFEREE_WARNING_MS
#define SRNOS_REFEREE_TIMEOUT_REFEREE_WARNING_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_DART_INFO_MS
#define SRNOS_REFEREE_TIMEOUT_DART_INFO_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_ROBOT_STATE_MS
#define SRNOS_REFEREE_TIMEOUT_ROBOT_STATE_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_POWER_HEAT_MS
#define SRNOS_REFEREE_TIMEOUT_POWER_HEAT_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_ROBOT_POS_MS
#define SRNOS_REFEREE_TIMEOUT_ROBOT_POS_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_BUFF_MS
#define SRNOS_REFEREE_TIMEOUT_BUFF_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_ROBOT_HURT_MS
#define SRNOS_REFEREE_TIMEOUT_ROBOT_HURT_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_SHOOT_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_SHOOT_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_BULLET_REMAINING_MS
#define SRNOS_REFEREE_TIMEOUT_BULLET_REMAINING_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_RFID_STATUS_MS
#define SRNOS_REFEREE_TIMEOUT_RFID_STATUS_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_DART_CLIENT_CMD_MS
#define SRNOS_REFEREE_TIMEOUT_DART_CLIENT_CMD_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS
#define SRNOS_REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_RADAR_MARK_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_RADAR_MARK_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_SENTRY_INFO_MS
#define SRNOS_REFEREE_TIMEOUT_SENTRY_INFO_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_RADAR_INFO_MS
#define SRNOS_REFEREE_TIMEOUT_RADAR_INFO_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS
#define SRNOS_REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_MAP_COMMAND_MS
#define SRNOS_REFEREE_TIMEOUT_MAP_COMMAND_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_RADAR_MAP_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_RADAR_MAP_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_PATH_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_PATH_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif
#ifndef SRNOS_REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS
#define SRNOS_REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS SRNOS_REFEREE_TIMEOUT_DEFAULT_MS
#endif

// ================================ Section A: protocol constants & packed structs ================================
// Packed structs and cmd_id definitions are in referee.h for public access.

enum {
  SRNOS_REFEREE_HEADER_LEN = 5,
  SRNOS_REFEREE_CMDID_LEN = 2,
  SRNOS_REFEREE_CRC16_LEN = 2,
  SRNOS_REFEREE_FRAME_BUF_SIZE = 256,
  SRNOS_REFEREE_RING_SIZE = 8192,
  SRNOS_REFEREE_DMA_BUF_SIZE = 256,
  SRNOS_REFEREE_TX_BUF_SIZE = 256,
  SRNOS_REFEREE_MAX_DATA_LEN = SRNOS_REFEREE_FRAME_BUF_SIZE - SRNOS_REFEREE_HEADER_LEN - SRNOS_REFEREE_CMDID_LEN - SRNOS_REFEREE_CRC16_LEN
};

#if (SRNOS_REFEREE_EVENT_RING_SIZE == 0u)
  #error "SRNOS_REFEREE_EVENT_RING_SIZE must be > 0"
#endif
#if ((SRNOS_REFEREE_EVENT_RING_SIZE & (SRNOS_REFEREE_EVENT_RING_SIZE - 1u)) != 0u)
  #error "SRNOS_REFEREE_EVENT_RING_SIZE must be power-of-two"
#endif

// ================================ Section B: CRC8 / CRC16 ================================
static uint8_t SRNOS_RefereeCrc8Calc(const uint8_t *data, uint16_t len)
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
static uint16_t SRNOS_RefereeCrc16Calc(const uint8_t *data, uint16_t len)
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

static bool SRNOS_RefereeCrc8VerifyHeader(const uint8_t *hdr5)
{
  return SRNOS_RefereeCrc8Calc(hdr5, 4) == hdr5[4];
}

static bool SRNOS_RefereeCrc16VerifyFrame(const uint8_t *frame, uint16_t total_len)
{
  if (total_len < (SRNOS_REFEREE_HEADER_LEN + SRNOS_REFEREE_CMDID_LEN + SRNOS_REFEREE_CRC16_LEN)) {
    return false;
  }
  const uint16_t crc_calc = SRNOS_RefereeCrc16Calc(frame, (uint16_t)(total_len - SRNOS_REFEREE_CRC16_LEN));
  const uint16_t crc_rx = (uint16_t)(frame[total_len - 2] | (frame[total_len - 1] << 8));
  return crc_calc == crc_rx;
}

// ================================ Section C: Ring Buffer (ISR write / Task read) ================================

static SRNOS_RefereeSnapshot g_snapshot;
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
  SRNOS_RefereeEvent event;
} SRNOS_RefereeEventSlot;

static SRNOS_RefereeEventSlot g_event_ring[SRNOS_REFEREE_EVENT_RING_SIZE];
static volatile uint32_t g_event_write_seq = 0;

static void SRNOS_RefereeEventPushRobotHurt(const uint8_t *hurt_payload, uint32_t rx_tick_ms)
{
  if (hurt_payload == NULL) {
    return;
  }

  const uint32_t seq = __atomic_add_fetch(&g_event_write_seq, 1u, __ATOMIC_ACQ_REL);
  SRNOS_RefereeEventSlot *slot = &g_event_ring[(seq - 1u) & (SRNOS_REFEREE_EVENT_RING_SIZE - 1u)];

  __atomic_store_n(&slot->stamp, (seq << 1u) | 1u, __ATOMIC_RELEASE);
  slot->event.seq = seq;
  slot->event.rx_tick_ms = rx_tick_ms;
  slot->event.cmd_id = (uint16_t)SRNOS_REFEREE_EVENT_TYPE_ROBOT_HURT;
  slot->event.reserved = 0u;
  memcpy(&slot->event.data.robot_hurt, hurt_payload, sizeof(slot->event.data.robot_hurt));
  __atomic_store_n(&slot->stamp, (seq << 1u), __ATOMIC_RELEASE);
}

static void SRNOS_RefereeEventPushShootData(const uint8_t *shoot_payload, uint32_t rx_tick_ms)
{
  if (shoot_payload == NULL) {
    return;
  }

  const uint32_t seq = __atomic_add_fetch(&g_event_write_seq, 1u, __ATOMIC_ACQ_REL);
  SRNOS_RefereeEventSlot *slot = &g_event_ring[(seq - 1u) & (SRNOS_REFEREE_EVENT_RING_SIZE - 1u)];

  __atomic_store_n(&slot->stamp, (seq << 1u) | 1u, __ATOMIC_RELEASE);
  slot->event.seq = seq;
  slot->event.rx_tick_ms = rx_tick_ms;
  slot->event.cmd_id = (uint16_t)SRNOS_REFEREE_EVENT_TYPE_SHOOT_DATA;
  slot->event.reserved = 0u;
  memcpy(&slot->event.data.shoot_data, shoot_payload, sizeof(slot->event.data.shoot_data));
  __atomic_store_n(&slot->stamp, (seq << 1u), __ATOMIC_RELEASE);
}

// ================================ Section D: caller-bound UART DMA + IDLE receive ================================
static SRNOS_SerialChannel *SRNOS_DebugRefereeChannel = NULL;
static bool SRNOS_DebugRefereeInitialized = false;

static volatile uint32_t g_last_rx_ms = 0;

volatile SRNOS_RefereeSnapshot SRNOS_DebugRefereeSnapshotRawDbg;
volatile uint32_t SRNOS_DebugRefereeRawLastRxMsDbg = 0;
volatile uint16_t SRNOS_DebugRefereeLastCmdIdDbg = 0;
volatile uint16_t SRNOS_DebugRefereeLastPayloadLenDbg = 0;
volatile uint32_t SRNOS_DebugRefereeLastCRCOkMsDbg = 0;
volatile uint32_t SRNOS_DebugRefereeCRCOkCntDbg = 0;
volatile uint32_t SRNOS_DebugRefereeCRCFailCntDbg = 0;
volatile uint32_t SRNOS_DebugRefereeLastRobotStateMsDbg = 0;

static void SRNOS_RefereeParserReset(void);
bool SRNOS_RefereeBindChannel(SRNOS_SerialChannel *channel) {
 const SRNOS_SerialConfig *cfg=SRNOS_SerialGetConfig(channel);
 if(!cfg||!cfg->rx_enabled||SRNOS_DebugRefereeInitialized||(SRNOS_DebugRefereeChannel&&SRNOS_DebugRefereeChannel!=channel))return false;
 if(SRNOS_SerialClaim(channel,&SRNOS_DebugRefereeChannel)!=SRNOS_SERIAL_OK)return false;
 SRNOS_DebugRefereeChannel=channel;return SRNOS_RefereeTxBindChannel(channel);
}
SRNOS_SerialChannel *SRNOS_RefereeGetBoundChannel(void){return SRNOS_DebugRefereeChannel;}

bool SRNOS_RefereeInit(void)
{
  if (SRNOS_DebugRefereeChannel == NULL || SRNOS_DebugRefereeInitialized) {
    return false;
  }
  g_last_rx_ms = 0;
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
  SRNOS_RefereeParserReset();
  SRNOS_DebugRefereeInitialized = true;
  return true;
}

bool SRNOS_RefereeReceiverReady(void) { return SRNOS_DebugRefereeInitialized; }

// ================================ Section E: Protocol parse state machine ================================
typedef enum {
  SRNOS_REFEREE_PARSE_WAIT_SOF = 0,
  SRNOS_REFEREE_PARSE_READ_HEADER,
  SRNOS_REFEREE_PARSE_READ_CMDID,
  SRNOS_REFEREE_PARSE_READ_DATA,
  SRNOS_REFEREE_PARSE_READ_CRC16
} SRNOS_RefereeParseState;

static SRNOS_RefereeParseState g_parse_state = SRNOS_REFEREE_PARSE_WAIT_SOF;
static uint16_t g_expected_data_len = 0;
static uint16_t g_frame_idx = 0;
static uint16_t g_cmd_id = 0;
static uint8_t g_frame_buf[SRNOS_REFEREE_FRAME_BUF_SIZE];

static void SRNOS_RefereeParserReset(void)
{
  g_parse_state = SRNOS_REFEREE_PARSE_WAIT_SOF;
  g_expected_data_len = 0;
  g_frame_idx = 0;
  g_cmd_id = 0;
}

static void SRNOS_RefereeDispatch(uint16_t cmd_id, const uint8_t *payload, uint16_t len);

static void SRNOS_RefereeParseByte(uint8_t b)
{
  switch (g_parse_state) {
    case SRNOS_REFEREE_PARSE_WAIT_SOF:
      if (b == SRNOS_REFEREE_SOF) {
        g_frame_idx = 0;
        g_frame_buf[g_frame_idx++] = b;
        g_parse_state = SRNOS_REFEREE_PARSE_READ_HEADER;
      }
      break;

    case SRNOS_REFEREE_PARSE_READ_HEADER:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == SRNOS_REFEREE_HEADER_LEN) {
        if (!SRNOS_RefereeCrc8VerifyHeader(g_frame_buf)) {
          SRNOS_RefereeParserReset();
          break;
        }
        g_expected_data_len = (uint16_t)(g_frame_buf[1] | (g_frame_buf[2] << 8));
        if (g_expected_data_len > SRNOS_REFEREE_MAX_DATA_LEN) {
          SRNOS_RefereeParserReset();
          break;
        }
        g_parse_state = SRNOS_REFEREE_PARSE_READ_CMDID;
      }
      break;

    case SRNOS_REFEREE_PARSE_READ_CMDID:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (SRNOS_REFEREE_HEADER_LEN + SRNOS_REFEREE_CMDID_LEN)) {
        g_cmd_id = (uint16_t)(g_frame_buf[5] | (g_frame_buf[6] << 8));
        if (g_expected_data_len == 0) {
          g_parse_state = SRNOS_REFEREE_PARSE_READ_CRC16;
        } else {
          g_parse_state = SRNOS_REFEREE_PARSE_READ_DATA;
        }
      }
      break;

    case SRNOS_REFEREE_PARSE_READ_DATA:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (SRNOS_REFEREE_HEADER_LEN + SRNOS_REFEREE_CMDID_LEN + g_expected_data_len)) {
        g_parse_state = SRNOS_REFEREE_PARSE_READ_CRC16;
      }
      break;

    case SRNOS_REFEREE_PARSE_READ_CRC16:
      g_frame_buf[g_frame_idx++] = b;
      if (g_frame_idx == (SRNOS_REFEREE_HEADER_LEN + SRNOS_REFEREE_CMDID_LEN + g_expected_data_len + SRNOS_REFEREE_CRC16_LEN)) {
        if (SRNOS_RefereeCrc16VerifyFrame(g_frame_buf, g_frame_idx)) {
          SRNOS_DebugRefereeCRCOkCntDbg++;
          SRNOS_DebugRefereeLastCmdIdDbg = g_cmd_id;
          SRNOS_DebugRefereeLastPayloadLenDbg = g_expected_data_len;
          SRNOS_DebugRefereeLastCRCOkMsDbg = g_last_rx_ms;
          const uint8_t *payload = &g_frame_buf[SRNOS_REFEREE_HEADER_LEN + SRNOS_REFEREE_CMDID_LEN];
          SRNOS_RefereeDispatch(g_cmd_id, payload, g_expected_data_len);
        } else {
          SRNOS_DebugRefereeCRCFailCntDbg++;
        }
        SRNOS_RefereeParserReset();
      }
      break;

    default:
      SRNOS_RefereeParserReset();
      break;
  }
}

static void SRNOS_RefereeParseBytes(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; ++i) {
    SRNOS_RefereeParseByte(data[i]);
  }
}

void SRNOS_RefereeFeed(const uint8_t *data,size_t size,uint32_t timestamp_ms) {
 if(!SRNOS_DebugRefereeInitialized||!data)return;
 g_last_rx_ms=timestamp_ms;SRNOS_DebugRefereeRawLastRxMsDbg=timestamp_ms;
 while(size){uint16_t n=(uint16_t)(size>UINT16_MAX?UINT16_MAX:size);SRNOS_RefereeParseBytes(data,n);data+=n;size-=n;}
}

void SRNOS_RefereePoll(void) {
 uint8_t tmp[128];size_t got;
 for(unsigned budget=0;budget<8;budget++) {
  if(SRNOS_SerialRead(SRNOS_DebugRefereeChannel,tmp,sizeof(tmp),&got,0)!=SRNOS_SERIAL_OK)break;
  SRNOS_RefereeFeed(tmp,got,SRNOS_RefereeNowMs());
 }
}

// ================================ Section F: cmd_id dispatch + data storage + seqlock ================================
static void SRNOS_RefereeWriteBegin(void)
{
  taskENTER_CRITICAL();
  g_seq++;
  SRNOS_REFEREE_MEMORY_BARRIER();
}

static void SRNOS_RefereeWriteEnd(void)
{
  SRNOS_REFEREE_MEMORY_BARRIER();
  memcpy((void *)&SRNOS_DebugRefereeSnapshotRawDbg, &g_snapshot, sizeof(g_snapshot));
  g_seq++;
  taskEXIT_CRITICAL();
}

static bool SRNOS_RefereeIsFresh(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms)
{
  if (last_ms == 0u) {
    return false;
  }
  return (uint32_t)(now_ms - last_ms) <= timeout_ms;
}

static void SRNOS_RefereeStoreGameStatus(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeGameStatus)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.game_status, data, sizeof(SRNOS_RefereeGameStatus));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_status_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreGameResult(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeGameResult)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.game_result, data, sizeof(SRNOS_RefereeGameResult));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_result_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreGameRobotHp(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeGameRobotHp)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.game_robot_hp, data, sizeof(SRNOS_RefereeGameRobotHp));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_game_robot_hp_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreEventData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeEventData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.event_data, data, sizeof(SRNOS_RefereeEventData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_event_data_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreWarning(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRefereeWarning)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.SRNOS_RefereeWarning, data, sizeof(SRNOS_RefereeRefereeWarning));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_referee_warning_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreDartInfo(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeDartInfo)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.dart_info, data, sizeof(SRNOS_RefereeDartInfo));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_dart_info_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRobotState(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRobotState)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.robot_state, data, sizeof(SRNOS_RefereeRobotState));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_state_ms = g_last_rx_ms;
  SRNOS_DebugRefereeLastRobotStateMsDbg = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStorePowerHeat(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereePowerHeatData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.power_heat_data, data, sizeof(SRNOS_RefereePowerHeatData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_power_heat_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRobotPos(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRobotPos)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.robot_pos, data, sizeof(SRNOS_RefereeRobotPos));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_pos_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreBuff(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeBuff)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.buff, data, sizeof(SRNOS_RefereeBuff));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_buff_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRobotHurt(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRobotHurt)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.robot_hurt, data, sizeof(SRNOS_RefereeRobotHurt));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_hurt_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
  SRNOS_RefereeEventPushRobotHurt(data, g_last_rx_ms);
}

static void SRNOS_RefereeStoreShootData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeShootData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.shoot_data, data, sizeof(SRNOS_RefereeShootData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_shoot_data_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
  SRNOS_RefereeEventPushShootData(data, g_last_rx_ms);
}

static void SRNOS_RefereeStoreBulletRemaining(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeBulletRemaining)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.bullet_remaining, data, sizeof(SRNOS_RefereeBulletRemaining));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_bullet_remaining_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRfidStatus(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRfidStatus)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.rfid_status, data, sizeof(SRNOS_RefereeRfidStatus));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_rfid_status_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreDartClientCmd(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeDartClientCmd)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.dart_client_cmd, data, sizeof(SRNOS_RefereeDartClientCmd));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_dart_client_cmd_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreGroundRobotPosition(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeGroundRobotPosition)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.ground_robot_position, data, sizeof(SRNOS_RefereeGroundRobotPosition));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_ground_robot_pos_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRadarMarkData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRadarMarkData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.radar_mark_data, data, sizeof(SRNOS_RefereeRadarMarkData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_mark_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreSentryInfo(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeSentryInfo)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.sentry_info, data, sizeof(SRNOS_RefereeSentryInfo));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_sentry_info_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRadarInfo(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRadarInfo)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.radar_info, data, sizeof(SRNOS_RefereeRadarInfo));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_info_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreInteractiveData(const uint8_t *data, uint16_t len)
{
  if (len < sizeof(SRNOS_RefereeInteractiveHeader)) {
    return;
  }

  SRNOS_RefereeInteractiveHeader hdr;
  memcpy(&hdr, data, sizeof(hdr));

  uint16_t user_len = (uint16_t)(len - sizeof(SRNOS_RefereeInteractiveHeader));
  if (user_len > sizeof(g_snapshot.robot_interactive.user_data)) {
    user_len = sizeof(g_snapshot.robot_interactive.user_data);
  }

  SRNOS_RefereeWriteBegin();
  g_snapshot.robot_interactive.data_cmd_id = hdr.data_cmd_id;
  g_snapshot.robot_interactive.sender_id = hdr.sender_id;
  g_snapshot.robot_interactive.receiver_id = hdr.receiver_id;
  g_snapshot.robot_interactive.user_data_len = user_len;
  memcpy(g_snapshot.robot_interactive.user_data, data + sizeof(SRNOS_RefereeInteractiveHeader), user_len);
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_interactive_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreMapCommand(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeMapCommand)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.map_command, data, sizeof(SRNOS_RefereeMapCommand));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_map_command_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRadarMapData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRadarMapData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.radar_map_data, data, sizeof(SRNOS_RefereeRadarMapData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_radar_map_data_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStorePathData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereePathData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.path_data, data, sizeof(SRNOS_RefereePathData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_path_data_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

static void SRNOS_RefereeStoreRobotMapData(const uint8_t *data, uint16_t len)
{
  if (len != sizeof(SRNOS_RefereeRobotMapData)) {
    return;
  }
  SRNOS_RefereeWriteBegin();
  memcpy(&g_snapshot.robot_map_data, data, sizeof(SRNOS_RefereeRobotMapData));
  g_snapshot.last_rx_ms = g_last_rx_ms;
  g_last_robot_map_data_ms = g_last_rx_ms;
  SRNOS_RefereeWriteEnd();
}

typedef struct {
  uint16_t cmd_id;
  uint16_t fixed_len;
  uint8_t  variable_len;
  void (*handler)(const uint8_t *data, uint16_t len);
} SRNOS_RefereeDispatchEntry;

static const SRNOS_RefereeDispatchEntry g_dispatch_table[] = {
  { SRNOS_REFEREE_CMD_GAME_STATUS,           sizeof(SRNOS_RefereeGameStatus), 0, SRNOS_RefereeStoreGameStatus },
  { SRNOS_REFEREE_CMD_GAME_RESULT,           sizeof(SRNOS_RefereeGameResult), 0, SRNOS_RefereeStoreGameResult },
  { SRNOS_REFEREE_CMD_GAME_ROBOT_HP,         sizeof(SRNOS_RefereeGameRobotHp), 0, SRNOS_RefereeStoreGameRobotHp },
  { SRNOS_REFEREE_CMD_EVENT_DATA,            sizeof(SRNOS_RefereeEventData), 0, SRNOS_RefereeStoreEventData },
  { SRNOS_REFEREE_CMD_REFEREE_WARNING,       sizeof(SRNOS_RefereeRefereeWarning), 0, SRNOS_RefereeStoreWarning },
  { SRNOS_REFEREE_CMD_DART_INFO,             sizeof(SRNOS_RefereeDartInfo), 0, SRNOS_RefereeStoreDartInfo },
  { SRNOS_REFEREE_CMD_ROBOT_STATE,           sizeof(SRNOS_RefereeRobotState), 0, SRNOS_RefereeStoreRobotState },
  { SRNOS_REFEREE_CMD_POWER_HEAT_DATA,       sizeof(SRNOS_RefereePowerHeatData), 0, SRNOS_RefereeStorePowerHeat },
  { SRNOS_REFEREE_CMD_ROBOT_POS,             sizeof(SRNOS_RefereeRobotPos), 0, SRNOS_RefereeStoreRobotPos },
  { SRNOS_REFEREE_CMD_BUFF,                  sizeof(SRNOS_RefereeBuff), 0, SRNOS_RefereeStoreBuff },
  { SRNOS_REFEREE_CMD_ROBOT_HURT,            sizeof(SRNOS_RefereeRobotHurt), 0, SRNOS_RefereeStoreRobotHurt },
  { SRNOS_REFEREE_CMD_SHOOT_DATA,            sizeof(SRNOS_RefereeShootData), 0, SRNOS_RefereeStoreShootData },
  { SRNOS_REFEREE_CMD_BULLET_REMAINING,      sizeof(SRNOS_RefereeBulletRemaining), 0, SRNOS_RefereeStoreBulletRemaining },
  { SRNOS_REFEREE_CMD_RFID_STATUS,           sizeof(SRNOS_RefereeRfidStatus), 0, SRNOS_RefereeStoreRfidStatus },
  { SRNOS_REFEREE_CMD_DART_CLIENT_CMD,       sizeof(SRNOS_RefereeDartClientCmd), 0, SRNOS_RefereeStoreDartClientCmd },
  { SRNOS_REFEREE_CMD_GROUND_ROBOT_POSITION, sizeof(SRNOS_RefereeGroundRobotPosition), 0, SRNOS_RefereeStoreGroundRobotPosition },
  { SRNOS_REFEREE_CMD_RADAR_MARK_DATA,       sizeof(SRNOS_RefereeRadarMarkData), 0, SRNOS_RefereeStoreRadarMarkData },
  { SRNOS_REFEREE_CMD_SENTRY_INFO,           sizeof(SRNOS_RefereeSentryInfo), 0, SRNOS_RefereeStoreSentryInfo },
  { SRNOS_REFEREE_CMD_RADAR_INFO,            sizeof(SRNOS_RefereeRadarInfo), 0, SRNOS_RefereeStoreRadarInfo },
  { SRNOS_REFEREE_CMD_ROBOT_INTERACTIVE_DATA, 0, 1, SRNOS_RefereeStoreInteractiveData },
  { SRNOS_REFEREE_CMD_MAP_COMMAND,           sizeof(SRNOS_RefereeMapCommand), 0, SRNOS_RefereeStoreMapCommand },
  { SRNOS_REFEREE_CMD_RADAR_MAP_DATA,        sizeof(SRNOS_RefereeRadarMapData), 0, SRNOS_RefereeStoreRadarMapData },
  { SRNOS_REFEREE_CMD_PATH_DATA,             sizeof(SRNOS_RefereePathData), 0, SRNOS_RefereeStorePathData },
  { SRNOS_REFEREE_CMD_ROBOT_MAP_DATA,        sizeof(SRNOS_RefereeRobotMapData), 0, SRNOS_RefereeStoreRobotMapData },
};

static void SRNOS_RefereeDispatch(uint16_t cmd_id, const uint8_t *payload, uint16_t len)
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

void SRNOS_RefereeGetSnapshot(SRNOS_RefereeSnapshot *out)
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
    SRNOS_REFEREE_MEMORY_BARRIER();
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
    SRNOS_REFEREE_MEMORY_BARRIER();
    if (seq1 == g_seq) {
      break;
    }
  }

  const uint32_t now_ms = SRNOS_RefereeNowMs();
  const bool rx_seen = (last_rx_ms != 0u);
  const bool robot_state_fresh = SRNOS_RefereeIsFresh(now_ms, last_robot_state_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_STATE_MS);
  if (!rx_seen || !robot_state_fresh) {
    memset(out, 0, sizeof(*out));
    return;
  }

  if (!SRNOS_RefereeIsFresh(now_ms, last_game_status_ms, SRNOS_REFEREE_TIMEOUT_GAME_STATUS_MS)) {
    memset(&out->game_status, 0, sizeof(out->game_status));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_game_result_ms, SRNOS_REFEREE_TIMEOUT_GAME_RESULT_MS)) {
    memset(&out->game_result, 0, sizeof(out->game_result));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_game_robot_hp_ms, SRNOS_REFEREE_TIMEOUT_GAME_ROBOT_HP_MS)) {
    memset(&out->game_robot_hp, 0, sizeof(out->game_robot_hp));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_event_data_ms, SRNOS_REFEREE_TIMEOUT_EVENT_DATA_MS)) {
    memset(&out->event_data, 0, sizeof(out->event_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_referee_warning_ms, SRNOS_REFEREE_TIMEOUT_REFEREE_WARNING_MS)) {
    memset(&out->SRNOS_RefereeWarning, 0, sizeof(out->SRNOS_RefereeWarning));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_dart_info_ms, SRNOS_REFEREE_TIMEOUT_DART_INFO_MS)) {
    memset(&out->dart_info, 0, sizeof(out->dart_info));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_power_heat_ms, SRNOS_REFEREE_TIMEOUT_POWER_HEAT_MS)) {
    memset(&out->power_heat_data, 0, sizeof(out->power_heat_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_robot_pos_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_POS_MS)) {
    memset(&out->robot_pos, 0, sizeof(out->robot_pos));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_buff_ms, SRNOS_REFEREE_TIMEOUT_BUFF_MS)) {
    memset(&out->buff, 0, sizeof(out->buff));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_robot_hurt_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_HURT_MS)) {
    memset(&out->robot_hurt, 0, sizeof(out->robot_hurt));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_shoot_data_ms, SRNOS_REFEREE_TIMEOUT_SHOOT_DATA_MS)) {
    memset(&out->shoot_data, 0, sizeof(out->shoot_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_bullet_remaining_ms, SRNOS_REFEREE_TIMEOUT_BULLET_REMAINING_MS)) {
    memset(&out->bullet_remaining, 0, sizeof(out->bullet_remaining));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_rfid_status_ms, SRNOS_REFEREE_TIMEOUT_RFID_STATUS_MS)) {
    memset(&out->rfid_status, 0, sizeof(out->rfid_status));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_dart_client_cmd_ms, SRNOS_REFEREE_TIMEOUT_DART_CLIENT_CMD_MS)) {
    memset(&out->dart_client_cmd, 0, sizeof(out->dart_client_cmd));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_ground_robot_pos_ms, SRNOS_REFEREE_TIMEOUT_GROUND_ROBOT_POS_MS)) {
    memset(&out->ground_robot_position, 0, sizeof(out->ground_robot_position));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_radar_mark_ms, SRNOS_REFEREE_TIMEOUT_RADAR_MARK_DATA_MS)) {
    memset(&out->radar_mark_data, 0, sizeof(out->radar_mark_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_sentry_info_ms, SRNOS_REFEREE_TIMEOUT_SENTRY_INFO_MS)) {
    memset(&out->sentry_info, 0, sizeof(out->sentry_info));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_radar_info_ms, SRNOS_REFEREE_TIMEOUT_RADAR_INFO_MS)) {
    memset(&out->radar_info, 0, sizeof(out->radar_info));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_robot_interactive_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_INTERACTIVE_MS)) {
    memset(&out->robot_interactive, 0, sizeof(out->robot_interactive));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_map_command_ms, SRNOS_REFEREE_TIMEOUT_MAP_COMMAND_MS)) {
    memset(&out->map_command, 0, sizeof(out->map_command));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_radar_map_data_ms, SRNOS_REFEREE_TIMEOUT_RADAR_MAP_DATA_MS)) {
    memset(&out->radar_map_data, 0, sizeof(out->radar_map_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_path_data_ms, SRNOS_REFEREE_TIMEOUT_PATH_DATA_MS)) {
    memset(&out->path_data, 0, sizeof(out->path_data));
  }
  if (!SRNOS_RefereeIsFresh(now_ms, last_robot_map_data_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_MAP_DATA_MS)) {
    memset(&out->robot_map_data, 0, sizeof(out->robot_map_data));
  }
}

uint32_t SRNOS_RefereeEventCursorNow(void)
{
  return __atomic_load_n(&g_event_write_seq, __ATOMIC_ACQUIRE);
}

bool SRNOS_RefereeEventReadNext(uint32_t *cursor, SRNOS_RefereeEvent *out, uint32_t *dropped)
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

    const uint32_t oldest_seq = (write_seq > SRNOS_REFEREE_EVENT_RING_SIZE)
                              ? (write_seq - SRNOS_REFEREE_EVENT_RING_SIZE + 1u)
                              : 1u;
    if (next_seq < oldest_seq) {
      dropped_local = oldest_seq - next_seq;
      next_seq = oldest_seq;
    }

    SRNOS_RefereeEventSlot *slot = &g_event_ring[(next_seq - 1u) & (SRNOS_REFEREE_EVENT_RING_SIZE - 1u)];
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

bool SRNOS_RefereeIsOnline(uint32_t timeout_ms)
{
  (void)timeout_ms;
  const uint32_t now_ms = SRNOS_RefereeNowMs();
  const uint32_t last_rx_ms = g_last_rx_ms;
  const uint32_t last_robot_state_ms = g_last_robot_state_ms;
  if (last_rx_ms == 0u || last_robot_state_ms == 0u) {
    return false;
  }
  return SRNOS_RefereeIsFresh(now_ms, last_robot_state_ms, SRNOS_REFEREE_TIMEOUT_ROBOT_STATE_MS);
}

uint32_t SRNOS_RefereeGetOverflowCnt(void)
{
  SRNOS_SerialStats stats;
  return SRNOS_SerialGetStats(SRNOS_DebugRefereeChannel,&stats)==SRNOS_SERIAL_OK?stats.rx_overflow:0;
}

uint32_t SRNOS_RefereeGetLastRxTickMs(void)
{
  return g_last_rx_ms;
}

bool SRNOS_RefereeTxSend(const uint8_t *data,uint16_t len) {return SRNOS_RefereeTransmit(data,len,0);}
