//
// Created by bismarckkk on 2024/2/17.
//

#ifndef SRNOS_REFEREE_UI_UI_TYPES_H
#define SRNOS_REFEREE_UI_UI_TYPES_H

// User Code Begin

#if defined(__GNUC__) || defined(__CC_ARM)
#define SRNOS_REFEREE_UI_MESSAGE_PACKED __attribute__((packed))
#include <stdint.h>
#else
#error "SRNOS_REFEREE_UI_MESSAGE_PACKED not defined for this compiler"
#endif

// User Code End

#define SRNOS_REFEREE_UI_PRIMITIVE_CAT(x, y) x ## y
#define SRNOS_REFEREE_UI_CAT(x, y) SRNOS_REFEREE_UI_PRIMITIVE_CAT(x, y)

#define SRNOS_REFEREE_UI_DEFINE_MESSAGE(name, p_a, p_b, p_c, p_d, p_e)   \
typedef struct {                                        \
uint8_t figure_name[3];                                 \
uint32_t operate_type:3;                                \
uint32_t figure_type:3;                                 \
uint32_t layer:4;                                       \
uint32_t color:4;                                       \
uint32_t SRNOS_REFEREE_UI_PRIMITIVE_CAT(,p_a) :9;                        \
uint32_t SRNOS_REFEREE_UI_PRIMITIVE_CAT(,p_b):9;                         \
uint32_t width:10;                                      \
uint32_t start_x:11;                                    \
uint32_t start_y:11;                                    \
uint32_t SRNOS_REFEREE_UI_PRIMITIVE_CAT(,p_c):10;                        \
uint32_t SRNOS_REFEREE_UI_PRIMITIVE_CAT(,p_d):11;                        \
uint32_t SRNOS_REFEREE_UI_PRIMITIVE_CAT(,p_e):11;                        \
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUIInterface ## name

SRNOS_REFEREE_UI_DEFINE_MESSAGE(Figure, _a, _b, _c, _d, _e);
SRNOS_REFEREE_UI_DEFINE_MESSAGE(Line, _a, _b, _c, end_x, end_y);
SRNOS_REFEREE_UI_DEFINE_MESSAGE(Rect, _a, _b, _c, end_x, end_y);
SRNOS_REFEREE_UI_DEFINE_MESSAGE(Round, _a, _b, r, _d, _e);
SRNOS_REFEREE_UI_DEFINE_MESSAGE(Ellipse, _a, _b, _c, rx, ry);
SRNOS_REFEREE_UI_DEFINE_MESSAGE(Arc, start_angle, end_angle, _c, rx, ry);

typedef struct {
    uint8_t figure_name[3];
    uint32_t operate_type: 3;
    uint32_t figure_type: 3;
    uint32_t layer: 4;
    uint32_t color: 4;
    uint32_t font_size: 9;
    uint32_t _b: 9;
    uint32_t width: 10;
    uint32_t start_x: 11;
    uint32_t start_y: 11;
    int32_t number;
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUIInterfaceNumber;

typedef struct {
    uint8_t figure_name[3];
    uint32_t operate_type: 3;
    uint32_t figure_type: 3;
    uint32_t layer: 4;
    uint32_t color: 4;
    uint32_t font_size: 9;
    uint32_t str_length: 9;
    uint32_t width: 10;
    uint32_t start_x: 11;
    uint32_t start_y: 11;
    uint32_t _c: 10;
    uint32_t _d: 11;
    uint32_t _e: 11;
    char string[30];
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUIInterfaceString;

typedef struct {
    uint8_t SOF;
    uint16_t length;
    uint8_t seq, crc8;
    uint16_t cmd_id, sub_id;
    uint16_t send_id, recv_id;
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUIFrameHeader;

#define SRNOS_REFEREE_UI_DEFINE_FIGURE_MESSAGE(num)      \
typedef struct {                        \
SRNOS_RefereeUIFrameHeader header;               \
SRNOS_RefereeUIInterfaceFigure data[num];        \
uint16_t crc16;                         \
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUI ## num ## Frame

SRNOS_REFEREE_UI_DEFINE_FIGURE_MESSAGE(1);
SRNOS_REFEREE_UI_DEFINE_FIGURE_MESSAGE(2);
SRNOS_REFEREE_UI_DEFINE_FIGURE_MESSAGE(5);
SRNOS_REFEREE_UI_DEFINE_FIGURE_MESSAGE(7);

typedef struct {
    SRNOS_RefereeUIFrameHeader header;
    SRNOS_RefereeUIInterfaceString option;
    uint16_t crc16;
} SRNOS_REFEREE_UI_MESSAGE_PACKED SRNOS_RefereeUIStringFrame;

#endif //SRNOS_REFEREE_UI_UI_TYPES_H
