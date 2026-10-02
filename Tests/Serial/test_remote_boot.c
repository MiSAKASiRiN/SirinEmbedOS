#include <assert.h>
#include <stdio.h>
#include "SirinEmbedOS/Services/RemoteControl/RemoteControl.h"
uint32_t test_primask;
static int scheduler,task_depth,task_entries;
int xTaskGetSchedulerState(void){return scheduler;}
void test_task_enter(void){assert(scheduler!=0);task_entries++;task_depth++;}
void test_task_exit(void){assert(task_depth>0);task_depth--;}
void SRNOS_RemoteControlFreeze(void);
int main(void){
 SRNOS_RC_InputInit();SRNOS_RC_SetSourcePriority(SRNOS_RC_SRC_VT03,SRNOS_RC_SRC_DR16);SRNOS_RC_SetTimeouts(50,300);SRNOS_RC_SetStickDeadzone(10);
 assert(test_primask==0&&task_entries==0&&task_depth==0);
 test_primask=1;SRNOS_RC_SetTimeouts(50,300);assert(test_primask==1&&task_entries==0);test_primask=0;
 SRNOS_RemoteControlFreeze();SRNOS_RC_SetSourcePriority(SRNOS_RC_SRC_DR16,SRNOS_RC_SRC_VT03);
 scheduler=1;SRNOS_RC_State input={0};SRNOS_RC_UpdateFromSource(SRNOS_RC_SRC_DR16,&input,10);SRNOS_RC_UpdateFromSource(SRNOS_RC_SRC_VT03,&input,10);
 assert(SRNOS_RC_GetStatus().active_src==SRNOS_RC_SRC_VT03); /* frozen priority was retained */
 assert(task_entries>0&&task_depth==0&&test_primask==0);
 puts("RemoteControl pre-kernel PRIMASK preservation and runtime frozen arbitration: PASS");
}
