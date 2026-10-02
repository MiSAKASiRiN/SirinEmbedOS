#pragma once
#include <stdbool.h>
/* Before USBD_Init, including retries. Does not start USB. */
bool SRNOS_BoardUSBPrepare(void);
