#include "adapter.h"
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/can_board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"

/* Test-only native observation. Never installed in a parent application's
 * include requirements. ECR is sampled only at stage boundaries. */
void SRNOS_TestCANHardwareRead(unsigned bus, SRNOS_TestCANHardware *out)
{
    FDCAN_HandleTypeDef *h = SRNOS_BoardCANHandle(bus);
    SRNOS_FDCANDiag d = SRNOS_FDCANGetDiag(bus);
    FDCAN_ErrorCountersTypeDef e;
    (void)HAL_FDCAN_GetErrorCounters(h, &e);
    *out = (SRNOS_TestCANHardware){
        d.rx_isr_cnt, d.rx_overrun_cnt, d.tx_ok_cnt, d.tx_busy_cnt,
        d.bus_off_cnt, e.TxErrorCnt, e.RxErrorCnt, e.ErrorLogging,
        h->Instance->TXBRP,
        (h->Instance->RXF0S & FDCAN_RXF0S_RF0L) != 0,
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) /
        (h->Init.NominalPrescaler * (1 + h->Init.NominalTimeSeg1 + h->Init.NominalTimeSeg2))
    };
}
void SRNOS_TestIdleRead(uint64_t *cycles, uint64_t *idle)
{
    TaskStatus_t s;
    taskENTER_CRITICAL();
    *cycles = SRNOS_TimeGetUptimeCycles64();
    vTaskGetInfo(xTaskGetIdleTaskHandle(), &s, pdFALSE, eInvalid);
    *idle = s.ulRunTimeCounter;
    taskEXIT_CRITICAL();
}
