#include "Devices/monitor_adc_board.h"
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "adc.h"
#include "stm32h7xx_ll_adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* Dedicated ownership; never share this memory with I2C4 BDMA. */
SRNOS_BDMA_BUF_BSS static uint16_t adc_samples[5];
static osThreadId_t owner;
static volatile uint32_t completion;
static volatile bool armed;
static uint64_t start_us;
static volatile uint64_t end_us;

static bool binding_valid(void)
{
    return hadc3.Instance==ADC3 && hadc3.DMA_Handle &&
           hadc3.DMA_Handle->Instance==(void *)BDMA_Channel2;
}
bool SRNOS_BoardADCStop(void)
{
    if (!binding_valid()) { armed=false; return false; }
    /* HAL stop may wait for hardware, but never with scheduler/interrupts
     * globally masked. No new buffer use until BOTH engines are stopped. */
    HAL_StatusTypeDef result=HAL_ADC_Stop_DMA(&hadc3);
    HAL_NVIC_DisableIRQ(BDMA_Channel2_IRQn);
    armed=false;
    /* In BDMA normal mode HAL marks the DMA READY on completion but leaves
     * CCR.EN set. ADC Stop_DMA then skips Abort because DMA is no longer BUSY.
     * Disable our dedicated channel after stopping the request source, and
     * verify the hardware readback before permitting buffer reuse. */
    __HAL_DMA_DISABLE(hadc3.DMA_Handle);
    __DSB();
    bool stopped=result==HAL_OK && !(ADC3->CR&(ADC_CR_ADSTART|ADC_CR_ADEN)) &&
                 !(BDMA_Channel2->CCR&BDMA_CCR_EN);
    __HAL_DMA_DISABLE_IT(hadc3.DMA_Handle,DMA_IT_TC|DMA_IT_HT|DMA_IT_TE);
    __HAL_DMA_CLEAR_FLAG(hadc3.DMA_Handle,__HAL_BDMA_GET_GI_FLAG_INDEX(hadc3.DMA_Handle));
    HAL_NVIC_ClearPendingIRQ(BDMA_Channel2_IRQn);
    HAL_NVIC_EnableIRQ(BDMA_Channel2_IRQn);
    return stopped;
}
bool SRNOS_BoardADCInitialize(osThreadId_t task,uint16_t cal[3])
{
    if (!task || !cal || !binding_valid()) return false;
    owner=task;
    memset(adc_samples,0,sizeof(adc_samples));
    cal[0]=*VREFINT_CAL_ADDR; cal[1]=*TEMPSENSOR_CAL1_ADDR; cal[2]=*TEMPSENSOR_CAL2_ADDR;
    return HAL_ADCEx_Calibration_Start(&hadc3,ADC_CALIB_OFFSET,ADC_SINGLE_ENDED)==HAL_OK;
}
bool SRNOS_BoardADCStart(void)
{
    if (!owner || armed || !binding_valid()) return false;
    (void)osThreadFlagsClear(SRNOS_BOARD_ADC_EVENT);
    /* Mask only this DMA IRQ while HAL enables HT; otherwise preemption
     * between Start_DMA and disabling HT could cause a half-transfer IRQ. */
    HAL_NVIC_DisableIRQ(BDMA_Channel2_IRQn);
    completion=0; end_us=0; start_us=SRNOS_TimeGetUptimeUs(); armed=true;
    HAL_StatusTypeDef result=HAL_ADC_Start_DMA(&hadc3,(uint32_t *)(void *)adc_samples,5);
    __HAL_DMA_DISABLE_IT(hadc3.DMA_Handle,DMA_IT_HT);
    HAL_NVIC_EnableIRQ(BDMA_Channel2_IRQn);
    return result==HAL_OK;
}
static void completed(ADC_HandleTypeDef *adc,bool error)
{
    if (adc!=&hadc3 || !armed) return;
    end_us=SRNOS_TimeGetUptimeUs();
    __DMB();
    completion=error?2u:1u;
    (void)osThreadFlagsSet(owner,SRNOS_BOARD_ADC_EVENT);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    completed(adc,(adc->ErrorCode!=HAL_ADC_ERROR_NONE)||(__HAL_ADC_GET_FLAG(adc,ADC_FLAG_OVR)!=0));
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc) { completed(adc,true); }
bool SRNOS_BoardADCTake(SRNOS_BoardADCResult *out)
{
    if (!out) return false;
    taskENTER_CRITICAL();
    uint32_t result=completion;
    if (result) {
        __DMB(); out->start_us=start_us; out->end_us=end_us; out->error=result!=1;
        memcpy(out->raw,adc_samples,sizeof(out->raw));
    }
    taskEXIT_CRITICAL();
    return result!=0;
}
