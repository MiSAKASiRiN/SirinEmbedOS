#ifndef SRNOS_REFEREE_H
#define SRNOS_REFEREE_H

#include <stdbool.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/Serial/Serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_REFEREE_EVENT_RING_SIZE
#define SRNOS_REFEREE_EVENT_RING_SIZE 64u
#endif

#define SRNOS_REFEREE_SOF 0xA5u

typedef struct __attribute__((packed)) {
  uint8_t  sof;
  uint16_t data_length;
  uint8_t  seq;
  uint8_t  crc8;
} SRNOS_RefereeFrameHeader;

typedef enum {
  SRNOS_REFEREE_CMD_GAME_STATUS               = 0x0001,
  SRNOS_REFEREE_CMD_GAME_RESULT               = 0x0002,
  SRNOS_REFEREE_CMD_GAME_ROBOT_HP             = 0x0003,

  SRNOS_REFEREE_CMD_EVENT_DATA                = 0x0101,
  SRNOS_REFEREE_CMD_REFEREE_WARNING           = 0x0104,
  SRNOS_REFEREE_CMD_DART_INFO                 = 0x0105,

  SRNOS_REFEREE_CMD_ROBOT_STATE               = 0x0201,
  SRNOS_REFEREE_CMD_POWER_HEAT_DATA           = 0x0202,
  SRNOS_REFEREE_CMD_ROBOT_POS                 = 0x0203,
  SRNOS_REFEREE_CMD_BUFF                      = 0x0204,
  SRNOS_REFEREE_CMD_ROBOT_HURT                = 0x0206,
  SRNOS_REFEREE_CMD_SHOOT_DATA                = 0x0207,
  SRNOS_REFEREE_CMD_BULLET_REMAINING          = 0x0208,
  SRNOS_REFEREE_CMD_RFID_STATUS               = 0x0209,
  SRNOS_REFEREE_CMD_DART_CLIENT_CMD           = 0x020A,
  SRNOS_REFEREE_CMD_GROUND_ROBOT_POSITION     = 0x020B,
  SRNOS_REFEREE_CMD_RADAR_MARK_DATA           = 0x020C,
  SRNOS_REFEREE_CMD_SENTRY_INFO               = 0x020D,
  SRNOS_REFEREE_CMD_RADAR_INFO                = 0x020E,

  SRNOS_REFEREE_CMD_ROBOT_INTERACTIVE_DATA    = 0x0301,
  SRNOS_REFEREE_CMD_MAP_COMMAND               = 0x0303,
  SRNOS_REFEREE_CMD_RADAR_MAP_DATA            = 0x0305,
  SRNOS_REFEREE_CMD_PATH_DATA                 = 0x0307,
  SRNOS_REFEREE_CMD_ROBOT_MAP_DATA            = 0x0308
} SRNOS_RefereeCmdId;

// ---------------- Section A: protocol packed structs ----------------
// 0x0001
typedef struct __attribute__((packed)) {
  uint8_t  game_type     : 4;
  uint8_t  game_progress : 4;
  uint16_t stage_remain_time;
  uint64_t SyncTimeStamp;
} SRNOS_RefereeGameStatus;

// 0x0002
typedef struct __attribute__((packed)) {
  uint8_t winner;
} SRNOS_RefereeGameResult;

// 0x0003
typedef struct __attribute__((packed)) {
  uint16_t ally_1_robot_HP;
  uint16_t ally_2_robot_HP;
  uint16_t ally_3_robot_HP;
  uint16_t ally_4_robot_HP;
  uint16_t reserved;
  uint16_t ally_7_robot_HP;
  uint16_t ally_outpost_HP;
  uint16_t ally_base_HP;
} SRNOS_RefereeGameRobotHp;

// 0x0101
typedef struct __attribute__((packed)) {
  uint32_t event_data;
} SRNOS_RefereeEventData;

// 0x0104
typedef struct __attribute__((packed)) {
  uint8_t level;
  uint8_t offending_robot_id;
  uint8_t count;
} SRNOS_RefereeRefereeWarning;

// 0x0105
typedef struct __attribute__((packed)) {
  uint8_t  dart_remaining_time;
  uint16_t dart_info;
} SRNOS_RefereeDartInfo;

  // 0x0201 Robot State (SHORT VERSION, payload len = 13)
  typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  robot_level;
    uint16_t current_HP;
    uint16_t maximum_HP;
    uint16_t shooter_barrel_cooling_value;
    uint16_t shooter_barrel_heat_limit;
    uint16_t chassis_power_limit;
    uint8_t  power_management_gimbal_output  : 1;
    uint8_t  power_management_chassis_output : 1;
    uint8_t  power_management_shooter_output : 1;
    uint8_t  reserved_bits                   : 5;
  } SRNOS_RefereeRobotState;

  // _Static_assert(sizeof(SRNOS_RefereeRobotState) == 13, "SRNOS_RefereeRobotState must be 13 bytes");

// 0x0202
typedef struct __attribute__((packed)) {
  uint16_t reserved1;
  uint16_t reserved2;
  float     reserved3;
  uint16_t buffer_energy;
  uint16_t shooter_17mm_1_barrel_heat;
  uint16_t shooter_42mm_barrel_heat;
} SRNOS_RefereePowerHeatData;

// 0x0203
typedef struct __attribute__((packed)) {
  float x;
  float y;
  float angle;
} SRNOS_RefereeRobotPos;

// 0x0204
typedef struct __attribute__((packed)) {
  uint8_t  recovery_buff;
  uint16_t cooling_buff;
  uint8_t  defence_buff;
  uint8_t  vulnerability_buff;
  uint16_t attack_buff;
  uint8_t  remaining_energy;
} SRNOS_RefereeBuff;

// 0x0206
typedef struct __attribute__((packed)) {
  uint8_t armor_id : 4;
  uint8_t hurt_type : 4;
} SRNOS_RefereeRobotHurt;

// 0x0207
typedef struct __attribute__((packed)) {
  uint8_t bullet_type;
  uint8_t shooter_number;
  uint8_t launching_frequency;
  float    initial_speed;
} SRNOS_RefereeShootData;

// 0x0208
typedef struct __attribute__((packed)) {
  uint16_t projectile_allowance_17mm;
  uint16_t projectile_allowance_42mm;
  uint16_t remaining_gold_coin;
  uint16_t projectile_allowance_fortress;
} SRNOS_RefereeBulletRemaining;

// 0x0209
typedef struct __attribute__((packed)) {
  uint32_t rfid_status;
  uint8_t  rfid_status_2;
} SRNOS_RefereeRfidStatus;

// 0x020A
typedef struct __attribute__((packed)) {
  uint8_t  dart_launch_opening_status;
  uint8_t  reserved;
  uint16_t target_change_time;
  uint16_t latest_launch_cmd_time;
} SRNOS_RefereeDartClientCmd;



// 0x020B
typedef struct __attribute__((packed)) {
  float hero_x;
  float hero_y;
  float engineer_x;
  float engineer_y;
  float standard_3_x;
  float standard_3_y;
  float standard_4_x;
  float standard_4_y;
  float reserved1;
  float reserved2;
} SRNOS_RefereeGroundRobotPosition;

// 0x020C
typedef struct __attribute__((packed)) {
  uint16_t mark_progress;
} SRNOS_RefereeRadarMarkData;

// 0x020D
typedef struct __attribute__((packed)) {
  uint32_t sentry_info;
  uint16_t sentry_info_2;
} SRNOS_RefereeSentryInfo;

// 0x020E
typedef struct __attribute__((packed)) {
  uint8_t radar_info;
} SRNOS_RefereeRadarInfo;

// 0x0301 header
typedef struct __attribute__((packed)) {
  uint16_t data_cmd_id;
  uint16_t sender_id;
  uint16_t receiver_id;
} SRNOS_RefereeInteractiveHeader;


typedef struct {
  uint16_t data_cmd_id;
  uint16_t sender_id;
  uint16_t receiver_id;
  uint16_t user_data_len;
  uint8_t  user_data[112];
} SRNOS_RefereeInteractiveData;

// 0x0303
typedef struct __attribute__((packed)) {
  float     target_position_x;
  float     target_position_y;
  uint8_t  cmd_keyboard;
  uint8_t  target_robot_id;
  uint16_t cmd_source;
} SRNOS_RefereeMapCommand;

// 0x0305
typedef struct __attribute__((packed)) {
  uint16_t hero_position_x;
  uint16_t hero_position_y;
  uint16_t engineer_position_x;
  uint16_t engineer_position_y;
  uint16_t infantry_3_position_x;
  uint16_t infantry_3_position_y;
  uint16_t infantry_4_position_x;
  uint16_t infantry_4_position_y;
  uint16_t infantry_5_position_x;
  uint16_t infantry_5_position_y;
  uint16_t sentry_position_x;
  uint16_t sentry_position_y;
} SRNOS_RefereeRadarMapData;

// 0x0307
typedef struct __attribute__((packed)) {
  uint8_t  intention;
  uint16_t start_position_x;
  uint16_t start_position_y;
  int8_t   delta_x[49];
  int8_t   delta_y[49];
  uint16_t sender_id;
} SRNOS_RefereePathData;

// 0x0308
typedef struct __attribute__((packed)) {
  uint16_t sender_id;
  uint16_t receiver_id;
  uint8_t  user_data[30];
} SRNOS_RefereeRobotMapData;

// Snapshot-friendly container for 0x0301

// ---------------- Compile-time layout checks (RM2026 V1.0.0) ----------------
#if defined(__cplusplus)
  #define SRNOS_REFEREE_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
  #define SRNOS_REFEREE_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeGameStatus) == 11, "SRNOS_RefereeGameStatus size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeGameResult) == 1, "SRNOS_RefereeGameResult size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeGameRobotHp) == 16, "SRNOS_RefereeGameRobotHp size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeEventData) == 4, "SRNOS_RefereeEventData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRefereeWarning) == 3, "SRNOS_RefereeRefereeWarning size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeDartInfo) == 3, "SRNOS_RefereeDartInfo size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRobotState) == 13, "SRNOS_RefereeRobotState size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereePowerHeatData) == 14, "SRNOS_RefereePowerHeatData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRobotPos) == 12, "SRNOS_RefereeRobotPos size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeBuff) == 8, "SRNOS_RefereeBuff size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRobotHurt) == 1, "SRNOS_RefereeRobotHurt size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeShootData) == 7, "SRNOS_RefereeShootData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeBulletRemaining) == 8, "SRNOS_RefereeBulletRemaining size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRfidStatus) == 5, "SRNOS_RefereeRfidStatus size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeDartClientCmd) == 6, "SRNOS_RefereeDartClientCmd size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeGroundRobotPosition) == 40, "SRNOS_RefereeGroundRobotPosition size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRadarMarkData) == 2, "SRNOS_RefereeRadarMarkData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeSentryInfo) == 6, "SRNOS_RefereeSentryInfo size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRadarInfo) == 1, "SRNOS_RefereeRadarInfo size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeMapCommand) == 12, "SRNOS_RefereeMapCommand size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRadarMapData) == 24, "SRNOS_RefereeRadarMapData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereePathData) == 105, "SRNOS_RefereePathData size mismatch");
SRNOS_REFEREE_STATIC_ASSERT(sizeof(SRNOS_RefereeRobotMapData) == 34, "SRNOS_RefereeRobotMapData size mismatch");

#undef SRNOS_REFEREE_STATIC_ASSERT


typedef struct {
  SRNOS_RefereeGameStatus game_status;
  SRNOS_RefereeGameResult game_result;
  SRNOS_RefereeGameRobotHp game_robot_hp;
  SRNOS_RefereeEventData event_data;
  SRNOS_RefereeRefereeWarning SRNOS_RefereeWarning;
  SRNOS_RefereeDartInfo dart_info;

  SRNOS_RefereeRobotState robot_state;
  SRNOS_RefereePowerHeatData power_heat_data;
  SRNOS_RefereeRobotPos robot_pos;
  SRNOS_RefereeBuff buff;
  SRNOS_RefereeRobotHurt robot_hurt;
  SRNOS_RefereeShootData shoot_data;
  SRNOS_RefereeBulletRemaining bullet_remaining;
  SRNOS_RefereeRfidStatus rfid_status;
  SRNOS_RefereeDartClientCmd dart_client_cmd;
  SRNOS_RefereeGroundRobotPosition ground_robot_position;
  SRNOS_RefereeRadarMarkData radar_mark_data;
  SRNOS_RefereeSentryInfo sentry_info;
  SRNOS_RefereeRadarInfo radar_info;

  SRNOS_RefereeInteractiveData robot_interactive;
  SRNOS_RefereeMapCommand map_command;
  SRNOS_RefereeRadarMapData radar_map_data;
  SRNOS_RefereePathData path_data;
  SRNOS_RefereeRobotMapData robot_map_data;

  uint32_t last_rx_ms;
} SRNOS_RefereeSnapshot;

typedef enum {
  SRNOS_REFEREE_EVENT_TYPE_ROBOT_HURT = SRNOS_REFEREE_CMD_ROBOT_HURT,
  SRNOS_REFEREE_EVENT_TYPE_SHOOT_DATA = SRNOS_REFEREE_CMD_SHOOT_DATA
} SRNOS_RefereeEventType;

typedef struct {
  uint32_t seq;       // Monotonic event sequence, starts from 1.
  uint32_t rx_tick_ms;
  uint16_t cmd_id;    // SRNOS_REFEREE_CMD_ROBOT_HURT / SRNOS_REFEREE_CMD_SHOOT_DATA
  uint16_t reserved;
  union {
    SRNOS_RefereeRobotHurt robot_hurt;
    SRNOS_RefereeShootData shoot_data;
  } data;
} SRNOS_RefereeEvent;

// Debug-visible raw snapshot & parse stats (for Ozone / diagnostics).
extern volatile SRNOS_RefereeSnapshot SRNOS_DebugRefereeSnapshotRawDbg;
extern volatile uint32_t SRNOS_DebugRefereeRawLastRxMsDbg;
extern volatile uint16_t SRNOS_DebugRefereeLastCmdIdDbg;
extern volatile uint16_t SRNOS_DebugRefereeLastPayloadLenDbg;
extern volatile uint32_t SRNOS_DebugRefereeLastCRCOkMsDbg;
extern volatile uint32_t SRNOS_DebugRefereeCRCOkCntDbg;
extern volatile uint32_t SRNOS_DebugRefereeCRCFailCntDbg;
extern volatile uint32_t SRNOS_DebugRefereeLastRobotStateMsDbg;

bool SRNOS_RefereeBindChannel(SRNOS_SerialChannel *channel);
SRNOS_SerialChannel *SRNOS_RefereeGetBoundChannel(void);
bool SRNOS_RefereeInit(void);
bool SRNOS_RefereeReceiverReady(void);
void SRNOS_RefereePoll(void);

bool SRNOS_RefereeIsOnline(uint32_t timeout_ms);
uint32_t SRNOS_RefereeGetOverflowCnt(void);
uint32_t SRNOS_RefereeGetLastRxTickMs(void);

/* Feed is single-writer/task-only; do not call concurrently with the receiver worker. */
void SRNOS_RefereeFeed(const uint8_t *data,size_t size,uint32_t timestamp_ms);
void SRNOS_RefereeGetSnapshot(SRNOS_RefereeSnapshot *out);

// Event stream API for one-shot frames (0x0206/0x0207):
// - cursor is "last consumed seq".
// - To consume only future events, initialize cursor = SRNOS_RefereeEventCursorNow().
// - dropped returns skipped event count due to ring overwrite (can be NULL).
uint32_t SRNOS_RefereeEventCursorNow(void);
bool SRNOS_RefereeEventReadNext(uint32_t *cursor, SRNOS_RefereeEvent *out, uint32_t *dropped);

/* Nonblocking submission through the SDK-bound Serial channel; no separate TX init. */
bool SRNOS_RefereeTxSend(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_REFEREE_H
