#ifndef H723VG_V2_FREERTOS_LIB_RS485_LC_TL42R_H
#define H723VG_V2_FREERTOS_LIB_RS485_LC_TL42R_H

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/RS485/Modbus/lib_modbus_rtu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LC_TL42R_OK            = 0,
    LC_TL42R_ERR_PARAM     = -100,
    LC_TL42R_ERR_STATE     = -101,
    LC_TL42R_ERR_LENGTH    = -102,
    LC_TL42R_ERR_ACCESS    = -103,
    LC_TL42R_ERR_TYPE      = -104,
    LC_TL42R_ERR_RANGE     = -105,
    LC_TL42R_ERR_NOT_FOUND = -106
};

typedef enum {
    LC_TL42R_PARAM_TYPE_U16 = 0,
    LC_TL42R_PARAM_TYPE_I16 = 1,
    LC_TL42R_PARAM_TYPE_U32 = 2,
    LC_TL42R_PARAM_TYPE_I32 = 3
} lc_tl42r_param_type_t;

typedef enum {
    LC_TL42R_PARAM_GROUP_MONITOR = 0,
    LC_TL42R_PARAM_GROUP_DIO = 1,
    LC_TL42R_PARAM_GROUP_COMM = 2,
    LC_TL42R_PARAM_GROUP_MULTI_POSITION = 3,
    LC_TL42R_PARAM_GROUP_MULTI_SPEED = 4,
    LC_TL42R_PARAM_GROUP_FACTORY = 5
} lc_tl42r_param_group_t;

typedef enum {
    LC_TL42R_PARAM_PA_001 = 1u,
    LC_TL42R_PARAM_PA_002 = 2u,
    LC_TL42R_PARAM_PA_004 = 4u,
    LC_TL42R_PARAM_PA_005 = 5u,
    LC_TL42R_PARAM_PA_006 = 6u,
    LC_TL42R_PARAM_PA_007 = 7u,
    LC_TL42R_PARAM_PA_008 = 8u,
    LC_TL42R_PARAM_PA_009 = 9u,
    LC_TL42R_PARAM_PA_00A = 10u,

    LC_TL42R_PARAM_PA_010 = 16u,
    LC_TL42R_PARAM_PA_011 = 17u,
    LC_TL42R_PARAM_PA_012 = 18u,
    LC_TL42R_PARAM_PA_013 = 19u,
    LC_TL42R_PARAM_PA_014 = 20u,
    LC_TL42R_PARAM_PA_015 = 21u,
    LC_TL42R_PARAM_PA_01A = 26u,
    LC_TL42R_PARAM_PA_01B = 27u,
    LC_TL42R_PARAM_PA_01C = 28u,
    LC_TL42R_PARAM_PA_01D = 29u,
    LC_TL42R_PARAM_PA_01F = 31u,

    LC_TL42R_PARAM_PA_020 = 32u,
    LC_TL42R_PARAM_PA_021 = 33u,
    LC_TL42R_PARAM_PA_022 = 34u,
    LC_TL42R_PARAM_PA_023 = 35u,
    LC_TL42R_PARAM_PA_024 = 36u,
    LC_TL42R_PARAM_PA_025 = 37u,
    LC_TL42R_PARAM_PA_026 = 38u,
    LC_TL42R_PARAM_PA_028 = 40u,
    LC_TL42R_PARAM_PA_030 = 48u,
    LC_TL42R_PARAM_PA_031 = 49u,
    LC_TL42R_PARAM_PA_032 = 50u,
    LC_TL42R_PARAM_PA_033 = 51u,
    LC_TL42R_PARAM_PA_034 = 52u,
    LC_TL42R_PARAM_PA_035 = 53u,
    LC_TL42R_PARAM_PA_036 = 54u,
    LC_TL42R_PARAM_PA_037 = 55u,
    LC_TL42R_PARAM_PA_038 = 56u,
    LC_TL42R_PARAM_PA_040 = 64u,
    LC_TL42R_PARAM_PA_041 = 65u,
    LC_TL42R_PARAM_PA_042 = 66u,
    LC_TL42R_PARAM_PA_043 = 67u,
    LC_TL42R_PARAM_PA_044 = 68u,
    LC_TL42R_PARAM_PA_045 = 69u,
    LC_TL42R_PARAM_PA_046 = 70u,
    LC_TL42R_PARAM_PA_047 = 71u,
    LC_TL42R_PARAM_PA_048 = 72u,
    LC_TL42R_PARAM_PA_049 = 73u,
    LC_TL42R_PARAM_PA_04A = 74u,
    LC_TL42R_PARAM_PA_04E = 78u,
    LC_TL42R_PARAM_PA_04F = 79u,

    LC_TL42R_PARAM_PA_050 = 80u,
    LC_TL42R_PARAM_PA_051 = 81u,
    LC_TL42R_PARAM_PA_052 = 82u,
    LC_TL42R_PARAM_PA_053 = 83u,
    LC_TL42R_PARAM_PA_054 = 84u,
    LC_TL42R_PARAM_PA_056 = 86u,
    LC_TL42R_PARAM_PA_057 = 87u,
    LC_TL42R_PARAM_PA_058 = 88u,
    LC_TL42R_PARAM_PA_059 = 89u,
    LC_TL42R_PARAM_PA_05A = 90u,
    LC_TL42R_PARAM_PA_05C = 92u,
    LC_TL42R_PARAM_PA_05D = 93u,
    LC_TL42R_PARAM_PA_05E = 94u,
    LC_TL42R_PARAM_PA_05F = 95u,
    LC_TL42R_PARAM_PA_060 = 96u,
    LC_TL42R_PARAM_PA_062 = 98u,
    LC_TL42R_PARAM_PA_063 = 99u,
    LC_TL42R_PARAM_PA_064 = 100u,
    LC_TL42R_PARAM_PA_065 = 101u,
    LC_TL42R_PARAM_PA_066 = 102u,
    LC_TL42R_PARAM_PA_068 = 104u,
    LC_TL42R_PARAM_PA_069 = 105u,
    LC_TL42R_PARAM_PA_06A = 106u,
    LC_TL42R_PARAM_PA_06B = 107u,
    LC_TL42R_PARAM_PA_06C = 108u,
    LC_TL42R_PARAM_PA_06E = 110u,
    LC_TL42R_PARAM_PA_06F = 111u,
    LC_TL42R_PARAM_PA_070 = 112u,
    LC_TL42R_PARAM_PA_071 = 113u,
    LC_TL42R_PARAM_PA_072 = 114u,
    LC_TL42R_PARAM_PA_074 = 116u,
    LC_TL42R_PARAM_PA_075 = 117u,
    LC_TL42R_PARAM_PA_076 = 118u,
    LC_TL42R_PARAM_PA_077 = 119u,
    LC_TL42R_PARAM_PA_078 = 120u,
    LC_TL42R_PARAM_PA_07A = 122u,
    LC_TL42R_PARAM_PA_07B = 123u,
    LC_TL42R_PARAM_PA_07C = 124u,
    LC_TL42R_PARAM_PA_07D = 125u,
    LC_TL42R_PARAM_PA_07E = 126u,
    LC_TL42R_PARAM_PA_080 = 128u,
    LC_TL42R_PARAM_PA_081 = 129u,
    LC_TL42R_PARAM_PA_082 = 130u,
    LC_TL42R_PARAM_PA_083 = 131u,
    LC_TL42R_PARAM_PA_084 = 132u,
    LC_TL42R_PARAM_PA_086 = 134u,
    LC_TL42R_PARAM_PA_087 = 135u,
    LC_TL42R_PARAM_PA_088 = 136u,
    LC_TL42R_PARAM_PA_089 = 137u,
    LC_TL42R_PARAM_PA_08A = 138u,
    LC_TL42R_PARAM_PA_08C = 140u,
    LC_TL42R_PARAM_PA_08D = 141u,
    LC_TL42R_PARAM_PA_08E = 142u,
    LC_TL42R_PARAM_PA_08F = 143u,
    LC_TL42R_PARAM_PA_090 = 144u,
    LC_TL42R_PARAM_PA_092 = 146u,
    LC_TL42R_PARAM_PA_093 = 147u,
    LC_TL42R_PARAM_PA_094 = 148u,
    LC_TL42R_PARAM_PA_095 = 149u,
    LC_TL42R_PARAM_PA_096 = 150u,
    LC_TL42R_PARAM_PA_098 = 152u,
    LC_TL42R_PARAM_PA_099 = 153u,
    LC_TL42R_PARAM_PA_09A = 154u,
    LC_TL42R_PARAM_PA_09B = 155u,
    LC_TL42R_PARAM_PA_09C = 156u,
    LC_TL42R_PARAM_PA_09E = 158u,
    LC_TL42R_PARAM_PA_09F = 159u,
    LC_TL42R_PARAM_PA_0A0 = 160u,
    LC_TL42R_PARAM_PA_0A1 = 161u,
    LC_TL42R_PARAM_PA_0A2 = 162u,
    LC_TL42R_PARAM_PA_0A4 = 164u,
    LC_TL42R_PARAM_PA_0A5 = 165u,
    LC_TL42R_PARAM_PA_0A6 = 166u,
    LC_TL42R_PARAM_PA_0A7 = 167u,
    LC_TL42R_PARAM_PA_0A8 = 168u,
    LC_TL42R_PARAM_PA_0AA = 170u,
    LC_TL42R_PARAM_PA_0AB = 171u,
    LC_TL42R_PARAM_PA_0AC = 172u,
    LC_TL42R_PARAM_PA_0AD = 173u,
    LC_TL42R_PARAM_PA_0AE = 174u,

    LC_TL42R_PARAM_PA_0B0 = 176u,
    LC_TL42R_PARAM_PA_0B1 = 177u,
    LC_TL42R_PARAM_PA_0B2 = 178u,
    LC_TL42R_PARAM_PA_0B3 = 179u,
    LC_TL42R_PARAM_PA_0B4 = 180u,
    LC_TL42R_PARAM_PA_0B5 = 181u,
    LC_TL42R_PARAM_PA_0B6 = 182u,
    LC_TL42R_PARAM_PA_0B7 = 183u,
    LC_TL42R_PARAM_PA_0B8 = 184u,
    LC_TL42R_PARAM_PA_0B9 = 185u,
    LC_TL42R_PARAM_PA_0BA = 186u,
    LC_TL42R_PARAM_PA_0BB = 187u,
    LC_TL42R_PARAM_PA_0BC = 188u,
    LC_TL42R_PARAM_PA_0BD = 189u,
    LC_TL42R_PARAM_PA_0BE = 190u,
    LC_TL42R_PARAM_PA_0BF = 191u,
    LC_TL42R_PARAM_PA_0C0 = 192u,
    LC_TL42R_PARAM_PA_0C1 = 193u,
    LC_TL42R_PARAM_PA_0C2 = 194u,
    LC_TL42R_PARAM_PA_0C3 = 195u,
    LC_TL42R_PARAM_PA_0C4 = 196u,
    LC_TL42R_PARAM_PA_0C5 = 197u,
    LC_TL42R_PARAM_PA_0C6 = 198u,
    LC_TL42R_PARAM_PA_0C7 = 199u,
    LC_TL42R_PARAM_PA_0C8 = 200u,
    LC_TL42R_PARAM_PA_0C9 = 201u,
    LC_TL42R_PARAM_PA_0CA = 202u,
    LC_TL42R_PARAM_PA_0CB = 203u,
    LC_TL42R_PARAM_PA_0CC = 204u,
    LC_TL42R_PARAM_PA_0CD = 205u,
    LC_TL42R_PARAM_PA_0CE = 206u,
    LC_TL42R_PARAM_PA_0CF = 207u,
    LC_TL42R_PARAM_PA_0D0 = 208u,
    LC_TL42R_PARAM_PA_0D1 = 209u,
    LC_TL42R_PARAM_PA_0D2 = 210u,
    LC_TL42R_PARAM_PA_0D3 = 211u,
    LC_TL42R_PARAM_PA_0D4 = 212u,
    LC_TL42R_PARAM_PA_0D5 = 213u,
    LC_TL42R_PARAM_PA_0D6 = 214u,
    LC_TL42R_PARAM_PA_0D7 = 215u,
    LC_TL42R_PARAM_PA_0D8 = 216u,
    LC_TL42R_PARAM_PA_0D9 = 217u,
    LC_TL42R_PARAM_PA_0DA = 218u,
    LC_TL42R_PARAM_PA_0DB = 219u,
    LC_TL42R_PARAM_PA_0DC = 220u,
    LC_TL42R_PARAM_PA_0DD = 221u,
    LC_TL42R_PARAM_PA_0DE = 222u,
    LC_TL42R_PARAM_PA_0DF = 223u,

    LC_TL42R_PARAM_PA_100 = 256u,
    LC_TL42R_PARAM_PA_101 = 257u,
    LC_TL42R_PARAM_PA_102 = 258u,
    LC_TL42R_PARAM_PA_103 = 259u,
    LC_TL42R_PARAM_PA_104 = 260u,
    LC_TL42R_PARAM_PA_105 = 261u,
    LC_TL42R_PARAM_PA_106 = 262u,
    LC_TL42R_PARAM_PA_107 = 263u,
    LC_TL42R_PARAM_PA_109 = 265u,
    LC_TL42R_PARAM_PA_10A = 266u,
    LC_TL42R_PARAM_PA_10B = 267u,
    LC_TL42R_PARAM_PA_10C = 268u,
    LC_TL42R_PARAM_PA_10D = 269u,
    LC_TL42R_PARAM_PA_10E = 270u,
    LC_TL42R_PARAM_PA_10F = 271u,
    LC_TL42R_PARAM_PA_110 = 272u,
    LC_TL42R_PARAM_PA_111 = 273u,
    LC_TL42R_PARAM_PA_112 = 274u,
    LC_TL42R_PARAM_PA_113 = 275u,
    LC_TL42R_PARAM_PA_114 = 276u,
    LC_TL42R_PARAM_PA_115 = 277u,
    LC_TL42R_PARAM_PA_116 = 278u,
    LC_TL42R_PARAM_PA_117 = 279u,
    LC_TL42R_PARAM_PA_118 = 280u,
    LC_TL42R_PARAM_PA_119 = 281u
} lc_tl42r_param_id_t;

typedef lc_tl42r_param_id_t lc_tl42r_register_t;

enum {
    LC_TL42R_REG_SOFTWARE_VERSION = LC_TL42R_PARAM_PA_001,
    LC_TL42R_REG_HARDWARE_VERSION = LC_TL42R_PARAM_PA_002,
    LC_TL42R_REG_RUN_STATUS = LC_TL42R_PARAM_PA_004,
    LC_TL42R_REG_ALARM = LC_TL42R_PARAM_PA_005,
    LC_TL42R_REG_DI_STATE = LC_TL42R_PARAM_PA_006,
    LC_TL42R_REG_DO_STATE = LC_TL42R_PARAM_PA_007,
    LC_TL42R_REG_POSITION_H = LC_TL42R_PARAM_PA_008,
    LC_TL42R_REG_POSITION_L = LC_TL42R_PARAM_PA_009,
    LC_TL42R_REG_CURRENT_SPEED = LC_TL42R_PARAM_PA_00A,
    LC_TL42R_REG_SUBDIVISION = LC_TL42R_PARAM_PA_023,
    LC_TL42R_REG_POSITION_SPEED = LC_TL42R_PARAM_PA_036,
    LC_TL42R_REG_POSITION_TARGET_H = LC_TL42R_PARAM_PA_037,
    LC_TL42R_REG_POSITION_TARGET_L = LC_TL42R_PARAM_PA_038,
    LC_TL42R_REG_CONTROL_WORD = LC_TL42R_PARAM_PA_04E,
    LC_TL42R_REG_AUX_CONTROL = LC_TL42R_PARAM_PA_04F,
    LC_TL42R_REG_RUN_MODE = LC_TL42R_PARAM_PA_100,
    LC_TL42R_REG_ENCODER_RES = LC_TL42R_PARAM_PA_101
};

enum {
    LC_TL42R_MONITOR_REG_START = LC_TL42R_REG_RUN_STATUS,
    LC_TL42R_MONITOR_REG_COUNT = 7u,
    LC_TL42R_MULTI_POSITION_SEGMENT_COUNT = 16u,
    LC_TL42R_MULTI_SPEED_SEGMENT_COUNT = 16u
};

typedef enum {
    LC_TL42R_STATUS_IN_POSITION = (1u << 0),
    LC_TL42R_STATUS_HOMING_DONE = (1u << 1),
    LC_TL42R_STATUS_MOTOR_RUNNING = (1u << 2),
    LC_TL42R_STATUS_FAULT = (1u << 3),
    LC_TL42R_STATUS_ENABLED = (1u << 4),
    LC_TL42R_STATUS_POSITIVE_SOFT_LIM = (1u << 5),
    LC_TL42R_STATUS_NEGATIVE_SOFT_LIM = (1u << 6)
} lc_tl42r_run_status_bit_t;

typedef enum {
    LC_TL42R_CTRL_POSITION_TRIGGER = (1u << 0),
    LC_TL42R_CTRL_ABSOLUTE_MODE = (1u << 1),
    LC_TL42R_CTRL_INTERRUPT_MODE = (1u << 2),
    LC_TL42R_CTRL_JOG_TRIGGER = (1u << 3),
    LC_TL42R_CTRL_HOMING_TRIGGER = (1u << 4),
    LC_TL42R_CTRL_STOP = (1u << 5),
    LC_TL42R_CTRL_ESTOP = (1u << 6)
} lc_tl42r_control_word_bit_t;

typedef enum {
    LC_TL42R_ALARM_NONE = 0x00u,
    LC_TL42R_ALARM_OVERCURRENT = 0x01u,
    LC_TL42R_ALARM_OVERVOLTAGE = 0x02u,
    LC_TL42R_ALARM_UNDERVOLTAGE = 0x03u
} lc_tl42r_alarm_code_t;

typedef enum {
    LC_TL42R_DI_FUNC_NONE = 0x00u,
    LC_TL42R_DI_FUNC_HOME = 0x01u,
    LC_TL42R_DI_FUNC_POS_LIMIT = 0x02u,
    LC_TL42R_DI_FUNC_NEG_LIMIT = 0x03u,
    LC_TL42R_DI_FUNC_RELEASE = 0x04u,
    LC_TL42R_DI_FUNC_STOP = 0x05u,
    LC_TL42R_DI_FUNC_ESTOP = 0x06u,
    LC_TL42R_DI_FUNC_POS_JOG = 0x09u,
    LC_TL42R_DI_FUNC_NEG_JOG = 0x0Au,
    LC_TL42R_DI_FUNC_HOME_TRIGGER = 0x0Bu,
    LC_TL42R_DI_FUNC_POSITION_TRIGGER = 0x0Cu,
    LC_TL42R_DI_FUNC_SPEED_TRIGGER = 0x0Du,
    LC_TL42R_DI_FUNC_ADDR0 = 0x0Eu,
    LC_TL42R_DI_FUNC_ADDR1 = 0x0Fu,
    LC_TL42R_DI_FUNC_ADDR2 = 0x10u,
    LC_TL42R_DI_FUNC_ADDR3 = 0x11u
} lc_tl42r_di_function_t;

typedef enum {
    LC_TL42R_DO_FUNC_NONE = 0x00u,
    LC_TL42R_DO_FUNC_ALARM = 0x01u,
    LC_TL42R_DO_FUNC_RUNNING = 0x02u,
    LC_TL42R_DO_FUNC_HOMING_DONE = 0x03u,
    LC_TL42R_DO_FUNC_IN_POSITION = 0x04u,
    LC_TL42R_DO_FUNC_BRAKE = 0x05u
} lc_tl42r_do_function_t;

typedef enum {
    LC_TL42R_SERIAL_8N1 = 0u,
    LC_TL42R_SERIAL_8N2 = 1u,
    LC_TL42R_SERIAL_8E1 = 2u,
    LC_TL42R_SERIAL_8O1 = 3u
} lc_tl42r_serial_format_t;

typedef enum {
    LC_TL42R_BAUD_115200 = 0u,
    LC_TL42R_BAUD_38400 = 1u,
    LC_TL42R_BAUD_19200 = 2u,
    LC_TL42R_BAUD_9600 = 3u
} lc_tl42r_baudrate_t;

typedef enum {
    LC_TL42R_MULTI_POSITION_MODE_RELATIVE = 0u,
    LC_TL42R_MULTI_POSITION_MODE_ABSOLUTE = 1u
} lc_tl42r_multi_position_mode_t;

typedef enum {
    LC_TL42R_HOMING_NEGATIVE_APPROACH = 17u,
    LC_TL42R_HOMING_POSITIVE_LIMIT = 18u,
    LC_TL42R_HOMING_POSITIVE_HOME = 24u,
    LC_TL42R_HOMING_NEGATIVE_HOME = 29u,
    LC_TL42R_HOMING_SET_CURRENT_AS_ZERO = 35u
} lc_tl42r_homing_mode_t;

typedef enum {
    LC_TL42R_AUX_NONE = 0x0000u,
    LC_TL42R_AUX_RESTORE_FACTORY = 0x0100u,
    LC_TL42R_AUX_SAVE_CURRENT = 0x0200u,
    LC_TL42R_AUX_CLEAR_ALARM = 0x0300u,
    LC_TL42R_AUX_CLEAR_POSITION = 0x0400u,
    LC_TL42R_AUX_MOTOR_ENABLE = 0x0500u,
    LC_TL42R_AUX_MOTOR_RELEASE = 0x0600u
} lc_tl42r_aux_command_t;

typedef enum {
    LC_TL42R_RUN_MODE_OPEN_LOOP = 1u,
    LC_TL42R_RUN_MODE_CLOSED_LOOP = 2u
} lc_tl42r_run_mode_t;

typedef enum {
    LC_TL42R_CONFIG_CACHE_BASIC = (1u << 0),
    LC_TL42R_CONFIG_CACHE_MULTI_POSITION = (1u << 1),
    LC_TL42R_CONFIG_CACHE_MULTI_SPEED = (1u << 2),
    LC_TL42R_CONFIG_CACHE_ALL = (LC_TL42R_CONFIG_CACHE_BASIC |
                                 LC_TL42R_CONFIG_CACHE_MULTI_POSITION |
                                 LC_TL42R_CONFIG_CACHE_MULTI_SPEED)
} lc_tl42r_config_cache_mask_t;

typedef struct {
    lc_tl42r_param_id_t param;
    uint16_t address;
    lc_tl42r_param_type_t type;
    bool readable;
    bool writable;
    bool persistent;
    lc_tl42r_param_group_t group;
} lc_tl42r_param_descriptor_t;

typedef struct {
    bool in_position;
    bool homing_done;
    bool motor_running;
    bool fault;
    bool enabled;
    bool positive_soft_limit;
    bool negative_soft_limit;
} lc_tl42r_run_status_bits_t;

typedef struct {
    bool di0;
    bool di1;
    bool di2;
    bool di3;
    bool di4;
} lc_tl42r_di_state_bits_t;

typedef struct {
    bool do0;
    bool do1;
    bool do2;
} lc_tl42r_do_state_bits_t;

typedef struct {
    uint16_t raw_run_status;
    uint16_t raw_alarm;
    uint16_t raw_di_state;
    uint16_t raw_do_state;
    uint16_t raw_position_high;
    uint16_t raw_position_low;
    uint16_t raw_current_speed;
    int32_t current_position_pulse;
    int16_t current_speed_rpm;
    lc_tl42r_run_status_bits_t run_status;
    lc_tl42r_di_state_bits_t di_state;
    lc_tl42r_do_state_bits_t do_state;
} lc_tl42r_monitor_t;

typedef struct {
    uint16_t normally_closed_mask;
    lc_tl42r_di_function_t input_function[5];
    uint16_t filter_coefficient;
} lc_tl42r_di_config_t;

typedef struct {
    uint16_t normally_closed_mask;
    lc_tl42r_do_function_t output_function[2];
    uint16_t forced_output_mask;
} lc_tl42r_do_config_t;

typedef struct {
    uint16_t station_id;
    lc_tl42r_serial_format_t serial_format;
    uint16_t default_direction;
    uint16_t subdivision;
    uint16_t limit_stop_mode;
    uint16_t soft_limit_enable;
    uint16_t multi_position_trigger_mode;
    lc_tl42r_baudrate_t baudrate;
} lc_tl42r_comm_config_t;

typedef struct {
    int16_t jog_speed_rpm;
    uint16_t jog_accel_ms;
    uint16_t jog_decel_ms;
    uint16_t position_start_speed_rpm;
    uint16_t position_accel_ms;
    uint16_t position_decel_ms;
    uint16_t position_speed_rpm;
    int32_t position_target_pulse;
    lc_tl42r_multi_position_mode_t multi_position_mode;
    uint16_t control_word;
    uint16_t aux_control;
} lc_tl42r_motion_config_t;

typedef struct {
    lc_tl42r_homing_mode_t homing_mode;
    uint16_t homing_approach_speed_rpm;
    uint16_t homing_crawl_speed_rpm;
    uint16_t homing_accel_ms;
    int32_t homing_offset_pulse;
    int32_t positive_soft_limit_pulse;
    int32_t negative_soft_limit_pulse;
} lc_tl42r_homing_config_t;

typedef struct {
    lc_tl42r_run_mode_t run_mode;
    uint16_t encoder_resolution;
    uint16_t max_effective_current_ma;
    uint16_t closed_loop_max_current_ratio;
    uint16_t base_current_ratio;
    uint16_t open_loop_max_current_ratio;
    uint16_t locked_current_ratio;
    uint16_t locked_time;
    uint16_t lowpass_filter_coefficient;
    uint16_t deviation_threshold;
    uint16_t positioning_precision_threshold;
    uint16_t positioning_done_time;
    uint16_t mean_filter_coefficient;
    uint16_t current_loop_gain_adjust_ratio;
    uint16_t current_loop_kp;
    uint16_t current_loop_ki;
    uint16_t current_loop_kc;
    uint16_t la_speed_kp1;
    uint16_t la_speed_kv1;
    uint16_t speed_node1;
    uint16_t la_speed_kp2;
    uint16_t la_speed_kv2;
    uint16_t speed_node2;
    uint16_t speed_feed_forward;
    uint16_t position_integral;
} lc_tl42r_factory_params_t;

typedef struct {
    lc_tl42r_di_config_t di_config;
    lc_tl42r_do_config_t do_config;
    lc_tl42r_comm_config_t comm_config;
    lc_tl42r_motion_config_t motion_config;
    lc_tl42r_homing_config_t homing_config;
    lc_tl42r_factory_params_t factory_params;
} lc_tl42r_basic_config_t;

typedef struct {
    int32_t target_pulse;
    uint16_t speed_rpm;
    uint16_t accel_ms;
    uint16_t decel_ms;
} lc_tl42r_position_segment_t;

typedef struct {
    lc_tl42r_position_segment_t segments[LC_TL42R_MULTI_POSITION_SEGMENT_COUNT];
} lc_tl42r_multi_position_table_t;

typedef struct {
    int16_t speed_rpm;
    uint16_t accel_ms;
    uint16_t decel_ms;
} lc_tl42r_speed_segment_t;

typedef struct {
    lc_tl42r_speed_segment_t segments[LC_TL42R_MULTI_SPEED_SEGMENT_COUNT];
} lc_tl42r_multi_speed_table_t;

typedef struct {
    bool initialized;
    bool basic_valid;
    bool position_table_valid;
    bool speed_table_valid;
    uint32_t last_refresh_ms;
    lc_tl42r_basic_config_t basic;
    lc_tl42r_multi_position_table_t position_table;
    lc_tl42r_multi_speed_table_t speed_table;
} lc_tl42r_config_cache_t;

typedef struct {
    bool initialized;
    bool online;
    uint8_t port;
    uint8_t unit_id;
    uint16_t software_version;
    uint16_t hardware_version;
    uint16_t run_mode;
    uint16_t encoder_resolution;
    uint32_t last_update_ms;
    uint32_t ok_count;
    uint32_t error_count;
    int last_error;
    int32_t pulses_per_rev;
    uint32_t raw_position_u32;
    uint32_t last_raw_position_u32;
    int64_t tracked_position_pulse;
    int64_t zero_offset_pulse;
    int64_t relative_position_pulse;
    lc_tl42r_monitor_t monitor;
} lc_tl42r_watch_t;

extern volatile lc_tl42r_watch_t g_lc_tl42r_watch;
extern volatile lc_tl42r_config_cache_t g_lc_tl42r_config_cache;

typedef struct {
    modbus_rtu_client_t modbus_client;
    uint8_t port;
    uint8_t unit_id;
    int last_error;
    uint8_t last_exception_code;
} lc_tl42r_device_t;

void lc_tl42r_reset(lc_tl42r_device_t *dev);

int lc_tl42r_init(lc_tl42r_device_t *dev, uint8_t port, uint8_t unit_id);

int lc_tl42r_deinit(lc_tl42r_device_t *dev);

bool lc_tl42r_get_param_descriptor(lc_tl42r_param_id_t param,
                                   lc_tl42r_param_descriptor_t *out_descriptor);

int lc_tl42r_read_u16(lc_tl42r_device_t *dev,
                      lc_tl42r_param_id_t param,
                      uint16_t *out_value,
                      uint32_t timeout_ms);

int lc_tl42r_read_i16(lc_tl42r_device_t *dev,
                      lc_tl42r_param_id_t param,
                      int16_t *out_value,
                      uint32_t timeout_ms);

int lc_tl42r_read_u32(lc_tl42r_device_t *dev,
                      lc_tl42r_param_id_t param,
                      uint32_t *out_value,
                      uint32_t timeout_ms);

int lc_tl42r_read_i32(lc_tl42r_device_t *dev,
                      lc_tl42r_param_id_t param,
                      int32_t *out_value,
                      uint32_t timeout_ms);

int lc_tl42r_write_u16(lc_tl42r_device_t *dev,
                       lc_tl42r_param_id_t param,
                       uint16_t value,
                       uint32_t timeout_ms);

int lc_tl42r_write_i16(lc_tl42r_device_t *dev,
                       lc_tl42r_param_id_t param,
                       int16_t value,
                       uint32_t timeout_ms);

int lc_tl42r_write_u32(lc_tl42r_device_t *dev,
                       lc_tl42r_param_id_t param,
                       uint32_t value,
                       uint32_t timeout_ms);

int lc_tl42r_write_i32(lc_tl42r_device_t *dev,
                       lc_tl42r_param_id_t param,
                       int32_t value,
                       uint32_t timeout_ms);

int lc_tl42r_save_parameters(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_execute_aux_command(lc_tl42r_device_t *dev,
                                 lc_tl42r_aux_command_t command,
                                 uint32_t timeout_ms);

int lc_tl42r_read_versions(lc_tl42r_device_t *dev,
                           uint16_t *out_software_version,
                           uint16_t *out_hardware_version,
                           uint32_t timeout_ms);

int lc_tl42r_read_run_status(lc_tl42r_device_t *dev,
                             uint16_t *out_status,
                             uint32_t timeout_ms);

int lc_tl42r_read_alarm(lc_tl42r_device_t *dev,
                        uint16_t *out_alarm,
                        uint32_t timeout_ms);

int lc_tl42r_read_status_and_alarm(lc_tl42r_device_t *dev,
                                   uint16_t *out_status,
                                   uint16_t *out_alarm,
                                   uint32_t timeout_ms);

int lc_tl42r_read_digital_inputs(lc_tl42r_device_t *dev,
                                 uint16_t *out_di_state,
                                 uint32_t timeout_ms);

int lc_tl42r_read_digital_outputs(lc_tl42r_device_t *dev,
                                  uint16_t *out_do_state,
                                  uint32_t timeout_ms);

int lc_tl42r_read_current_position(lc_tl42r_device_t *dev,
                                   int32_t *out_position_pulse,
                                   uint32_t timeout_ms);

int lc_tl42r_read_current_speed(lc_tl42r_device_t *dev,
                                int16_t *out_speed_rpm,
                                uint32_t timeout_ms);

int lc_tl42r_read_subdivision(lc_tl42r_device_t *dev,
                              uint16_t *out_subdivision,
                              uint32_t timeout_ms);

int lc_tl42r_read_monitor(lc_tl42r_device_t *dev,
                          lc_tl42r_monitor_t *out_monitor,
                          uint32_t timeout_ms);

int lc_tl42r_read_drive_info(lc_tl42r_device_t *dev,
                             uint16_t *out_run_mode,
                             uint16_t *out_encoder_resolution,
                             uint32_t timeout_ms);

int lc_tl42r_read_factory_params(lc_tl42r_device_t *dev,
                                 lc_tl42r_factory_params_t *out_factory_params,
                                 uint32_t timeout_ms);

int lc_tl42r_read_basic_config(lc_tl42r_device_t *dev,
                               lc_tl42r_basic_config_t *out_basic_config,
                               uint32_t timeout_ms);

int lc_tl42r_set_di_config(lc_tl42r_device_t *dev,
                           const lc_tl42r_di_config_t *config,
                           uint32_t timeout_ms);

int lc_tl42r_set_do_config(lc_tl42r_device_t *dev,
                           const lc_tl42r_do_config_t *config,
                           uint32_t timeout_ms);

int lc_tl42r_set_communication_config(lc_tl42r_device_t *dev,
                                      const lc_tl42r_comm_config_t *config,
                                      uint32_t timeout_ms);

int lc_tl42r_set_motion_config(lc_tl42r_device_t *dev,
                               const lc_tl42r_motion_config_t *config,
                               uint32_t timeout_ms);

int lc_tl42r_set_homing_config(lc_tl42r_device_t *dev,
                               const lc_tl42r_homing_config_t *config,
                               uint32_t timeout_ms);

int lc_tl42r_read_multi_position_segment(lc_tl42r_device_t *dev,
                                         uint8_t segment_index,
                                         lc_tl42r_position_segment_t *out_segment,
                                         uint32_t timeout_ms);

int lc_tl42r_write_multi_position_segment(lc_tl42r_device_t *dev,
                                          uint8_t segment_index,
                                          const lc_tl42r_position_segment_t *segment,
                                          uint32_t timeout_ms);

int lc_tl42r_read_multi_position_table(lc_tl42r_device_t *dev,
                                       lc_tl42r_multi_position_table_t *out_table,
                                       uint32_t timeout_ms);

int lc_tl42r_write_multi_position_table(lc_tl42r_device_t *dev,
                                        const lc_tl42r_multi_position_table_t *table,
                                        uint32_t timeout_ms);

int lc_tl42r_read_multi_speed_segment(lc_tl42r_device_t *dev,
                                      uint8_t segment_index,
                                      lc_tl42r_speed_segment_t *out_segment,
                                      uint32_t timeout_ms);

int lc_tl42r_write_multi_speed_segment(lc_tl42r_device_t *dev,
                                       uint8_t segment_index,
                                       const lc_tl42r_speed_segment_t *segment,
                                       uint32_t timeout_ms);

int lc_tl42r_read_multi_speed_table(lc_tl42r_device_t *dev,
                                    lc_tl42r_multi_speed_table_t *out_table,
                                    uint32_t timeout_ms);

int lc_tl42r_write_multi_speed_table(lc_tl42r_device_t *dev,
                                     const lc_tl42r_multi_speed_table_t *table,
                                     uint32_t timeout_ms);

int lc_tl42r_set_position_speed(lc_tl42r_device_t *dev,
                                uint16_t speed_rpm,
                                uint32_t timeout_ms);

int lc_tl42r_set_position_target(lc_tl42r_device_t *dev,
                                 int32_t target_pulse,
                                 uint32_t timeout_ms);

int lc_tl42r_write_control_word(lc_tl42r_device_t *dev,
                                uint16_t control_word,
                                uint32_t timeout_ms);

int lc_tl42r_start_relative_move(lc_tl42r_device_t *dev,
                                 int32_t target_pulse,
                                 uint16_t speed_rpm,
                                 bool interrupt_current,
                                 uint32_t timeout_ms);

int lc_tl42r_start_absolute_move(lc_tl42r_device_t *dev,
                                 int32_t target_pulse,
                                 uint16_t speed_rpm,
                                 bool interrupt_current,
                                 uint32_t timeout_ms);

int lc_tl42r_start_jog(lc_tl42r_device_t *dev,
                       int16_t speed_rpm,
                       uint16_t accel_ms,
                       uint16_t decel_ms,
                       uint32_t timeout_ms);

int lc_tl42r_start_homing(lc_tl42r_device_t *dev,
                          lc_tl42r_homing_mode_t homing_mode,
                          uint16_t approach_speed_rpm,
                          uint16_t crawl_speed_rpm,
                          uint16_t accel_ms,
                          int32_t offset_pulse,
                          uint32_t timeout_ms);

int lc_tl42r_stop(lc_tl42r_device_t *dev,
                  bool emergency_stop,
                  uint32_t timeout_ms);

int lc_tl42r_estop(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_restore_factory_defaults(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_clear_alarm(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_clear_position(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_motor_enable(lc_tl42r_device_t *dev, uint32_t timeout_ms);

int lc_tl42r_motor_release(lc_tl42r_device_t *dev, uint32_t timeout_ms);

void lc_tl42r_watch_reset(uint8_t port, uint8_t unit_id, int32_t pulses_per_rev);

void lc_tl42r_watch_set_versions(uint16_t software_version, uint16_t hardware_version);

void lc_tl42r_watch_set_drive_info(uint16_t run_mode, uint16_t encoder_resolution);

void lc_tl42r_watch_set_pulses_per_rev(int32_t pulses_per_rev);

void lc_tl42r_watch_set_zero_current(void);

void lc_tl42r_watch_mark_error(int error_code, uint32_t now_ms);

void lc_tl42r_watch_update(const lc_tl42r_monitor_t *monitor, uint32_t now_ms);

bool lc_tl42r_get_watch(lc_tl42r_watch_t *out_watch);

void lc_tl42r_config_cache_reset(void);

int lc_tl42r_refresh_config_cache(lc_tl42r_device_t *dev,
                                  uint32_t refresh_mask,
                                  uint32_t timeout_ms);

bool lc_tl42r_get_config_cache(lc_tl42r_config_cache_t *out_cache);

void lc_tl42r_decode_run_status(uint16_t raw_status, lc_tl42r_run_status_bits_t *out_status);

void lc_tl42r_decode_di_state(uint16_t raw_di_state, lc_tl42r_di_state_bits_t *out_state);

void lc_tl42r_decode_do_state(uint16_t raw_do_state, lc_tl42r_do_state_bits_t *out_state);

int32_t lc_tl42r_compose_position(uint16_t position_high, uint16_t position_low);

int16_t lc_tl42r_decode_speed(uint16_t raw_speed);

const char *lc_tl42r_alarm_to_string(uint16_t alarm_code);

const char *lc_tl42r_run_mode_to_string(uint16_t run_mode);

const char *lc_tl42r_error_to_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_LIB_RS485_LC_TL42R_H
