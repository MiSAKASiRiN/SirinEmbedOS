#include "adapter.h"
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/can_board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include <string.h>

void SRNOS_TestCANHardwareRead(unsigned bus, SRNOS_TestCANHardware *out)
{
    FDCAN_HandleTypeDef *h=SRNOS_BoardCANHandle(bus);
    SRNOS_FDCANDiag d=SRNOS_FDCANGetDiag(bus);
    FDCAN_ErrorCountersTypeDef e={0};
    (void)HAL_FDCAN_GetErrorCounters(h,&e); /* ECR sampled only at stage boundaries. */
    uint32_t hz=HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
    *out=(SRNOS_TestCANHardware){
        d.rx_isr_cnt,d.rx_overrun_cnt,d.rx_hw_lost_cnt,d.tx_ok_cnt,d.tx_busy_cnt,d.bus_off_cnt,
        e.TxErrorCnt,e.RxErrorCnt,e.ErrorLogging,h->Instance->TXBRP,
        (h->Instance->RXF0S&FDCAN_RXF0S_RF0L)!=0,
        hz,hz/(h->Init.NominalPrescaler*(1+h->Init.NominalTimeSeg1+h->Init.NominalTimeSeg2)),
        hz/(h->Init.DataPrescaler*(1+h->Init.DataTimeSeg1+h->Init.DataTimeSeg2)),h->Init.FrameFormat,
        h->Instance->NBTP,h->Instance->DBTP,h->Instance->TDCR,h->Instance->CCCR,h->Instance->PSR
    };
}
void SRNOS_TestIdleRead(uint64_t *cycles,uint64_t *idle)
{
    TaskStatus_t s;
    taskENTER_CRITICAL();
    *cycles=SRNOS_TimeGetUptimeCycles64();
    vTaskGetInfo(xTaskGetIdleTaskHandle(),&s,pdFALSE,eInvalid);
    *idle=s.ulRunTimeCounter;
    taskEXIT_CRITICAL();
}
unsigned SRNOS_TestCANStacks(SRNOS_TestCANStack *out,unsigned capacity)
{
    /* Explicitly outside measured traffic. FreeRTOS scans stack fill bytes
     * while the scheduler is suspended; this must not become a live monitor. */
    static TaskStatus_t tasks[48];
    UBaseType_t count=uxTaskGetSystemState(tasks,48,NULL);
    if(count>capacity)count=capacity;
    for(UBaseType_t i=0;i<count;++i) {
        memset(out[i].name,0,sizeof(out[i].name));
        strncpy(out[i].name,tasks[i].pcTaskName,sizeof(out[i].name)-1);
        out[i].free_bytes=tasks[i].usStackHighWaterMark*sizeof(StackType_t);
        out[i].priority=tasks[i].uxCurrentPriority;
    }
    return count;
}
