#pragma once
#include "FreeRTOS.h"
typedef StaticEventGroup_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *);
uint32_t xEventGroupSetBits(EventGroupHandle_t,uint32_t);
uint32_t xEventGroupClearBits(EventGroupHandle_t,uint32_t);
uint32_t xEventGroupWaitBits(EventGroupHandle_t,uint32_t,BaseType_t,BaseType_t,TickType_t);
void vEventGroupDelete(EventGroupHandle_t);
