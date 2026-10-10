#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../../../Source/IO/CAN/can_controller.c"
#include "../../../../Source/IO/CAN/External/exfdcan_device.cpp"
uint32_t SRNOS_TestPrimask, SRNOS_TestIpsr;
static GPIO_TypeDef gpio;
static SRNOS_ExternalCANDevCfg binding={0,SRNOS_SPI_BUS_4,&gpio,1,2,4,8};
struct task_state {uint32_t flags;};
static task_state owner;
static TaskHandle_t current;
static unsigned isr_notifications,task_notifications;
static uint32_t now_ms;
static bool inject_registration_irq;
void SRNOS_TestUnmask(void) {
    if(!inject_registration_irq)return;
    inject_registration_irq=false;
    SRNOS_TestIpsr=16;SRNOS_ExternalCANOnExti(binding.int_pin);SRNOS_TestIpsr=0;
}
extern "C" uint32_t HAL_GetTick(void) { return now_ms; }
extern "C" const SRNOS_ExternalCANDevCfg *SRNOS_BoardExternalCANConfig(uint8_t dev) { return dev?nullptr:&binding; }
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
extern "C" BaseType_t xTaskNotify(TaskHandle_t target,uint32_t flags,eNotifyAction action) {
    assert(!SRNOS_TestIpsr && action==eSetBits);++task_notifications;
    static_cast<task_state*>(target)->flags|=flags;return pdPASS;
}
extern "C" BaseType_t xTaskNotifyFromISR(TaskHandle_t target,uint32_t flags,eNotifyAction action,BaseType_t *woken) {
    assert(SRNOS_TestIpsr && action==eSetBits);++isr_notifications;
    static_cast<task_state*>(target)->flags|=flags;*woken=pdTRUE;return pdPASS;
}
extern "C" BaseType_t xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,TickType_t) { assert(false);return pdFALSE; }
int osKernelGetState(void) { return osKernelRunning; }
uint32_t osKernelGetTickFreq(void) { return 1000; }
extern "C" void SRNOS_CANFDCorePrepareResources(void) {}
int main() {
    SRNOS_CAN_PrepareResources();
    auto *ctx=SRNOS_ExternalCANCtxOf(0);
    SRNOS_TestIpsr=16;
    SRNOS_ExternalCANOnExti(binding.int1_pin);SRNOS_ExternalCANOnExti(binding.int_pin);
    SRNOS_TestIpsr=0;
    SRNOS_ExternalCANNotify(ctx,SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER);
    assert(!isr_notifications && !task_notifications && !ctx->task);
    current=&owner;inject_registration_irq=true;SRNOS_ExternalCANRegisterWorker(ctx);
    assert(owner.flags==(SRNOS_EXTERNAL_CAN_EVT_RX|SRNOS_EXTERNAL_CAN_EVT_INT|SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER));
    assert(!ctx->pending && isr_notifications==1 && task_notifications==1);
    owner.flags=0;SRNOS_TestIpsr=16;SRNOS_ExternalCANOnExti(binding.int0_pin);SRNOS_TestIpsr=0;
    assert(owner.flags==SRNOS_EXTERNAL_CAN_EVT_TX);
    SRNOS_ExternalCANNotify(ctx,SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER);
    assert(owner.flags==(SRNOS_EXTERNAL_CAN_EVT_TX|SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER));
    puts("CAN4 single-owner notification: pre-start events and publication/IRQ race preserved: PASS");
}
