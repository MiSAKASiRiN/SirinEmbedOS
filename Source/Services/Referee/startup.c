#include "thread_internal.h"
#include "SirinEmbedOS/Services/Referee/Startup.h"
#include "SirinEmbedOS/Services/Referee/Receiver/RefereeReceiver.h"
#include "cmsis_os2.h"
static bool configured,enabled,started;
int SRNOS_RefereeConfigure(SRNOS_SerialPortId port) {
 if(configured)return -1;
 if(port!=SRNOS_SERIAL_NONE){if(!SRNOS_RefereeBindChannel(SRNOS_SerialGetChannel(port))||!SRNOS_RefereeInit())return -1;enabled=true;}
 configured=true;return 0;
}
int SRNOS_RefereeStart(void) {
 if(!configured||started||osKernelGetState()!=osKernelReady)return -1;started=true;
 if(!enabled)return 0;
 const osThreadAttr_t attr={.name="SRNOS_Referee",.stack_size=3072,.priority=osPriorityHigh7};
 return SRNOS_KernelThreadNew(SRNOS_RefereeTask,NULL,&attr)?0:-1;
}
