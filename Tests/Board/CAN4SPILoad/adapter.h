#pragma once
#include "../CANFDLoad/adapter.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t ok,mode,tec,rec,error_state,nbtp,dbtp,tdc,rx_status,tx_status;
} SRNOS_TestCAN4Hardware;
int SRNOS_TestCAN4Ready(void);
void SRNOS_TestCAN4PerformanceRead(uint32_t out[10]);
void SRNOS_TestCAN4HardwareRead(SRNOS_TestCAN4Hardware *out);
#ifdef __cplusplus
}
#endif
