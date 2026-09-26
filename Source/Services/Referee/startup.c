#include "SirinEmbedOS/Services/Referee/Startup.h"
#include "SirinEmbedOS/Services/Referee/Receiver/tskptt_referee.h"
#include "cmsis_os2.h"
static bool configured,enabled,started;
int SRNOS_RefereeConfigure(SRNOS_SerialPortId port) {
 if(configured)return -1;
 if(port!=SRNOS_SERIAL_NONE){if(!Referee_BindChannel(SRNOS_SerialGetChannel(port))||!Referee_Init())return -1;enabled=true;}
 configured=true;return 0;
}
int SRNOS_RefereeStart(void) {
 if(!configured||started||osKernelGetState()!=osKernelReady)return -1;started=true;
 if(!enabled)return 0;
 const osThreadAttr_t attr={.name="Referee",.stack_size=3072,.priority=osPriorityHigh};
 return osThreadNew(TSKPTT_Referee_Task,NULL,&attr)?0:-1;
}
