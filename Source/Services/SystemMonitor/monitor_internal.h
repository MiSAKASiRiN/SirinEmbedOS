#pragma once
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include "../../Diagnostics/Runtime/runtime_internal.h"
typedef struct { uint16_t vref, temperature_30, temperature_130; } SRNOS_MonitorCalibration;
typedef struct { uint64_t cycles, runtime; uint32_t generation; bool valid; } SRNOS_MonitorBaseline;
void SRNOS_MonitorConvert(SRNOS_MonitorADC *,const uint16_t raw[5],const SRNOS_MonitorCalibration *,uint64_t start,uint64_t end);
void SRNOS_MonitorTaskUpdate(SRNOS_MonitorTask *,SRNOS_MonitorBaseline *,const SRNOS_RuntimeSample *,uint32_t hz);
uint64_t SRNOS_MonitorNextDeadline(uint64_t deadline,uint64_t now,uint32_t *skipped);
bool SRNOS_SystemMonitorPrepare(bool enabled);
bool SRNOS_SystemMonitorStart(void);
void SRNOS_MonitorPublish(const SRNOS_SystemMonitorSnapshot *);
void SRNOS_MonitorPublishTask(const SRNOS_MonitorTask *);
uint32_t SRNOS_MonitorMaxCopyCycles(void);
