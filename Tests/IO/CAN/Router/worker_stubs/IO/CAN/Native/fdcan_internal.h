#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {struct {uint32_t Identifier,DataLength,IdType,RxFrameType,FDFormat,BitRateSwitch,ErrorStateIndicator;} hdr;uint8_t data[64];} SRNOS_FDCANRxFrame;
bool SRNOS_FDCANStart(uint8_t);
size_t SRNOS_FDCANRxPop(uint8_t,SRNOS_FDCANRxFrame *,size_t);

#include "SirinEmbedOS/IO/CAN/FD/CANFD.h"
bool SRNOS_FDCANIsReady(uint8_t);
bool SRNOS_FDCANTxPush(uint8_t,uint16_t,const uint8_t*,uint8_t);
bool SRNOS_FDCANTxPushExt(uint8_t,uint32_t,const uint8_t*,uint8_t);
bool SRNOS_FDCANTxPushFD(SRNOS_CANChannel,const SRNOS_CANFDFrame*);
bool SRNOS_FDCANService(uint8_t,uint32_t);
uint32_t SRNOS_FDCANNextServiceWait(uint8_t,uint32_t);
