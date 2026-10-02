#include "thread_config.h"
#include <string.h>
bool SRNOS_KernelThreadConfigValid(const SRNOS_ThreadConfig *c)
{
    if(!c || !c->name || !c->name[0] || strnlen(c->name,16)>=16 || !strncmp(c->name,"SRNOS_",6) ||
       !c->step || c->stack_bytes<1024 || c->stack_bytes>32768 || c->stack_bytes%8 ||
       !c->priority || c->priority>=40 || (unsigned)c->mode>SRNOS_TASK_EVENT)return false;
    if(c->mode==SRNOS_TASK_PERIODIC && (!c->period_ms || c->period_ms>INT32_MAX))return false;
    if(c->mode==SRNOS_TASK_EVENT && (!c->event_mask || (c->event_mask&~0x7fffffffu) ||
       !c->event_timeout_ms || (c->event_timeout_ms>INT32_MAX && c->event_timeout_ms!=SRNOS_WAIT_FOREVER)))return false;
    return true;
}
