#pragma once
#include "usbd_def.h"
extern int USBD_COMPOSITE, SRNOS_USBCdcInterface;
void USBD_COMPOSITE_Mount_Class(void);
int USBD_CDC_ACM_RegisterInterface(USBD_HandleTypeDef *, void *);
