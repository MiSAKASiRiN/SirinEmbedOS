#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {struct {uint32_t Identifier,DataLength,IdType,RxFrameType,FDFormat,BitRateSwitch,ErrorStateIndicator;} hdr;uint8_t data[64];} SRNOS_FDCANRxFrame;
bool SRNOS_FDCANStart(uint8_t);
size_t SRNOS_FDCANRxPop(uint8_t,SRNOS_FDCANRxFrame *,size_t);
