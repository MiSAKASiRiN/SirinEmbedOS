#pragma once
#include "usbd_def.h"
int USBD_Init(USBD_HandleTypeDef *, USBD_DescriptorsTypeDef *, int);
int USBD_RegisterClass(USBD_HandleTypeDef *, void *);
int USBD_Start(USBD_HandleTypeDef *);
int USBD_DeInit(USBD_HandleTypeDef *);
