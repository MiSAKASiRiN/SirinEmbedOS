#ifndef H723VG_V2_FREERTOS_REFEREE_H
#define H723VG_V2_FREERTOS_REFEREE_H

#include <stdbool.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/Serial/Serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef REFEREE_EVENT_RING_SIZE
#define REFEREE_EVENT_RING_SIZE 64u
#endif

#define REFEREE_SOF 0xA5u

typedef struct __attribute__((packed)) {
  uint8_t  sof;
  uint16_t data_length;
  uint8_t  seq;
  uint8_t  crc8;
} frame_header_t;

typedef enum {
  REFEREE_CMD_GAME_STATUS               = 0x0001,
  REFEREE_CMD_GAME_RESULT               = 0x0002,
  REFEREE_CMD_GAME_ROBOT_HP             = 0x0003,

  REFEREE_CMD_EVENT_DATA                = 0x0101,
  REFEREE_CMD_REFEREE_WARNING           = 0x0104,
  REFEREE_CMD_DART_INFO                 = 0x0105,

  REFEREE_CMD_ROBOT_STATE               = 0x0201,
  REFEREE_CMD_POWER_HEAT_DATA           = 0x0202,
  REFEREE_CMD_ROBOT_POS                 = 0x0203,
  REFEREE_CMD_BUFF                      = 0x0204,
  REFEREE_CMD_ROBOT_HURT                = 0x0206,
  REFEREE_CMD_SHOOT_DATA                = 0x0207,
  REFEREE_CMD_BULLET_REMAINING          = 0x0208,
  REFEREE_CMD_RFID_STATUS               = 0x0209,
  REFEREE_CMD_DART_CLIENT_CMD           = 0x020A,
  REFEREE_CMD_GROUND_ROBOT_POSITION     = 0x020B,
  REFEREE_CMD_RADAR_MARK_DATA           = 0x020C,
  REFEREE_CMD_SENTRY_INFO               = 0x020D,
  REFEREE_CMD_RADAR_INFO                = 0x020E,

  REFEREE_CMD_ROBOT_INTERACTIVE_DATA    = 0x0301,
  REFEREE_CMD_MAP_COMMAND               = 0x0303,
  REFEREE_CMD_RADAR_MAP_DATA            = 0x0305,
  REFEREE_CMD_PATH_DATA                 = 0x0307,
  REFEREE_CMD_ROBOT_MAP_DATA            = 0x0308
} referee_cmd_id_t;

// ---------------- Section A: protocol packed structs ----------------
// 0x0001
typedef struct __attribute__((packed)) {
  uint8_t  game_type     : 4;
  uint8_t  game_progress : 4;
  uint16_t stage_remain_time;
  uint64_t SyncTimeStamp;
} referee_game_status_t;

// 0x0002
typedef struct __attribute__((packed)) {
  uint8_t winner;
} referee_game_result_t;

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
} referee_game_robot_hp_t;

// 0x0101
typedef struct __attribute__((packed)) {
  uint32_t event_data;
} referee_event_data_t;

// 0x0104
typedef struct __attribute__((packed)) {
  uint8_t level;
  uint8_t offending_robot_id;
  uint8_t count;
} referee_referee_warning_t;

// 0x0105
typedef struct __attribute__((packed)) {
  uint8_t  dart_remaining_time;
  uint16_t dart_info;
} referee_dart_info_t;

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
  } referee_robot_state_t;

  // _Static_assert(sizeof(referee_robot_state_t) == 13, "referee_robot_state_t must be 13 bytes");

// 0x0202
typedef struct __attribute__((packed)) {
  uint16_t reserved1;
  uint16_t reserved2;
  float     reserved3;
  uint16_t buffer_energy;
  uint16_t shooter_17mm_1_barrel_heat;
  uint16_t shooter_42mm_barrel_heat;
} referee_power_heat_data_t;

// 0x0203
typedef struct __attribute__((packed)) {
  float x;
  float y;
  float angle;
} referee_robot_pos_t;

// 0x0204
typedef struct __attribute__((packed)) {
  uint8_t  recovery_buff;
  uint16_t cooling_buff;
  uint8_t  defence_buff;
  uint8_t  vulnerability_buff;
  uint16_t attack_buff;
  uint8_t  remaining_energy;
} referee_buff_t;

// 0x0206
typedef struct __attribute__((packed)) {
  uint8_t armor_id : 4;
  uint8_t hurt_type : 4;
} referee_robot_hurt_t;

// 0x0207
typedef struct __attribute__((packed)) {
  uint8_t bullet_type;
  uint8_t shooter_number;
  uint8_t launching_frequency;
  float    initial_speed;
} referee_shoot_data_t;

// 0x0208
typedef struct __attribute__((packed)) {
  uint16_t projectile_allowance_17mm;
  uint16_t projectile_allowance_42mm;
  uint16_t remaining_gold_coin;
  uint16_t projectile_allowance_fortress;
} referee_bullet_remaining_t;

// 0x0209
typedef struct __attribute__((packed)) {
  uint32_t rfid_status;
  uint8_t  rfid_status_2;
} referee_rfid_status_t;

// 0x020A
typedef struct __attribute__((packed)) {
  uint8_t  dart_launch_opening_status;
  uint8_t  reserved;
  uint16_t target_change_time;
  uint16_t latest_launch_cmd_time;
} referee_dart_client_cmd_t;

// Compatibility alias: older code may refer to dart_attack_target.
#define dart_attack_target reserved


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
} referee_ground_robot_position_t;

// 0x020C
typedef struct __attribute__((packed)) {
  uint16_t mark_progress;
} referee_radar_mark_data_t;

// 0x020D
typedef struct __attribute__((packed)) {
  uint32_t sentry_info;
  uint16_t sentry_info_2;
} referee_sentry_info_t;

// 0x020E
typedef struct __attribute__((packed)) {
  uint8_t radar_info;
} referee_radar_info_t;

// 0x0301 header
typedef struct __attribute__((packed)) {
  uint16_t data_cmd_id;
  uint16_t sender_id;
  uint16_t receiver_id;
} referee_interactive_header_t;


typedef struct {
  uint16_t data_cmd_id;
  uint16_t sender_id;
  uint16_t receiver_id;
  uint16_t user_data_len;
  uint8_t  user_data[112];
} referee_interactive_data_t;

// 0x0303
typedef struct __attribute__((packed)) {
  float     target_position_x;
  float     target_position_y;
  uint8_t  cmd_keyboard;
  uint8_t  target_robot_id;
  uint16_t cmd_source;
} referee_map_command_t;

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
} referee_radar_map_data_t;

// 0x0307
typedef struct __attribute__((packed)) {
  uint8_t  intention;
  uint16_t start_position_x;
  uint16_t start_position_y;
  int8_t   delta_x[49];
  int8_t   delta_y[49];
  uint16_t sender_id;
} referee_path_data_t;

// 0x0308
typedef struct __attribute__((packed)) {
  uint16_t sender_id;
  uint16_t receiver_id;
  uint8_t  user_data[30];
} referee_robot_map_data_t;

// Snapshot-friendly container for 0x0301

// ---------------- Compile-time layout checks (RM2026 V1.0.0) ----------------
#if defined(__cplusplus)
  #define REFEREE_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
  #define REFEREE_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

REFEREE_STATIC_ASSERT(sizeof(referee_game_status_t) == 11, "referee_game_status_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_game_result_t) == 1, "referee_game_result_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_game_robot_hp_t) == 16, "referee_game_robot_hp_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_event_data_t) == 4, "referee_event_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_referee_warning_t) == 3, "referee_referee_warning_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_dart_info_t) == 3, "referee_dart_info_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_robot_state_t) == 13, "referee_robot_state_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_power_heat_data_t) == 14, "referee_power_heat_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_robot_pos_t) == 12, "referee_robot_pos_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_buff_t) == 8, "referee_buff_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_robot_hurt_t) == 1, "referee_robot_hurt_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_shoot_data_t) == 7, "referee_shoot_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_bullet_remaining_t) == 8, "referee_bullet_remaining_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_rfid_status_t) == 5, "referee_rfid_status_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_dart_client_cmd_t) == 6, "referee_dart_client_cmd_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_ground_robot_position_t) == 40, "referee_ground_robot_position_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_radar_mark_data_t) == 2, "referee_radar_mark_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_sentry_info_t) == 6, "referee_sentry_info_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_radar_info_t) == 1, "referee_radar_info_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_map_command_t) == 12, "referee_map_command_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_radar_map_data_t) == 24, "referee_radar_map_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_path_data_t) == 105, "referee_path_data_t size mismatch");
REFEREE_STATIC_ASSERT(sizeof(referee_robot_map_data_t) == 34, "referee_robot_map_data_t size mismatch");

#undef REFEREE_STATIC_ASSERT


typedef struct {
  referee_game_status_t game_status;
  referee_game_result_t game_result;
  referee_game_robot_hp_t game_robot_hp;
  referee_event_data_t event_data;
  referee_referee_warning_t referee_warning;
  referee_dart_info_t dart_info;

  referee_robot_state_t robot_state;
  referee_power_heat_data_t power_heat_data;
  referee_robot_pos_t robot_pos;
  referee_buff_t buff;
  referee_robot_hurt_t robot_hurt;
  referee_shoot_data_t shoot_data;
  referee_bullet_remaining_t bullet_remaining;
  referee_rfid_status_t rfid_status;
  referee_dart_client_cmd_t dart_client_cmd;
  referee_ground_robot_position_t ground_robot_position;
  referee_radar_mark_data_t radar_mark_data;
  referee_sentry_info_t sentry_info;
  referee_radar_info_t radar_info;

  referee_interactive_data_t robot_interactive;
  referee_map_command_t map_command;
  referee_radar_map_data_t radar_map_data;
  referee_path_data_t path_data;
  referee_robot_map_data_t robot_map_data;

  uint32_t last_rx_ms;
} referee_snapshot_t;

typedef enum {
  REFEREE_EVENT_TYPE_ROBOT_HURT = REFEREE_CMD_ROBOT_HURT,
  REFEREE_EVENT_TYPE_SHOOT_DATA = REFEREE_CMD_SHOOT_DATA
} referee_event_type_t;

typedef struct {
  uint32_t seq;       // Monotonic event sequence, starts from 1.
  uint32_t rx_tick_ms;
  uint16_t cmd_id;    // REFEREE_CMD_ROBOT_HURT / REFEREE_CMD_SHOOT_DATA
  uint16_t reserved;
  union {
    referee_robot_hurt_t robot_hurt;
    referee_shoot_data_t shoot_data;
  } data;
} referee_event_t;

// Debug-visible raw snapshot & parse stats (for Ozone / diagnostics).
extern volatile referee_snapshot_t g_referee_snapshot_raw_dbg;
extern volatile uint32_t g_referee_raw_last_rx_ms_dbg;
extern volatile uint16_t g_referee_last_cmd_id_dbg;
extern volatile uint16_t g_referee_last_payload_len_dbg;
extern volatile uint32_t g_referee_last_crc_ok_ms_dbg;
extern volatile uint32_t g_referee_crc_ok_cnt_dbg;
extern volatile uint32_t g_referee_crc_fail_cnt_dbg;
extern volatile uint32_t g_referee_last_robot_state_ms_dbg;

bool Referee_BindChannel(SRNOS_SerialChannel *channel);
SRNOS_SerialChannel *Referee_GetBoundChannel(void);
bool Referee_Init(void);
bool Referee_Start(void);
void Referee_Poll(void);

bool Referee_IsOnline(uint32_t timeout_ms);
uint32_t Referee_GetOverflowCnt(void);
uint32_t Referee_GetLastRxTickMs(void);

/* Feed is single-writer/task-only; do not call concurrently with the receiver worker. */
void Referee_Feed(const uint8_t *data,size_t size,uint32_t timestamp_ms);
void Referee_GetSnapshot(referee_snapshot_t *out);

// Event stream API for one-shot frames (0x0206/0x0207):
// - cursor is "last consumed seq".
// - To consume only future events, initialize cursor = Referee_EventCursorNow().
// - dropped returns skipped event count due to ring overwrite (can be NULL).
uint32_t Referee_EventCursorNow(void);
bool Referee_EventReadNext(uint32_t *cursor, referee_event_t *out, uint32_t *dropped);

// Reserved for future TX path on the caller-bound UART.
void Referee_TxInit(void);
bool Referee_TxSend(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_REFEREE_H
