#ifndef TEST_USBD_CDC_ACM_IF_H
#define TEST_USBD_CDC_ACM_IF_H

#include <stdint.h>

#define USBD_OK   0u
#define USBD_BUSY 1u
#define USBD_FAIL 2u
#define USBD_STATE_CONFIGURED 3u
#define NUMBER_OF_CDC 2u

#define CDC_SET_LINE_CODING        0x20u
#define CDC_GET_LINE_CODING        0x21u
#define CDC_SET_CONTROL_LINE_STATE 0x22u

typedef struct {
    uint8_t dev_state;
} USBD_HandleTypeDef;

typedef struct {
    uint8_t bmRequest;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} USBD_SetupReqTypedef;

typedef struct {
    uint32_t bitrate;
    uint8_t format;
    uint8_t paritytype;
    uint8_t datatype;
} USBD_CDC_ACM_LineCodingTypeDef;

typedef struct {
    uint32_t TxState;
} USBD_CDC_ACM_HandleTypeDef;

typedef struct {
    int8_t (*Init)(uint8_t ch);
    int8_t (*DeInit)(uint8_t ch);
    int8_t (*Control)(uint8_t ch, uint8_t cmd, uint8_t *data, uint16_t length);
    int8_t (*Receive)(uint8_t ch, uint8_t *data, uint32_t *length);
    int8_t (*TransmitCplt)(uint8_t ch, uint8_t *data,
                           uint32_t *length, uint8_t epnum);
} USBD_CDC_ACM_ItfTypeDef;

extern uint8_t CDC_IN_EP[NUMBER_OF_CDC];
extern uint8_t CDC_OUT_EP[NUMBER_OF_CDC];
extern uint8_t CDC_CMD_EP[NUMBER_OF_CDC];

uint8_t USBD_CDC_SetTxBuffer(uint8_t ch, USBD_HandleTypeDef *device,
                             uint8_t *data, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(uint8_t ch, USBD_HandleTypeDef *device,
                             uint8_t *data);
uint8_t USBD_CDC_ReceivePacket(uint8_t ch, USBD_HandleTypeDef *device);
uint8_t USBD_CDC_TransmitPacket(uint8_t ch, USBD_HandleTypeDef *device);

uint8_t SRNOS_USBCdcTransmit(uint8_t ch, uint8_t *data, uint16_t length);

#endif
