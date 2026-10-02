#pragma once
/* Component-only adapter. Lifecycle/pool behavior is tested with real kernel
 * implementation in Tests/Kernel; these suites keep their existing fake RTOS. */
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_KernelThreadNew osThreadNew
typedef struct {void (*step)(void *);bool (*wait)(void *);void *context;uint32_t period_ms;bool wait_first;} SRNOS_InternalLoop;
static inline void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop)
{
 bool first=true;for(;;){if((!first||loop->wait_first)&&loop->wait&&!loop->wait(loop->context))return;loop->step(loop->context);first=false;}
}
#ifdef __cplusplus
}
template<class Step,class Wait>
static inline void SRNOS_KernelRunCpp(Step step,Wait wait,uint32_t period=0,bool first=true)
{
 struct C {Step *s;Wait *w;};C c{&step,&wait};
 const SRNOS_InternalLoop loop={[](void *p){auto *v=static_cast<C *>(p);(*v->s)();},[](void *p){auto *v=static_cast<C *>(p);return (*v->w)();},&c,period,first};
 SRNOS_KernelRunLoop(&loop);
}
#endif
