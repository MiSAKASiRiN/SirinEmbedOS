#ifndef SRNOS_RS485_LC_TL42R_H
#define SRNOS_RS485_LC_TL42R_H

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "SirinEmbedOS/IO/RS485/Modbus/ModbusRTU.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SRNOS_TL42R_OK            = 0,
    SRNOS_TL42R_ERR_PARAM     = -100,
    SRNOS_TL42R_ERR_STATE     = -101,
    SRNOS_TL42R_ERR_LENGTH    = -102,
    SRNOS_TL42R_ERR_ACCESS    = -103,
    SRNOS_TL42R_ERR_TYPE      = -104,
    SRNOS_TL42R_ERR_RANGE     = -105,
    SRNOS_TL42R_ERR_NOT_FOUND = -106
};

typedef enum {
    SRNOS_TL42R_PARAM_TYPE_U16 = 0,
    SRNOS_TL42R_PARAM_TYPE_I16 = 1,
    SRNOS_TL42R_PARAM_TYPE_U32 = 2,
    SRNOS_TL42R_PARAM_TYPE_I32 = 3
} SRNOS_TL42RParamType;

typedef enum {
    SRNOS_TL42R_PARAM_GROUP_MONITOR = 0,
    SRNOS_TL42R_PARAM_GROUP_DIO = 1,
    SRNOS_TL42R_PARAM_GROUP_COMM = 2,
    SRNOS_TL42R_PARAM_GROUP_MULTI_POSITION = 3,
    SRNOS_TL42R_PARAM_GROUP_MULTI_SPEED = 4,
    SRNOS_TL42R_PARAM_GROUP_FACTORY = 5
} SRNOS_TL42RParamGroup;

typedef enum {
    SRNOS_TL42R_PARAM_PA_001 = 1u,
    SRNOS_TL42R_PARAM_PA_002 = 2u,
    SRNOS_TL42R_PARAM_PA_004 = 4u,
    SRNOS_TL42R_PARAM_PA_005 = 5u,
    SRNOS_TL42R_PARAM_PA_006 = 6u,
    SRNOS_TL42R_PARAM_PA_007 = 7u,
    SRNOS_TL42R_PARAM_PA_008 = 8u,
    SRNOS_TL42R_PARAM_PA_009 = 9u,
    SRNOS_TL42R_PARAM_PA_00A = 10u,

    SRNOS_TL42R_PARAM_PA_010 = 16u,
    SRNOS_TL42R_PARAM_PA_011 = 17u,
    SRNOS_TL42R_PARAM_PA_012 = 18u,
    SRNOS_TL42R_PARAM_PA_013 = 19u,
    SRNOS_TL42R_PARAM_PA_014 = 20u,
    SRNOS_TL42R_PARAM_PA_015 = 21u,
    SRNOS_TL42R_PARAM_PA_01A = 26u,
    SRNOS_TL42R_PARAM_PA_01B = 27u,
    SRNOS_TL42R_PARAM_PA_01C = 28u,
    SRNOS_TL42R_PARAM_PA_01D = 29u,
    SRNOS_TL42R_PARAM_PA_01F = 31u,

    SRNOS_TL42R_PARAM_PA_020 = 32u,
    SRNOS_TL42R_PARAM_PA_021 = 33u,
    SRNOS_TL42R_PARAM_PA_022 = 34u,
    SRNOS_TL42R_PARAM_PA_023 = 35u,
    SRNOS_TL42R_PARAM_PA_024 = 36u,
    SRNOS_TL42R_PARAM_PA_025 = 37u,
    SRNOS_TL42R_PARAM_PA_026 = 38u,
    SRNOS_TL42R_PARAM_PA_028 = 40u,
    SRNOS_TL42R_PARAM_PA_030 = 48u,
    SRNOS_TL42R_PARAM_PA_031 = 49u,
    SRNOS_TL42R_PARAM_PA_032 = 50u,
    SRNOS_TL42R_PARAM_PA_033 = 51u,
    SRNOS_TL42R_PARAM_PA_034 = 52u,
    SRNOS_TL42R_PARAM_PA_035 = 53u,
    SRNOS_TL42R_PARAM_PA_036 = 54u,
    SRNOS_TL42R_PARAM_PA_037 = 55u,
    SRNOS_TL42R_PARAM_PA_038 = 56u,
    SRNOS_TL42R_PARAM_PA_040 = 64u,
    SRNOS_TL42R_PARAM_PA_041 = 65u,
    SRNOS_TL42R_PARAM_PA_042 = 66u,
    SRNOS_TL42R_PARAM_PA_043 = 67u,
    SRNOS_TL42R_PARAM_PA_044 = 68u,
    SRNOS_TL42R_PARAM_PA_045 = 69u,
    SRNOS_TL42R_PARAM_PA_046 = 70u,
    SRNOS_TL42R_PARAM_PA_047 = 71u,
    SRNOS_TL42R_PARAM_PA_048 = 72u,
    SRNOS_TL42R_PARAM_PA_049 = 73u,
    SRNOS_TL42R_PARAM_PA_04A = 74u,
    SRNOS_TL42R_PARAM_PA_04E = 78u,
    SRNOS_TL42R_PARAM_PA_04F = 79u,

    SRNOS_TL42R_PARAM_PA_050 = 80u,
    SRNOS_TL42R_PARAM_PA_051 = 81u,
    SRNOS_TL42R_PARAM_PA_052 = 82u,
    SRNOS_TL42R_PARAM_PA_053 = 83u,
    SRNOS_TL42R_PARAM_PA_054 = 84u,
    SRNOS_TL42R_PARAM_PA_056 = 86u,
    SRNOS_TL42R_PARAM_PA_057 = 87u,
    SRNOS_TL42R_PARAM_PA_058 = 88u,
    SRNOS_TL42R_PARAM_PA_059 = 89u,
    SRNOS_TL42R_PARAM_PA_05A = 90u,
    SRNOS_TL42R_PARAM_PA_05C = 92u,
    SRNOS_TL42R_PARAM_PA_05D = 93u,
    SRNOS_TL42R_PARAM_PA_05E = 94u,
    SRNOS_TL42R_PARAM_PA_05F = 95u,
    SRNOS_TL42R_PARAM_PA_060 = 96u,
    SRNOS_TL42R_PARAM_PA_062 = 98u,
    SRNOS_TL42R_PARAM_PA_063 = 99u,
    SRNOS_TL42R_PARAM_PA_064 = 100u,
    SRNOS_TL42R_PARAM_PA_065 = 101u,
    SRNOS_TL42R_PARAM_PA_066 = 102u,
    SRNOS_TL42R_PARAM_PA_068 = 104u,
    SRNOS_TL42R_PARAM_PA_069 = 105u,
    SRNOS_TL42R_PARAM_PA_06A = 106u,
    SRNOS_TL42R_PARAM_PA_06B = 107u,
    SRNOS_TL42R_PARAM_PA_06C = 108u,
    SRNOS_TL42R_PARAM_PA_06E = 110u,
    SRNOS_TL42R_PARAM_PA_06F = 111u,
    SRNOS_TL42R_PARAM_PA_070 = 112u,
    SRNOS_TL42R_PARAM_PA_071 = 113u,
    SRNOS_TL42R_PARAM_PA_072 = 114u,
    SRNOS_TL42R_PARAM_PA_074 = 116u,
    SRNOS_TL42R_PARAM_PA_075 = 117u,
    SRNOS_TL42R_PARAM_PA_076 = 118u,
    SRNOS_TL42R_PARAM_PA_077 = 119u,
    SRNOS_TL42R_PARAM_PA_078 = 120u,
    SRNOS_TL42R_PARAM_PA_07A = 122u,
    SRNOS_TL42R_PARAM_PA_07B = 123u,
    SRNOS_TL42R_PARAM_PA_07C = 124u,
    SRNOS_TL42R_PARAM_PA_07D = 125u,
    SRNOS_TL42R_PARAM_PA_07E = 126u,
    SRNOS_TL42R_PARAM_PA_080 = 128u,
    SRNOS_TL42R_PARAM_PA_081 = 129u,
    SRNOS_TL42R_PARAM_PA_082 = 130u,
    SRNOS_TL42R_PARAM_PA_083 = 131u,
    SRNOS_TL42R_PARAM_PA_084 = 132u,
    SRNOS_TL42R_PARAM_PA_086 = 134u,
    SRNOS_TL42R_PARAM_PA_087 = 135u,
    SRNOS_TL42R_PARAM_PA_088 = 136u,
    SRNOS_TL42R_PARAM_PA_089 = 137u,
    SRNOS_TL42R_PARAM_PA_08A = 138u,
    SRNOS_TL42R_PARAM_PA_08C = 140u,
    SRNOS_TL42R_PARAM_PA_08D = 141u,
    SRNOS_TL42R_PARAM_PA_08E = 142u,
    SRNOS_TL42R_PARAM_PA_08F = 143u,
    SRNOS_TL42R_PARAM_PA_090 = 144u,
    SRNOS_TL42R_PARAM_PA_092 = 146u,
    SRNOS_TL42R_PARAM_PA_093 = 147u,
    SRNOS_TL42R_PARAM_PA_094 = 148u,
    SRNOS_TL42R_PARAM_PA_095 = 149u,
    SRNOS_TL42R_PARAM_PA_096 = 150u,
    SRNOS_TL42R_PARAM_PA_098 = 152u,
    SRNOS_TL42R_PARAM_PA_099 = 153u,
    SRNOS_TL42R_PARAM_PA_09A = 154u,
    SRNOS_TL42R_PARAM_PA_09B = 155u,
    SRNOS_TL42R_PARAM_PA_09C = 156u,
    SRNOS_TL42R_PARAM_PA_09E = 158u,
    SRNOS_TL42R_PARAM_PA_09F = 159u,
    SRNOS_TL42R_PARAM_PA_0A0 = 160u,
    SRNOS_TL42R_PARAM_PA_0A1 = 161u,
    SRNOS_TL42R_PARAM_PA_0A2 = 162u,
    SRNOS_TL42R_PARAM_PA_0A4 = 164u,
    SRNOS_TL42R_PARAM_PA_0A5 = 165u,
    SRNOS_TL42R_PARAM_PA_0A6 = 166u,
    SRNOS_TL42R_PARAM_PA_0A7 = 167u,
    SRNOS_TL42R_PARAM_PA_0A8 = 168u,
    SRNOS_TL42R_PARAM_PA_0AA = 170u,
    SRNOS_TL42R_PARAM_PA_0AB = 171u,
    SRNOS_TL42R_PARAM_PA_0AC = 172u,
    SRNOS_TL42R_PARAM_PA_0AD = 173u,
    SRNOS_TL42R_PARAM_PA_0AE = 174u,

    SRNOS_TL42R_PARAM_PA_0B0 = 176u,
    SRNOS_TL42R_PARAM_PA_0B1 = 177u,
    SRNOS_TL42R_PARAM_PA_0B2 = 178u,
    SRNOS_TL42R_PARAM_PA_0B3 = 179u,
    SRNOS_TL42R_PARAM_PA_0B4 = 180u,
    SRNOS_TL42R_PARAM_PA_0B5 = 181u,
    SRNOS_TL42R_PARAM_PA_0B6 = 182u,
    SRNOS_TL42R_PARAM_PA_0B7 = 183u,
    SRNOS_TL42R_PARAM_PA_0B8 = 184u,
    SRNOS_TL42R_PARAM_PA_0B9 = 185u,
    SRNOS_TL42R_PARAM_PA_0BA = 186u,
    SRNOS_TL42R_PARAM_PA_0BB = 187u,
    SRNOS_TL42R_PARAM_PA_0BC = 188u,
    SRNOS_TL42R_PARAM_PA_0BD = 189u,
    SRNOS_TL42R_PARAM_PA_0BE = 190u,
    SRNOS_TL42R_PARAM_PA_0BF = 191u,
    SRNOS_TL42R_PARAM_PA_0C0 = 192u,
    SRNOS_TL42R_PARAM_PA_0C1 = 193u,
    SRNOS_TL42R_PARAM_PA_0C2 = 194u,
    SRNOS_TL42R_PARAM_PA_0C3 = 195u,
    SRNOS_TL42R_PARAM_PA_0C4 = 196u,
    SRNOS_TL42R_PARAM_PA_0C5 = 197u,
    SRNOS_TL42R_PARAM_PA_0C6 = 198u,
    SRNOS_TL42R_PARAM_PA_0C7 = 199u,
    SRNOS_TL42R_PARAM_PA_0C8 = 200u,
    SRNOS_TL42R_PARAM_PA_0C9 = 201u,
    SRNOS_TL42R_PARAM_PA_0CA = 202u,
    SRNOS_TL42R_PARAM_PA_0CB = 203u,
    SRNOS_TL42R_PARAM_PA_0CC = 204u,
    SRNOS_TL42R_PARAM_PA_0CD = 205u,
    SRNOS_TL42R_PARAM_PA_0CE = 206u,
    SRNOS_TL42R_PARAM_PA_0CF = 207u,
    SRNOS_TL42R_PARAM_PA_0D0 = 208u,
    SRNOS_TL42R_PARAM_PA_0D1 = 209u,
    SRNOS_TL42R_PARAM_PA_0D2 = 210u,
    SRNOS_TL42R_PARAM_PA_0D3 = 211u,
    SRNOS_TL42R_PARAM_PA_0D4 = 212u,
    SRNOS_TL42R_PARAM_PA_0D5 = 213u,
    SRNOS_TL42R_PARAM_PA_0D6 = 214u,
    SRNOS_TL42R_PARAM_PA_0D7 = 215u,
    SRNOS_TL42R_PARAM_PA_0D8 = 216u,
    SRNOS_TL42R_PARAM_PA_0D9 = 217u,
    SRNOS_TL42R_PARAM_PA_0DA = 218u,
    SRNOS_TL42R_PARAM_PA_0DB = 219u,
    SRNOS_TL42R_PARAM_PA_0DC = 220u,
    SRNOS_TL42R_PARAM_PA_0DD = 221u,
    SRNOS_TL42R_PARAM_PA_0DE = 222u,
    SRNOS_TL42R_PARAM_PA_0DF = 223u,

    SRNOS_TL42R_PARAM_PA_100 = 256u,
    SRNOS_TL42R_PARAM_PA_101 = 257u,
    SRNOS_TL42R_PARAM_PA_102 = 258u,
    SRNOS_TL42R_PARAM_PA_103 = 259u,
    SRNOS_TL42R_PARAM_PA_104 = 260u,
    SRNOS_TL42R_PARAM_PA_105 = 261u,
    SRNOS_TL42R_PARAM_PA_106 = 262u,
    SRNOS_TL42R_PARAM_PA_107 = 263u,
    SRNOS_TL42R_PARAM_PA_109 = 265u,
    SRNOS_TL42R_PARAM_PA_10A = 266u,
    SRNOS_TL42R_PARAM_PA_10B = 267u,
    SRNOS_TL42R_PARAM_PA_10C = 268u,
    SRNOS_TL42R_PARAM_PA_10D = 269u,
    SRNOS_TL42R_PARAM_PA_10E = 270u,
    SRNOS_TL42R_PARAM_PA_10F = 271u,
    SRNOS_TL42R_PARAM_PA_110 = 272u,
    SRNOS_TL42R_PARAM_PA_111 = 273u,
    SRNOS_TL42R_PARAM_PA_112 = 274u,
    SRNOS_TL42R_PARAM_PA_113 = 275u,
    SRNOS_TL42R_PARAM_PA_114 = 276u,
    SRNOS_TL42R_PARAM_PA_115 = 277u,
    SRNOS_TL42R_PARAM_PA_116 = 278u,
    SRNOS_TL42R_PARAM_PA_117 = 279u,
    SRNOS_TL42R_PARAM_PA_118 = 280u,
    SRNOS_TL42R_PARAM_PA_119 = 281u
} SRNOS_TL42RParamId;

typedef SRNOS_TL42RParamId SRNOS_TL42RRegister;

enum {
    SRNOS_TL42R_REG_SOFTWARE_VERSION = SRNOS_TL42R_PARAM_PA_001,
    SRNOS_TL42R_REG_HARDWARE_VERSION = SRNOS_TL42R_PARAM_PA_002,
    SRNOS_TL42R_REG_RUN_STATUS = SRNOS_TL42R_PARAM_PA_004,
    SRNOS_TL42R_REG_ALARM = SRNOS_TL42R_PARAM_PA_005,
    SRNOS_TL42R_REG_DI_STATE = SRNOS_TL42R_PARAM_PA_006,
    SRNOS_TL42R_REG_DO_STATE = SRNOS_TL42R_PARAM_PA_007,
    SRNOS_TL42R_REG_POSITION_H = SRNOS_TL42R_PARAM_PA_008,
    SRNOS_TL42R_REG_POSITION_L = SRNOS_TL42R_PARAM_PA_009,
    SRNOS_TL42R_REG_CURRENT_SPEED = SRNOS_TL42R_PARAM_PA_00A,
    SRNOS_TL42R_REG_SUBDIVISION = SRNOS_TL42R_PARAM_PA_023,
    SRNOS_TL42R_REG_POSITION_SPEED = SRNOS_TL42R_PARAM_PA_036,
    SRNOS_TL42R_REG_POSITION_TARGET_H = SRNOS_TL42R_PARAM_PA_037,
    SRNOS_TL42R_REG_POSITION_TARGET_L = SRNOS_TL42R_PARAM_PA_038,
    SRNOS_TL42R_REG_CONTROL_WORD = SRNOS_TL42R_PARAM_PA_04E,
    SRNOS_TL42R_REG_AUX_CONTROL = SRNOS_TL42R_PARAM_PA_04F,
    SRNOS_TL42R_REG_RUN_MODE = SRNOS_TL42R_PARAM_PA_100,
    SRNOS_TL42R_REG_ENCODER_RES = SRNOS_TL42R_PARAM_PA_101
};

enum {
    SRNOS_TL42R_MONITOR_REG_START = SRNOS_TL42R_REG_RUN_STATUS,
    SRNOS_TL42R_MONITOR_REG_COUNT = 7u,
    SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT = 16u,
    SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT = 16u
};

typedef enum {
    SRNOS_TL42R_STATUS_IN_POSITION = (1u << 0),
    SRNOS_TL42R_STATUS_HOMING_DONE = (1u << 1),
    SRNOS_TL42R_STATUS_MOTOR_RUNNING = (1u << 2),
    SRNOS_TL42R_STATUS_FAULT = (1u << 3),
    SRNOS_TL42R_STATUS_ENABLED = (1u << 4),
    SRNOS_TL42R_STATUS_POSITIVE_SOFT_LIM = (1u << 5),
    SRNOS_TL42R_STATUS_NEGATIVE_SOFT_LIM = (1u << 6)
} SRNOS_TL42RRunStatusBit;

typedef enum {
    SRNOS_TL42R_CTRL_POSITION_TRIGGER = (1u << 0),
    SRNOS_TL42R_CTRL_ABSOLUTE_MODE = (1u << 1),
    SRNOS_TL42R_CTRL_INTERRUPT_MODE = (1u << 2),
    SRNOS_TL42R_CTRL_JOG_TRIGGER = (1u << 3),
    SRNOS_TL42R_CTRL_HOMING_TRIGGER = (1u << 4),
    SRNOS_TL42R_CTRL_STOP = (1u << 5),
    SRNOS_TL42R_CTRL_ESTOP = (1u << 6)
} SRNOS_TL42RControlWordBit;

typedef enum {
    SRNOS_TL42R_ALARM_NONE = 0x00u,
    SRNOS_TL42R_ALARM_OVERCURRENT = 0x01u,
    SRNOS_TL42R_ALARM_OVERVOLTAGE = 0x02u,
    SRNOS_TL42R_ALARM_UNDERVOLTAGE = 0x03u
} SRNOS_TL42RAlarmCode;

typedef enum {
    SRNOS_TL42R_DI_FUNC_NONE = 0x00u,
    SRNOS_TL42R_DI_FUNC_HOME = 0x01u,
    SRNOS_TL42R_DI_FUNC_POS_LIMIT = 0x02u,
    SRNOS_TL42R_DI_FUNC_NEG_LIMIT = 0x03u,
    SRNOS_TL42R_DI_FUNC_RELEASE = 0x04u,
    SRNOS_TL42R_DI_FUNC_STOP = 0x05u,
    SRNOS_TL42R_DI_FUNC_ESTOP = 0x06u,
    SRNOS_TL42R_DI_FUNC_POS_JOG = 0x09u,
    SRNOS_TL42R_DI_FUNC_NEG_JOG = 0x0Au,
    SRNOS_TL42R_DI_FUNC_HOME_TRIGGER = 0x0Bu,
    SRNOS_TL42R_DI_FUNC_POSITION_TRIGGER = 0x0Cu,
    SRNOS_TL42R_DI_FUNC_SPEED_TRIGGER = 0x0Du,
    SRNOS_TL42R_DI_FUNC_ADDR0 = 0x0Eu,
    SRNOS_TL42R_DI_FUNC_ADDR1 = 0x0Fu,
    SRNOS_TL42R_DI_FUNC_ADDR2 = 0x10u,
    SRNOS_TL42R_DI_FUNC_ADDR3 = 0x11u
} SRNOS_TL42RDiFunction;

typedef enum {
    SRNOS_TL42R_DO_FUNC_NONE = 0x00u,
    SRNOS_TL42R_DO_FUNC_ALARM = 0x01u,
    SRNOS_TL42R_DO_FUNC_RUNNING = 0x02u,
    SRNOS_TL42R_DO_FUNC_HOMING_DONE = 0x03u,
    SRNOS_TL42R_DO_FUNC_IN_POSITION = 0x04u,
    SRNOS_TL42R_DO_FUNC_BRAKE = 0x05u
} SRNOS_TL42RDoFunction;

typedef enum {
    SRNOS_TL42R_SERIAL_8N1 = 0u,
    SRNOS_TL42R_SERIAL_8N2 = 1u,
    SRNOS_TL42R_SERIAL_8E1 = 2u,
    SRNOS_TL42R_SERIAL_8O1 = 3u
} SRNOS_TL42RSerialFormat;

typedef enum {
    SRNOS_TL42R_BAUD_115200 = 0u,
    SRNOS_TL42R_BAUD_38400 = 1u,
    SRNOS_TL42R_BAUD_19200 = 2u,
    SRNOS_TL42R_BAUD_9600 = 3u
} SRNOS_TL42RBaudrate;

typedef enum {
    SRNOS_TL42R_MULTI_POSITION_MODE_RELATIVE = 0u,
    SRNOS_TL42R_MULTI_POSITION_MODE_ABSOLUTE = 1u
} SRNOS_TL42RMultiPositionMode;

typedef enum {
    SRNOS_TL42R_HOMING_NEGATIVE_APPROACH = 17u,
    SRNOS_TL42R_HOMING_POSITIVE_LIMIT = 18u,
    SRNOS_TL42R_HOMING_POSITIVE_HOME = 24u,
    SRNOS_TL42R_HOMING_NEGATIVE_HOME = 29u,
    SRNOS_TL42R_HOMING_SET_CURRENT_AS_ZERO = 35u
} SRNOS_TL42RHomingMode;

typedef enum {
    SRNOS_TL42R_AUX_NONE = 0x0000u,
    SRNOS_TL42R_AUX_RESTORE_FACTORY = 0x0100u,
    SRNOS_TL42R_AUX_SAVE_CURRENT = 0x0200u,
    SRNOS_TL42R_AUX_CLEAR_ALARM = 0x0300u,
    SRNOS_TL42R_AUX_CLEAR_POSITION = 0x0400u,
    SRNOS_TL42R_AUX_MOTOR_ENABLE = 0x0500u,
    SRNOS_TL42R_AUX_MOTOR_RELEASE = 0x0600u
} SRNOS_TL42RAuxCommand;

typedef enum {
    SRNOS_TL42R_RUN_MODE_OPEN_LOOP = 1u,
    SRNOS_TL42R_RUN_MODE_CLOSED_LOOP = 2u
} SRNOS_TL42RRunMode;

typedef enum {
    SRNOS_TL42R_CONFIG_CACHE_BASIC = (1u << 0),
    SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION = (1u << 1),
    SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED = (1u << 2),
    SRNOS_TL42R_CONFIG_CACHE_ALL = (SRNOS_TL42R_CONFIG_CACHE_BASIC |
                                 SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION |
                                 SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED)
} SRNOS_TL42RConfigCacheMask;

typedef struct {
    SRNOS_TL42RParamId param;
    uint16_t address;
    SRNOS_TL42RParamType type;
    bool readable;
    bool writable;
    bool persistent;
    SRNOS_TL42RParamGroup group;
} SRNOS_TL42RParamDescriptor;

typedef struct {
    bool in_position;
    bool homing_done;
    bool motor_running;
    bool fault;
    bool enabled;
    bool positive_soft_limit;
    bool negative_soft_limit;
} SRNOS_TL42RRunStatusBits;

typedef struct {
    bool di0;
    bool di1;
    bool di2;
    bool di3;
    bool di4;
} SRNOS_TL42RDiStateBits;

typedef struct {
    bool do0;
    bool do1;
    bool do2;
} SRNOS_TL42RDoStateBits;

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
    SRNOS_TL42RRunStatusBits run_status;
    SRNOS_TL42RDiStateBits di_state;
    SRNOS_TL42RDoStateBits do_state;
} SRNOS_TL42RMonitor;

typedef struct {
    uint16_t normally_closed_mask;
    SRNOS_TL42RDiFunction input_function[5];
    uint16_t filter_coefficient;
} SRNOS_TL42RDiConfig;

typedef struct {
    uint16_t normally_closed_mask;
    SRNOS_TL42RDoFunction output_function[2];
    uint16_t forced_output_mask;
} SRNOS_TL42RDoConfig;

typedef struct {
    uint16_t station_id;
    SRNOS_TL42RSerialFormat serial_format;
    uint16_t default_direction;
    uint16_t subdivision;
    uint16_t limit_stop_mode;
    uint16_t soft_limit_enable;
    uint16_t multi_position_trigger_mode;
    SRNOS_TL42RBaudrate baudrate;
} SRNOS_TL42RCommConfig;

typedef struct {
    int16_t jog_speed_rpm;
    uint16_t jog_accel_ms;
    uint16_t jog_decel_ms;
    uint16_t position_start_speed_rpm;
    uint16_t position_accel_ms;
    uint16_t position_decel_ms;
    uint16_t position_speed_rpm;
    int32_t position_target_pulse;
    SRNOS_TL42RMultiPositionMode multi_position_mode;
    uint16_t control_word;
    uint16_t aux_control;
} SRNOS_TL42RMotionConfig;

typedef struct {
    SRNOS_TL42RHomingMode homing_mode;
    uint16_t homing_approach_speed_rpm;
    uint16_t homing_crawl_speed_rpm;
    uint16_t homing_accel_ms;
    int32_t homing_offset_pulse;
    int32_t positive_soft_limit_pulse;
    int32_t negative_soft_limit_pulse;
} SRNOS_TL42RHomingConfig;

typedef struct {
    SRNOS_TL42RRunMode run_mode;
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
} SRNOS_TL42RFactoryParams;

typedef struct {
    SRNOS_TL42RDiConfig di_config;
    SRNOS_TL42RDoConfig do_config;
    SRNOS_TL42RCommConfig comm_config;
    SRNOS_TL42RMotionConfig motion_config;
    SRNOS_TL42RHomingConfig homing_config;
    SRNOS_TL42RFactoryParams factory_params;
} SRNOS_TL42RBasicConfig;

typedef struct {
    int32_t target_pulse;
    uint16_t speed_rpm;
    uint16_t accel_ms;
    uint16_t decel_ms;
} SRNOS_TL42RPositionSegment;

typedef struct {
    SRNOS_TL42RPositionSegment segments[SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT];
} SRNOS_TL42RMultiPositionTable;

typedef struct {
    int16_t speed_rpm;
    uint16_t accel_ms;
    uint16_t decel_ms;
} SRNOS_TL42RSpeedSegment;

typedef struct {
    SRNOS_TL42RSpeedSegment segments[SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT];
} SRNOS_TL42RMultiSpeedTable;

typedef struct {
    bool initialized;
    bool basic_valid;
    bool position_table_valid;
    bool speed_table_valid;
    uint32_t last_refresh_ms;
    SRNOS_TL42RBasicConfig basic;
    SRNOS_TL42RMultiPositionTable position_table;
    SRNOS_TL42RMultiSpeedTable speed_table;
} SRNOS_TL42RConfigCache;

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
    SRNOS_TL42RMonitor monitor;
} SRNOS_TL42RWatch;

extern volatile SRNOS_TL42RWatch SRNOS_DebugTL42RWatch;
extern volatile SRNOS_TL42RConfigCache SRNOS_DebugTL42RConfigCache;

typedef struct {
    SRNOS_ModbusRtuClient SRNOS_ModbusClient;
    uint8_t port;
    uint8_t unit_id;
    int last_error;
    uint8_t last_exception_code;
} SRNOS_TL42RDevice;

void SRNOS_TL42RReset(SRNOS_TL42RDevice *dev);

int SRNOS_TL42RInit(SRNOS_TL42RDevice *dev, uint8_t port, uint8_t unit_id);

int SRNOS_TL42RDeinit(SRNOS_TL42RDevice *dev);

bool SRNOS_TL42RGetParamDescriptor(SRNOS_TL42RParamId param,
                                   SRNOS_TL42RParamDescriptor *out_descriptor);

int SRNOS_TL42RReadU16(SRNOS_TL42RDevice *dev,
                      SRNOS_TL42RParamId param,
                      uint16_t *out_value,
                      uint32_t timeout_ms);

int SRNOS_TL42RReadI16(SRNOS_TL42RDevice *dev,
                      SRNOS_TL42RParamId param,
                      int16_t *out_value,
                      uint32_t timeout_ms);

int SRNOS_TL42RReadU32(SRNOS_TL42RDevice *dev,
                      SRNOS_TL42RParamId param,
                      uint32_t *out_value,
                      uint32_t timeout_ms);

int SRNOS_TL42RReadI32(SRNOS_TL42RDevice *dev,
                      SRNOS_TL42RParamId param,
                      int32_t *out_value,
                      uint32_t timeout_ms);

int SRNOS_TL42RWriteU16(SRNOS_TL42RDevice *dev,
                       SRNOS_TL42RParamId param,
                       uint16_t value,
                       uint32_t timeout_ms);

int SRNOS_TL42RWriteI16(SRNOS_TL42RDevice *dev,
                       SRNOS_TL42RParamId param,
                       int16_t value,
                       uint32_t timeout_ms);

int SRNOS_TL42RWriteU32(SRNOS_TL42RDevice *dev,
                       SRNOS_TL42RParamId param,
                       uint32_t value,
                       uint32_t timeout_ms);

int SRNOS_TL42RWriteI32(SRNOS_TL42RDevice *dev,
                       SRNOS_TL42RParamId param,
                       int32_t value,
                       uint32_t timeout_ms);

int SRNOS_TL42RSaveParameters(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RExecuteAuxCommand(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RAuxCommand command,
                                 uint32_t timeout_ms);

int SRNOS_TL42RReadVersions(SRNOS_TL42RDevice *dev,
                           uint16_t *out_software_version,
                           uint16_t *out_hardware_version,
                           uint32_t timeout_ms);

int SRNOS_TL42RReadRunStatus(SRNOS_TL42RDevice *dev,
                             uint16_t *out_status,
                             uint32_t timeout_ms);

int SRNOS_TL42RReadAlarm(SRNOS_TL42RDevice *dev,
                        uint16_t *out_alarm,
                        uint32_t timeout_ms);

int SRNOS_TL42RReadStatusAndAlarm(SRNOS_TL42RDevice *dev,
                                   uint16_t *out_status,
                                   uint16_t *out_alarm,
                                   uint32_t timeout_ms);

int SRNOS_TL42RReadDigitalInputs(SRNOS_TL42RDevice *dev,
                                 uint16_t *out_di_state,
                                 uint32_t timeout_ms);

int SRNOS_TL42RReadDigitalOutputs(SRNOS_TL42RDevice *dev,
                                  uint16_t *out_do_state,
                                  uint32_t timeout_ms);

int SRNOS_TL42RReadCurrentPosition(SRNOS_TL42RDevice *dev,
                                   int32_t *out_position_pulse,
                                   uint32_t timeout_ms);

int SRNOS_TL42RReadCurrentSpeed(SRNOS_TL42RDevice *dev,
                                int16_t *out_speed_rpm,
                                uint32_t timeout_ms);

int SRNOS_TL42RReadSubdivision(SRNOS_TL42RDevice *dev,
                              uint16_t *out_subdivision,
                              uint32_t timeout_ms);

int SRNOS_TL42RReadMonitor(SRNOS_TL42RDevice *dev,
                          SRNOS_TL42RMonitor *out_monitor,
                          uint32_t timeout_ms);

int SRNOS_TL42RReadDriveInfo(SRNOS_TL42RDevice *dev,
                             uint16_t *out_run_mode,
                             uint16_t *out_encoder_resolution,
                             uint32_t timeout_ms);

int SRNOS_TL42RReadFactoryParams(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RFactoryParams *out_factory_params,
                                 uint32_t timeout_ms);

int SRNOS_TL42RReadBasicConfig(SRNOS_TL42RDevice *dev,
                               SRNOS_TL42RBasicConfig *out_basic_config,
                               uint32_t timeout_ms);

int SRNOS_TL42RSetDiConfig(SRNOS_TL42RDevice *dev,
                           const SRNOS_TL42RDiConfig *config,
                           uint32_t timeout_ms);

int SRNOS_TL42RSetDoConfig(SRNOS_TL42RDevice *dev,
                           const SRNOS_TL42RDoConfig *config,
                           uint32_t timeout_ms);

int SRNOS_TL42RSetCommunicationConfig(SRNOS_TL42RDevice *dev,
                                      const SRNOS_TL42RCommConfig *config,
                                      uint32_t timeout_ms);

int SRNOS_TL42RSetMotionConfig(SRNOS_TL42RDevice *dev,
                               const SRNOS_TL42RMotionConfig *config,
                               uint32_t timeout_ms);

int SRNOS_TL42RSetHomingConfig(SRNOS_TL42RDevice *dev,
                               const SRNOS_TL42RHomingConfig *config,
                               uint32_t timeout_ms);

int SRNOS_TL42RReadMultiPositionSegment(SRNOS_TL42RDevice *dev,
                                         uint8_t segment_index,
                                         SRNOS_TL42RPositionSegment *out_segment,
                                         uint32_t timeout_ms);

int SRNOS_TL42RWriteMultiPositionSegment(SRNOS_TL42RDevice *dev,
                                          uint8_t segment_index,
                                          const SRNOS_TL42RPositionSegment *segment,
                                          uint32_t timeout_ms);

int SRNOS_TL42RReadMultiPositionTable(SRNOS_TL42RDevice *dev,
                                       SRNOS_TL42RMultiPositionTable *out_table,
                                       uint32_t timeout_ms);

int SRNOS_TL42RWriteMultiPositionTable(SRNOS_TL42RDevice *dev,
                                        const SRNOS_TL42RMultiPositionTable *table,
                                        uint32_t timeout_ms);

int SRNOS_TL42RReadMultiSpeedSegment(SRNOS_TL42RDevice *dev,
                                      uint8_t segment_index,
                                      SRNOS_TL42RSpeedSegment *out_segment,
                                      uint32_t timeout_ms);

int SRNOS_TL42RWriteMultiSpeedSegment(SRNOS_TL42RDevice *dev,
                                       uint8_t segment_index,
                                       const SRNOS_TL42RSpeedSegment *segment,
                                       uint32_t timeout_ms);

int SRNOS_TL42RReadMultiSpeedTable(SRNOS_TL42RDevice *dev,
                                    SRNOS_TL42RMultiSpeedTable *out_table,
                                    uint32_t timeout_ms);

int SRNOS_TL42RWriteMultiSpeedTable(SRNOS_TL42RDevice *dev,
                                     const SRNOS_TL42RMultiSpeedTable *table,
                                     uint32_t timeout_ms);

int SRNOS_TL42RSetPositionSpeed(SRNOS_TL42RDevice *dev,
                                uint16_t speed_rpm,
                                uint32_t timeout_ms);

int SRNOS_TL42RSetPositionTarget(SRNOS_TL42RDevice *dev,
                                 int32_t target_pulse,
                                 uint32_t timeout_ms);

int SRNOS_TL42RWriteControlWord(SRNOS_TL42RDevice *dev,
                                uint16_t control_word,
                                uint32_t timeout_ms);

int SRNOS_TL42RStartRelativeMove(SRNOS_TL42RDevice *dev,
                                 int32_t target_pulse,
                                 uint16_t speed_rpm,
                                 bool interrupt_current,
                                 uint32_t timeout_ms);

int SRNOS_TL42RStartAbsoluteMove(SRNOS_TL42RDevice *dev,
                                 int32_t target_pulse,
                                 uint16_t speed_rpm,
                                 bool interrupt_current,
                                 uint32_t timeout_ms);

int SRNOS_TL42RStartJog(SRNOS_TL42RDevice *dev,
                       int16_t speed_rpm,
                       uint16_t accel_ms,
                       uint16_t decel_ms,
                       uint32_t timeout_ms);

int SRNOS_TL42RStartHoming(SRNOS_TL42RDevice *dev,
                          SRNOS_TL42RHomingMode homing_mode,
                          uint16_t approach_speed_rpm,
                          uint16_t crawl_speed_rpm,
                          uint16_t accel_ms,
                          int32_t offset_pulse,
                          uint32_t timeout_ms);

int SRNOS_TL42RStop(SRNOS_TL42RDevice *dev,
                  bool emergency_stop,
                  uint32_t timeout_ms);

int SRNOS_TL42REstop(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RRestoreFactoryDefaults(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RClearAlarm(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RClearPosition(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RMotorEnable(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

int SRNOS_TL42RMotorRelease(SRNOS_TL42RDevice *dev, uint32_t timeout_ms);

void SRNOS_TL42RWatchReset(uint8_t port, uint8_t unit_id, int32_t pulses_per_rev);

void SRNOS_TL42RWatchSetVersions(uint16_t software_version, uint16_t hardware_version);

void SRNOS_TL42RWatchSetDriveInfo(uint16_t run_mode, uint16_t encoder_resolution);

void SRNOS_TL42RWatchSetPulsesPerRev(int32_t pulses_per_rev);

void SRNOS_TL42RWatchSetZeroCurrent(void);

void SRNOS_TL42RWatchMarkError(int error_code, uint32_t now_ms);

void SRNOS_TL42RWatchUpdate(const SRNOS_TL42RMonitor *monitor, uint32_t now_ms);

bool SRNOS_TL42RGetWatch(SRNOS_TL42RWatch *out_watch);

void SRNOS_TL42RConfigCacheReset(void);

int SRNOS_TL42RRefreshConfigCache(SRNOS_TL42RDevice *dev,
                                  uint32_t refresh_mask,
                                  uint32_t timeout_ms);

bool SRNOS_TL42RGetConfigCache(SRNOS_TL42RConfigCache *out_cache);

void SRNOS_TL42RDecodeRunStatus(uint16_t raw_status, SRNOS_TL42RRunStatusBits *out_status);

void SRNOS_TL42RDecodeDiState(uint16_t raw_di_state, SRNOS_TL42RDiStateBits *out_state);

void SRNOS_TL42RDecodeDoState(uint16_t raw_do_state, SRNOS_TL42RDoStateBits *out_state);

int32_t SRNOS_TL42RComposePosition(uint16_t position_high, uint16_t position_low);

int16_t SRNOS_TL42RDecodeSpeed(uint16_t raw_speed);

const char *SRNOS_TL42RAlarmToString(uint16_t alarm_code);

const char *SRNOS_TL42RRunModeToString(uint16_t run_mode);

const char *SRNOS_TL42RErrorToString(int error_code);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_RS485_LC_TL42R_H
