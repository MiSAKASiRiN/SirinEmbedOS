#pragma once
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_MONITOR_STREAM_TASKS 0x0001u
#define SRNOS_MONITOR_STREAM_ADC 0x0002u
#define SRNOS_MONITOR_STREAM_CYCLES 0x0003u
#define SRNOS_MONITOR_CYCLE_RECORD_BYTES 112u
#define SRNOS_MONITOR_WIRE_VERSION 2u
#define SRNOS_MONITOR_ADC_RECORD_BYTES 128u
#define SRNOS_MONITOR_TASK_RECORD_BYTES 112u
SRNOS_SirinLinkResult SRNOS_SystemMonitorTelemetryRegister(SRNOS_SirinLinkRegistration *,void *);
bool SRNOS_MonitorEncodeSnapshot(const SRNOS_SystemMonitorSnapshot *,uint8_t *,size_t);
bool SRNOS_MonitorEncodeTask(const SRNOS_SystemMonitorSnapshot *,const SRNOS_MonitorTask *,uint8_t *,size_t);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
bool SRNOS_MonitorEncodeCycle(const SRNOS_SystemMonitorSnapshot *snapshot,const SRNOS_MonitorTask *,uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
