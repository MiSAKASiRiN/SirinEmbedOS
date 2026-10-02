#include <assert.h>
#include <stdio.h>
#include "Devices/MCP2518FD/spi_adapter_internal.h"
#include "IO/can_board.h"
#include "main.h"
#include "fdcan.h"
GPIO_TypeDef mcp_gpio, explicit_gpio;
FDCAN_HandleTypeDef hfdcan1,hfdcan2,hfdcan3;
static unsigned prepares, transfers;
static GPIO_TypeDef *seen_port;
static uint16_t seen_pin;
uint32_t HAL_GetTick(void) { return 0; }
int osKernelGetState(void) { return 0; }
void osDelay(uint32_t ms) { (void)ms; }
void SRNOS_ExternalCANOnExti(uint16_t pin) { (void)pin; }
SRNOS_SPIStatus SRNOS_SPIDeviceInit(SRNOS_SPIDevice *d,SRNOS_SPIBusId bus,GPIO_TypeDef *port,uint16_t pin) {
    assert(bus==SRNOS_SPI_BUS_4); ++prepares; d->cs_port=port; d->cs_pin=pin; return SRNOS_SPI_OK;
}
SRNOS_SPIStatus SRNOS_SPITransferEx(SRNOS_SPIDevice *d,const uint8_t *tx,uint8_t *rx,size_t n,SRNOS_SPICallback cb,void *ctx,uint8_t blocking,uint32_t ms) {
    assert(n==1 && ms==5 && !cb && !ctx && blocking==1); ++transfers; seen_port=d->cs_port; seen_pin=d->cs_pin; rx[0]=tx[0]; return SRNOS_SPI_OK;
}
void SRNOS_SPIGetStatistics(const SRNOS_SPIDevice *d,SRNOS_SPIStatistics *s) { (void)d; (void)s; }
int main(void) {
    const SRNOS_ExternalCANDevCfg *c=SRNOS_BoardExternalCANConfig(0);
    assert(c && c->bus_id==SRNOS_SPI_BUS_4 && c->cs_port==MCP_CS_GPIO_Port && c->cs_pin==MCP_CS_Pin);
    assert(c->int_pin==MCP1_INT_Pin && c->int0_pin==MCP1_INT0_Pin && c->int1_pin==MCP1_INT1_Pin);
    assert(!SRNOS_BoardExternalCANConfig(1) && !SRNOS_BoardExternalCANConfig(255));
    assert(SRNOS_BoardCANHandle(1)==&hfdcan1 && SRNOS_BoardCANHandle(2)==&hfdcan2 && SRNOS_BoardCANHandle(3)==&hfdcan3);
    assert(!SRNOS_BoardCANHandle(0) && !SRNOS_BoardCANHandle(4));
    uint8_t tx=42,rx=0;
    assert(DRV_SPI_TransferData(0,&tx,&rx,1)==0 && rx==tx);
    assert(prepares==1 && transfers==1 && seen_port==c->cs_port && seen_pin==c->cs_pin);
    assert(DRV_SPI_TransferData(0,&tx,&rx,1)==0 && prepares==1);
    assert(DRV_SPI_TransferData(1,&tx,&rx,1)==-1 && prepares==1);
    assert(SRNOS_MCP2518FDAttachSPI(1,SRNOS_SPI_BUS_4,&explicit_gpio,123)==0);
    assert(DRV_SPI_TransferData(1,&tx,&rx,1)==0 && seen_port==&explicit_gpio && seen_pin==123);
    assert(SRNOS_MCP2518FDAttachSPI(0,SRNOS_SPI_BUS_4,&explicit_gpio,456)==0);
    assert(DRV_SPI_TransferData(0,&tx,&rx,1)==0 && seen_port==&explicit_gpio && seen_pin==456);
    assert(prepares==3 && transfers==4);
    assert(SRNOS_MCP2518FDAttachSPI(SRNOS_MCP2518FD_MAX_DEVICES,SRNOS_SPI_BUS_4,&explicit_gpio,1)==-1);
    assert(SRNOS_MCP2518FDAttachSPI(0,SRNOS_SPI_BUS_4,NULL,1)==-1);
    puts("MCP board binding: shared defaults, invalid IDs, explicit attach and override passed");
}
