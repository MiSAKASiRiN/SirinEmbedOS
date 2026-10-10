#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/CAN/can_controller_internal.h"
#include "fake_hal.h"

static void configure(unsigned mask, uint32_t nominal, uint32_t data, bool brs)
{
    SRNOS_CANControllerConfig c[4];
    for (unsigned i=0;i<4;++i) {
        const bool fd=i<3u && (mask & (1u<<i));
        c[i]=(SRNOS_CANControllerConfig){fd?SRNOS_CAN_MODE_FD:SRNOS_CAN_MODE_CLASSIC,
            nominal,fd?data:0u,fd&&brs};
    }
    assert(SRNOS_CANControllerPrepare(c));
    fake_config_frozen=true;
}

static uint32_t ram_words(const FDCAN_InitTypeDef *i)
{
    return i->StdFiltersNbr + i->ExtFiltersNbr*2u +
        i->RxFifo0ElmtsNbr*i->RxFifo0ElmtSize + i->RxFifo1ElmtsNbr*i->RxFifo1ElmtSize +
        i->RxBuffersNbr*i->RxBufferSize + i->TxEventsNbr*2u +
        (i->TxBuffersNbr+i->TxFifoQueueElmtsNbr)*i->TxElmtSize;
}

static void verify_profiles(unsigned mask)
{
    uint32_t previous_end=0u;
    for (unsigned n=0;n<3;++n) {
        const FDCAN_InitTypeDef *i=&fake_handles[n].Init;
        const SRNOS_CANControllerConfig *c=SRNOS_CANControllerConfigOf((uint8_t)(n+1u));
        const bool fd=(mask & (1u<<n))!=0u;
        assert(i->MessageRAMOffset==n*0x300u && i->MessageRAMOffset>=previous_end);
        assert(ram_words(i)==(fd?676u:232u));
        previous_end=i->MessageRAMOffset+ram_words(i);
        assert(previous_end<(n+1u)*0x300u && previous_end<2560u);
        assert(i->FrameFormat==(fd?(c->brs?FDCAN_FRAME_FD_BRS:FDCAN_FRAME_FD_NO_BRS):FDCAN_FRAME_CLASSIC));
        assert(i->NominalTimeSeg1==(fd?63u:60u)&&i->NominalTimeSeg2==(fd?16u:19u)&&i->NominalSyncJumpWidth==13u);
        assert(80000000u==c->nominal_bitrate*i->NominalPrescaler*(1u+i->NominalTimeSeg1+i->NominalTimeSeg2));
        const uint32_t data_rate=fd?c->data_bitrate:c->nominal_bitrate;
        assert(80000000u==data_rate*i->DataPrescaler*(1u+i->DataTimeSeg1+i->DataTimeSeg2));
        if(fd)assert((1u+i->DataTimeSeg1)*100u==(data_rate==5000000u?75u:80u)*(1u+i->DataTimeSeg1+i->DataTimeSeg2));
        assert(i->DataPrescaler>=1u&&i->DataPrescaler<=32u);
        assert(i->DataTimeSeg1>=1u&&i->DataTimeSeg1<=32u&&i->DataTimeSeg2>=1u&&i->DataTimeSeg2<=16u);
        assert(i->DataSyncJumpWidth<=i->DataTimeSeg2);
        assert(tdc_enabled[n]==(fd&&c->brs&&c->data_bitrate>1000000u));
        if(tdc_enabled[n])assert(tdc_offset[n]==i->DataPrescaler*i->DataTimeSeg1&&tdc_filter[n]==0u);
    }
}

static void verify_payloads(unsigned mask)
{
    const uint8_t lengths[16]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    for(uint8_t bus=1u;bus<=3u;++bus) {
        SRNOS_CANFDFrame frame={.id=0x18abcdefu,.flags=SRNOS_CANFD_FRAME_FLAG_EXT|SRNOS_CANFD_FRAME_FLAG_BRS};
        for(unsigned k=0;k<64;++k)frame.data[k]=(uint8_t)(k^0x5au);
        fake_registers[bus-1u].TXFQS=32u;
        if(!(mask&(1u<<(bus-1u)))) {
            assert(!SRNOS_FDCANTxPushFD(bus,&frame));
            assert(!SRNOS_FDCANTxPush(bus,1u,frame.data,9u));
            assert(SRNOS_FDCANTxPush(bus,1u,frame.data,1u));
            assert(captured_data[0]==frame.data[0]&&captured_data[1]==0u&&captured_data[2]==0u&&captured_data[3]==0u);
            continue;
        }
        assert(!SRNOS_FDCANTxPush(bus,1u,frame.data,8u));
        for(unsigned dlc=0;dlc<16;++dlc) {
            frame.length=lengths[dlc];
            assert(SRNOS_FDCANTxPushFD(bus,&frame));
            assert(captured_header.DataLength==dlc&&captured_header.FDFormat==FDCAN_FD_CAN);
            assert(captured_header.BitRateSwitch==FDCAN_BRS_ON&&captured_header.IdType==FDCAN_EXTENDED_ID);
            assert(captured_header.Identifier==frame.id&&captured_header.TxEventFifoControl==FDCAN_STORE_TX_EVENTS);
            assert(memcmp(captured_data,frame.data,frame.length)==0);
            for(unsigned k=frame.length;k<((frame.length+3u)&~3u);++k)assert(captured_data[k]==0u);
        }
        const unsigned accepted=tx_calls;
        frame.length=9u;assert(!SRNOS_FDCANTxPushFD(bus,&frame));
        frame.length=8u;frame.flags=SRNOS_CANFD_FRAME_FLAG_EXT;assert(!SRNOS_FDCANTxPushFD(bus,&frame));
        frame.flags|=SRNOS_CANFD_FRAME_FLAG_BRS|SRNOS_CANFD_FRAME_FLAG_ESI;assert(!SRNOS_FDCANTxPushFD(bus,&frame));
        assert(tx_calls==accepted);
        custom_rx=true;custom_rx_header=(FDCAN_RxHeaderTypeDef){.Identifier=0x18abcdefu,.DataLength=15u,
            .IdType=FDCAN_EXTENDED_ID,.FDFormat=FDCAN_FD_CAN,.BitRateSwitch=FDCAN_BRS_ON,.ErrorStateIndicator=FDCAN_ESI_PASSIVE};
        rx_remaining=1u;const uint32_t seq=rx_sequence;
        fake_isr=1u;HAL_FDCAN_RxFifo0Callback(&fake_handles[bus-1u],FDCAN_IT_RX_FIFO0_NEW_MESSAGE);fake_isr=0u;
        struct {SRNOS_FDCANRxFrame frame;uint32_t canary;} out={.canary=0xdeadbeefu};
        assert(SRNOS_FDCANRxPop(bus,&out.frame,1u)==1u&&out.canary==0xdeadbeefu);
        assert(out.frame.hdr.DataLength==15u&&out.frame.hdr.FDFormat==FDCAN_FD_CAN);
        for(unsigned k=0;k<64;++k)assert(out.frame.data[k]==(uint8_t)(seq+k));
    }
}

static void verify_recovery(void)
{
    const FDCAN_InitTypeDef before=fake_handles[0].Init;
    const uint32_t offset=tdc_offset[0];const bool enabled=tdc_enabled[0];
    fake_registers[0].PSR=FDCAN_PSR_BO;fake_registers[0].CCCR|=FDCAN_CCCR_INIT;
    fake_isr=1u;HAL_FDCAN_ErrorStatusCallback(&fake_handles[0],FDCAN_IT_BUS_OFF);fake_isr=0u;
    assert(!SRNOS_FDCANService(1u,0u));clear_bo_on_start=true;
    assert(SRNOS_FDCANService(1u,100u));
    assert(memcmp(&before,&fake_handles[0].Init,sizeof(before))==0&&init_calls==3u);
    assert(tdc_offset[0]==offset&&tdc_enabled[0]==enabled);
}

int main(int argc,char **argv)
{
    assert(argc>=3);
    assert(!SRNOS_FDCANStart(1u)&&start_calls==0u);
    if(strcmp(argv[1],"failure")==0) {
        configure(7u,1000000u,5000000u,true);
        if(strcmp(argv[2],"clock")==0)fake_clock=25000000u;
        else if(strcmp(argv[2],"ccu")==0)fake_ccu.CCFG=FDCANCCU_CCFG_CDIV;
        else if(strcmp(argv[2],"unfrozen")==0)fake_config_frozen=false;
        else if(strcmp(argv[2],"init")==0)fail_init_bus=2u;
        else if(strcmp(argv[2],"tdc")==0)fail_tdc_bus=2u;
        else if(strcmp(argv[2],"filter")==0)fail_filter_bus=2u;
        else assert(0);
        assert(!SRNOS_FDCANInitAll());
        assert(!SRNOS_FDCANStart(2u)&&!SRNOS_FDCANIsReady(2u)&&start_calls==0u);
        assert(!SRNOS_FDCANService(2u,1000u));
        const unsigned calls=init_calls;
        fail_init_bus=fail_tdc_bus=fail_filter_bus=0u;fake_clock=80000000u;fake_ccu.CCFG=0u;fake_config_frozen=true;
        assert(!SRNOS_FDCANInitAll()&&init_calls==calls&&!SRNOS_FDCANStart(2u));
        assert(!SRNOS_FDCANStartAll());
        assert(start_calls==((strcmp(argv[2],"clock")==0||strcmp(argv[2],"ccu")==0||strcmp(argv[2],"unfrozen")==0)?0u:2u));
        return 0;
    }
    const unsigned mask=(unsigned)strtoul(argv[2],NULL,0);
    if(strcmp(argv[1],"rate")==0) {
        assert(argc==6);
        configure(mask,(uint32_t)strtoul(argv[3],NULL,0),(uint32_t)strtoul(argv[4],NULL,0),atoi(argv[5])!=0);
    } else configure(mask,1000000u,5000000u,true);
    assert(SRNOS_FDCANInitAll()&&init_calls==3u&&start_calls==0u);
    verify_profiles(mask);
    assert(SRNOS_FDCANStartAll()&&start_calls==3u);
    if(strcmp(argv[1],"matrix")==0) {verify_payloads(mask);verify_recovery();}
    return 0;
}
