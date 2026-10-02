#include <assert.h>
#include <stdio.h>
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
/* The host disables MCU sections; retain alignment without an ELF section.
 * The firmware ELF check independently verifies the actual D3 placement. */
#undef SRNOS_BDMA_BUF_BSS
#define SRNOS_BDMA_BUF_BSS __attribute__((aligned(32)))
#include "../../../../Board/Support/Devices/monitor_adc_board.c"
ADC_TypeDef fake_adc;
BDMA_Channel_TypeDef fake_dma;
static DMA_HandleTypeDef dma={&fake_dma,0,0};
ADC_HandleTypeDef hadc3={&fake_adc,&dma,0};
uint16_t fake_cal[3]={1500,800,1100};
uint32_t fake_ipsr;
static uint64_t now=100;
static unsigned critical,irq_mask,flags;
static bool calibration_ok=true,stop_ok=true,start_ok=true,hardware_stuck=false;
void test_enter(void) {++critical;}
void test_exit(void) {assert(critical);--critical;}
uint64_t SRNOS_TimeGetUptimeUs(void) {return now;}
void HAL_NVIC_DisableIRQ(int n) {assert(n==2);irq_mask=1;}
void HAL_NVIC_EnableIRQ(int n) {assert(n==2);assert(!(dma.interrupts&DMA_IT_HT));irq_mask=0;}
void HAL_NVIC_ClearPendingIRQ(int n) {assert(n==2);}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *a,uint32_t mode,uint32_t single)
{assert(!critical && a==&hadc3 && !mode && !single);return calibration_ok?HAL_OK:HAL_ERROR;}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *a,uint32_t *data,uint32_t count)
{
    assert(!critical && irq_mask && a==&hadc3 && count==5);
    assert((uintptr_t)data%32==0);fake_adc.CR=ADC_CR_ADEN|ADC_CR_ADSTART;fake_dma.CCR=BDMA_CCR_EN;
    dma.interrupts=DMA_IT_TC|DMA_IT_HT|DMA_IT_TE;
    uint16_t *samples=(uint16_t *)(void *)data;for(unsigned i=0;i<5;i++)samples[i]=(uint16_t)(100+i);
    return start_ok?HAL_OK:HAL_ERROR;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *a)
{
    assert(!critical && a==&hadc3);
    /* Real HAL leaves completed normal-mode BDMA EN set when state is READY. */
    if(!hardware_stuck)fake_adc.CR=0;
    return stop_ok?HAL_OK:HAL_ERROR;
}
void test_dma_disable(DMA_HandleTypeDef *h)
{assert(h==&dma && irq_mask);if(!hardware_stuck)fake_dma.CCR&=~BDMA_CCR_EN;}
uint32_t osThreadFlagsClear(uint32_t f) {flags&=~f;return 0;}
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) {assert(t==(void *)1);flags|=f;return flags;}
int main(void)
{
    uint16_t cal[3];assert(!SRNOS_BoardADCInitialize(NULL,cal));
    assert(SRNOS_BoardADCInitialize((void *)1,cal) && cal[0]==1500 && cal[2]==1100);
    assert(!memcmp(adc_samples,(uint16_t[5]){0},sizeof(adc_samples)));
    assert(SRNOS_BoardADCStart());assert(!SRNOS_BoardADCStart());
    assert(!irq_mask && !(dma.interrupts&DMA_IT_HT));
    SRNOS_BoardADCResult r;assert(!SRNOS_BoardADCTake(&r));
    now+=300;HAL_ADC_ConvCpltCallback(&hadc3);
    assert(flags==1 && SRNOS_BoardADCTake(&r) && !r.error && r.start_us==100 && r.end_us==400 && r.raw[4]==104);
    assert(fake_dma.CCR&BDMA_CCR_EN);
    assert(SRNOS_BoardADCStop());assert(!armed && !(fake_dma.CCR&BDMA_CCR_EN));
    flags=0;HAL_ADC_ErrorCallback(&hadc3);assert(flags==0);
    assert(SRNOS_BoardADCStart());fake_adc.ISR=ADC_FLAG_OVR;HAL_ADC_ConvCpltCallback(&hadc3);
    assert(SRNOS_BoardADCTake(&r) && r.error);fake_adc.ISR=0;
    hardware_stuck=true;assert(!SRNOS_BoardADCStop());assert(!armed && !dma.interrupts);
    hardware_stuck=false;stop_ok=false;assert(!SRNOS_BoardADCStop());stop_ok=true;
    calibration_ok=false;assert(!SRNOS_BoardADCInitialize((void *)1,cal));
    calibration_ok=true;start_ok=false;assert(!SRNOS_BoardADCStart());assert(SRNOS_BoardADCStop());
    hadc3.DMA_Handle=NULL;
    assert(!SRNOS_BoardADCInitialize((void *)1,cal));
    assert(!SRNOS_BoardADCStart() && !SRNOS_BoardADCStop());
    hadc3.DMA_Handle=&dma;dma.Instance=NULL;
    assert(!SRNOS_BoardADCStop());dma.Instance=&fake_dma;
    hadc3.Instance=NULL;assert(!SRNOS_BoardADCStop());hadc3.Instance=&fake_adc;
    assert(SRNOS_BoardADCStop());
    puts("PASS: actual ADC adapter calibration, aligned DMA, HT suppression, completion/OVR, verified stop, invalid binding and failed-stop isolation");
}
