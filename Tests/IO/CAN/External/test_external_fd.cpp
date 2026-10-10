#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include "../../../../Source/IO/CAN/can_controller.c"
#include "../../../../Source/IO/CAN/External/exfdcan_device.cpp"
#include "../../../../Source/IO/CAN/External/exfdcan_rx_router.cpp"
#include "../../../../Source/IO/CAN/External/exfdcan_tx_router.cpp"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_register.h"

/* Real device, TX/RX and vendor implementations; only the SPI register/RAM
 * boundary and RTOS/core endpoints are simulated. No replacement DLC/timing
 * or message-load algorithms are used by this test. */
uint32_t SRNOS_TestPrimask, SRNOS_TestIpsr;
static GPIO_TypeDef gpio;
static SRNOS_ExternalCANDevCfg binding={0,SRNOS_SPI_BUS_4,&gpio,1,2,4,8};
static uint8_t registers[4096];
static unsigned lock_depth,spi_calls,fail_spi_at,mode_reads,mode_delays,ack_after;
static unsigned classic_received,fd_received,classic_rejected,fd_rejected,tx_admissions;
static unsigned resource_prepares,rx_wakes,tx_completions;
static unsigned classic_waits,fd_waits,classic_drains,fd_drains,wait_calls;
static uint32_t worker_flags;
static uint32_t next_wait_delay=UINT32_MAX;
static TickType_t observed_wait;
static bool fail_after, auto_ack=true, refill_rx;
static unsigned rx_index,tx_index,tx_depth,yield_count;
static uint64_t clock_us;
static unsigned spi_cost_us=10;
static bool pump_tx, pump_irq;
static std::vector<char> service_order;
struct worker_complete {};
static uint32_t now_ms;
static bool frozen,refuse_config,refuse_final,force_tx_full;
static CAN_OPERATION_MODE pending_mode;
static const SRNOS_CANRouterBackendOps *classic_backend;
static const SRNOS_CANFDBackendOps *fd_backend;
static SRNOS_CANFDFrame last_fd;
static SRNOS_CANFrame last_classic;
struct wire_frame { CAN_RX_MSGOBJ object; uint8_t data[64]; };
struct tx_wire { CAN_TX_MSGOBJ object; uint8_t data[64]; uint8_t length; };
static std::deque<wire_frame> rx_wire;
static std::vector<tx_wire> tx_wire_log;
static constexpr unsigned rx_con=cREGADDR_CiFIFOCON+CAN_FIFO_CH1*CiFIFO_OFFSET;
static constexpr unsigned tx_con=cREGADDR_CiFIFOCON+CAN_FIFO_CH2*CiFIFO_OFFSET;
static uint32_t read_word(unsigned address) { uint32_t value;std::memcpy(&value,registers+address,4);return value; }
static void write_word(unsigned address,uint32_t value) { std::memcpy(registers+address,&value,4); }
static unsigned payload(unsigned con) { static const unsigned sizes[]={8,12,16,20,24,32,48,64};return sizes[read_word(con)>>29]; }
static unsigned depth(unsigned con) { return ((read_word(con)>>24)&31u)+1u; }
static void set_mode(CAN_OPERATION_MODE mode) { registers[cREGADDR_CiCON+2]=(registers[cREGADDR_CiCON+2]&0x1fu)|((uint8_t)mode<<5); }
static void chip_reset(void) {
    std::memset(registers,0,sizeof(registers));
    for(unsigned i=0;i<sizeof(canControlResetValues)/sizeof(canControlResetValues[0]);++i)write_word(i*4,canControlResetValues[i]);
    for(unsigned i=0;i<32;++i)for(unsigned j=0;j<3;++j)write_word(cREGADDR_CiFIFOCON+i*CiFIFO_OFFSET+j*4,canFifoResetValues[j]);
    pending_mode=CAN_CONFIGURATION_MODE;set_mode(CAN_CONFIGURATION_MODE);
}
static void update_fifo_registers(void) {
    registers[rx_con+4]=rx_wire.empty()?CAN_RX_FIFO_EMPTY:CAN_RX_FIFO_NOT_EMPTY;
    if(rx_wire.size()>=depth(rx_con)/2)registers[rx_con+4]|=CAN_RX_FIFO_HALF_FULL;
    registers[tx_con+4]=(force_tx_full || tx_depth==depth(tx_con))?CAN_TX_FIFO_FULL:CAN_TX_FIFO_NOT_FULL;
    write_word(rx_con+8,rx_index*(8u+payload(rx_con)));
    write_word(tx_con+8,depth(rx_con)*(8u+payload(rx_con))+tx_index*(8u+payload(tx_con)));
}
void SRNOS_TestUnmask(void) {}
extern "C" uint32_t HAL_GetTick(void) { return now_ms; }
extern "C" void HAL_Delay(uint32_t ms) { now_ms+=ms; }
extern "C" const SRNOS_ExternalCANDevCfg *SRNOS_BoardExternalCANConfig(uint8_t dev) { return dev?nullptr:&binding; }
extern "C" int8_t SRNOS_MCP2518FDAttachSPI(CANFDSPI_MODULE_ID dev,SRNOS_SPIBusId bus,GPIO_TypeDef *port,uint16_t pin) {
    assert(!lock_depth && !dev && bus==binding.bus_id && port==&gpio && pin==binding.cs_pin);return 0;
}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void*)0x1234; }
extern "C" BaseType_t xTaskNotify(TaskHandle_t,uint32_t bits,eNotifyAction) { worker_flags|=bits;return pdPASS; }
extern "C" BaseType_t xTaskNotifyFromISR(TaskHandle_t,uint32_t bits,eNotifyAction,BaseType_t *) { worker_flags|=bits;return pdPASS; }
extern "C" BaseType_t xTaskNotifyWait(uint32_t,uint32_t,uint32_t *flags,TickType_t ticks) {
    observed_wait=ticks;++wait_calls;*flags=worker_flags;worker_flags=0;return *flags?pdPASS:pdFALSE;
}
osMutexId_t osMutexNew(const void *) { return &lock_depth; }
int osMutexAcquire(osMutexId_t,uint32_t) { assert(!lock_depth);++lock_depth;return osOK; }
int osMutexRelease(osMutexId_t) { assert(lock_depth==1);--lock_depth;return osOK; }
int osKernelGetState(void) { return osKernelRunning; }
uint32_t osKernelGetTickFreq(void) { return 1000; }
int osThreadYield(void) { ++yield_count;return osOK; }
osThreadId_t osThreadGetId(void) { return (void*)2; }
extern "C" uint64_t SRNOS_TimeGetUptimeUs(void) { return clock_us; }
int osDelay(uint32_t ticks) { assert(ticks==1 && !lock_depth);++mode_delays;now_ms+=ticks;return osOK; }

extern "C" bool SRNOS_CAN_ConfigurationFrozen(void) { return frozen; }
extern "C" bool SRNOS_CANRouterCoreConfigurationCompatible(SRNOS_CANChannel,const SRNOS_CANControllerConfig *) { return true; }
extern "C" bool SRNOS_CANFDCoreHasRegistrations(SRNOS_CANChannel) { return false; }
extern "C" void SRNOS_CANFDCorePrepareResources(void) { ++resource_prepares; }
extern "C" void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel ch,const SRNOS_CANRouterBackendOps *ops) { assert(ch==4);classic_backend=ops; }
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel ch,const SRNOS_CANFDBackendOps *ops) { assert(ch==4);fd_backend=ops; }
extern "C" uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel,uint32_t) { ++classic_waits;return next_wait_delay; }
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel,uint32_t) { ++fd_waits;return next_wait_delay; }
extern "C" bool SRNOS_CANRouterCoreStepTx(SRNOS_CANChannel,uint32_t) { ++classic_drains;if(pump_tx)service_order.push_back('T');return pump_tx; }
extern "C" bool SRNOS_CANFDCoreStepTx(SRNOS_CANChannel,uint32_t) { ++fd_drains;if(pump_tx)service_order.push_back('T');return pump_tx; }
extern "C" void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel) { ++tx_completions; }
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel) { ++tx_completions; }
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel,bool) { ++rx_wakes; }
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel,bool) { ++rx_wakes; }
extern "C" void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel) { assert(!lock_depth);++classic_rejected; }
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel) { assert(!lock_depth);++fd_rejected; }
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel ch,const SRNOS_CANFrame *frame,uint32_t stamp) {
    assert(!lock_depth && ch==4 && stamp==now_ms);last_classic=*frame;++classic_received;service_order.push_back('R');
}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel ch,const SRNOS_CANFDFrame *frame,uint32_t stamp) {
    assert(!lock_depth && ch==4 && stamp==now_ms);last_fd=*frame;++fd_received;service_order.push_back('R');
}

extern "C" int8_t DRV_SPI_TransferData(CANFDSPI_MODULE_ID dev,uint8_t *tx,uint8_t *rx,uint16_t size) {
    assert(!dev && !lock_depth && size>=2 && size<=256);
    ++spi_calls;clock_us+=spi_cost_us;
    if(pump_irq)SRNOS_ExternalCANOnExti(binding.int1_pin);
    if(spi_calls==fail_spi_at && !fail_after)return -1;
    std::memset(rx,0,size);
    unsigned instruction=tx[0]>>4,address=((tx[0]&15u)<<8)|tx[1],length=size-2u;
    if(instruction==cINSTRUCTION_RESET) { assert(size==2);chip_reset();return 0; }
    assert(address+length<=sizeof(registers));
    update_fifo_registers();
    if(instruction==cINSTRUCTION_READ) {
        if(address==cREGADDR_CiCON+2 && length==1) {
            ++mode_reads;
            const bool refuse=pending_mode==CAN_CONFIGURATION_MODE?refuse_config:refuse_final;
            if(!refuse && mode_reads>=ack_after)set_mode(pending_mode);
        }
        if(address==tx_con && length==8)++tx_admissions;
        if(address>=cRAMADDR_START && address<cRAMADDR_END) {
            assert(!rx_wire.empty());unsigned ram=cRAMADDR_START+read_word(rx_con+8);
            assert(address>=ram && address+length<=ram+8u+payload(rx_con));
            std::memcpy(registers+ram,rx_wire.front().object.byte,8);
            std::memcpy(registers+ram+8,rx_wire.front().data,payload(rx_con));
        }
        std::memcpy(rx+2,registers+address,length);
    } else {
        assert(instruction==cINSTRUCTION_WRITE);
        if(address>=cRAMADDR_START && address<cRAMADDR_END) {
            assert(address==cRAMADDR_START+read_word(tx_con+8));
            assert(length<=8u+payload(tx_con) && address+length<=cRAMADDR_END);
        }
        std::memcpy(registers+address,tx+2,length);
        if(address<=cREGADDR_CiCON+3 && address+length>cREGADDR_CiCON+3) {
            pending_mode=(CAN_OPERATION_MODE)(registers[cREGADDR_CiCON+3]&7u);mode_reads=0;
            if(pending_mode==CAN_CONFIGURATION_MODE && refuse_config)set_mode(CAN_RESTRICTED_MODE);
            else if(pending_mode!=CAN_CONFIGURATION_MODE)set_mode(CAN_CONFIGURATION_MODE);
        }
        if(address==rx_con+1 && length==1 && (tx[2]&1u)) {
            assert(!rx_wire.empty());auto item=rx_wire.front();rx_wire.pop_front();
            rx_index=(rx_index+1)%depth(rx_con);if(refill_rx)rx_wire.push_back(item);
        }
        if(address==tx_con+1 && length==1 && (tx[2]&1u)) {
            tx_wire item{};unsigned ram=cRAMADDR_START+read_word(tx_con+8);
            std::memcpy(item.object.byte,registers+ram,8);
            item.length=(uint8_t)DRV_CANFDSPI_DlcToDataBytes((CAN_DLC)item.object.bF.ctrl.DLC);
            assert(item.length<=payload(tx_con));std::memcpy(item.data,registers+ram+8,item.length);
            assert((tx[2]&3u)==3u && tx_depth<depth(tx_con));
            tx_wire_log.push_back(item);tx_index=(tx_index+1)%depth(tx_con);
            if(!auto_ack)++tx_depth;
        }
    }
    return spi_calls==fail_spi_at?-1:0;
}

static const SRNOS_CANControllerConfig classic={SRNOS_CAN_MODE_CLASSIC,1000000u,0u,false};
static const SRNOS_CANControllerConfig fd={SRNOS_CAN_MODE_FD,1000000u,2000000u,true};
static SRNOS_ExternalCANCtx *reset(const SRNOS_CANControllerConfig &config) {
    assert(!lock_depth);s_ctx[0]={};s_hardware_requested=false;s_dbg_initialized[0]=0;
    spi_calls=fail_spi_at=mode_reads=mode_delays=0;ack_after=1;
    classic_received=fd_received=classic_rejected=fd_rejected=tx_admissions=0;
    now_ms=0;refuse_config=refuse_final=force_tx_full=false;
    fail_after=refill_rx=false;auto_ack=true;rx_index=tx_index=tx_depth=yield_count=0;clock_us=0;worker_flags=0;classic_waits=fd_waits=classic_drains=fd_drains=wait_calls=0;
    next_wait_delay=UINT32_MAX;observed_wait=0;
    pump_tx=pump_irq=false;spi_cost_us=10;service_order.clear();
    rx_wire.clear();tx_wire_log.clear();classic_backend=nullptr;fd_backend=nullptr;
    frozen=prepared=false;
    SRNOS_CANControllerConfig configs[4]={classic,classic,classic,config};
    assert(SRNOS_CANControllerPrepare(configs));frozen=true;
    return SRNOS_ExternalCANCtxOf(0);
}
static uint32_t nominal_rate(void) {
    REG_CiNBTCFG reg;reg.word=read_word(cREGADDR_CiNBTCFG);
    return 40000000u/((reg.bF.BRP+1u)*(reg.bF.TSEG1+reg.bF.TSEG2+3u));
}
static uint32_t data_rate(void) {
    REG_CiDBTCFG reg;reg.word=read_word(cREGADDR_CiDBTCFG);
    return 40000000u/((reg.bF.BRP+1u)*(reg.bF.TSEG1+reg.bF.TSEG2+3u));
}
static void check_init(const SRNOS_CANControllerConfig &config) {
    SRNOS_ExternalCANCtx *ctx=reset(config);
    assert(SRNOS_ExternalCANHwInit(0)==0 && ctx->init_done && !lock_depth);
    assert(nominal_rate()==config.nominal_bitrate);
    assert(data_rate()==(config.data_bitrate<500000u?1000000u:config.data_bitrate));
    REG_CiCON con;con.word=read_word(cREGADDR_CiCON);
    assert(!con.bF.TXQEnable && !con.bF.StoreInTEF && con.bF.BitRateSwitchDisable==!config.brs);
    assert(con.bF.OpMode==(config.mode==SRNOS_CAN_MODE_FD?CAN_NORMAL_MODE:CAN_CLASSIC_MODE));
    REG_CiFIFOCON rxf,txf;rxf.word=read_word(rx_con);txf.word=read_word(tx_con);
    assert(!rxf.rxBF.TxEnable && !rxf.rxBF.RxTimeStampEnable && txf.txBF.TxEnable && !txf.txBF.RTREnable);
    unsigned expected=config.mode==SRNOS_CAN_MODE_FD?64u:8u;
    assert(payload(rx_con)==expected && payload(tx_con)==expected);
    assert(depth(rx_con)==(expected==64?16u:32u));assert(depth(tx_con)==(expected==64?12u:32u));
    assert((depth(rx_con)+depth(tx_con))*(8u+expected)==(expected==64?2016u:1024u));
    unsigned before=spi_calls;assert(SRNOS_ExternalCANHwInit(0)==0 && spi_calls==before);
    SRNOS_ExternalCANBackendBind(0);
    assert((fd_backend!=nullptr)==(config.mode==SRNOS_CAN_MODE_FD));
    assert((classic_backend!=nullptr)==(config.mode==SRNOS_CAN_MODE_CLASSIC));
}
static void queue_rx(bool fdf,uint8_t dlc,bool ext=false,bool brs=false,bool esi=false,bool rtr=false) {
    wire_frame item{};item.object.bF.ctrl.FDF=fdf;item.object.bF.ctrl.DLC=dlc;
    item.object.bF.ctrl.IDE=ext;item.object.bF.ctrl.BRS=brs;item.object.bF.ctrl.ESI=esi;item.object.bF.ctrl.RTR=rtr;
    item.object.bF.id.SID=ext?0x456u:0x123u;item.object.bF.id.EID=ext?0x23456u:0;
    for(unsigned i=0;i<64;++i)item.data[i]=(uint8_t)(i^0xa5u);
    assert(rx_wire.size()<depth(rx_con));rx_wire.push_back(item);
}
static void poll(SRNOS_ExternalCANCtx *ctx) {
    for(unsigned i=0;i<32 && SRNOS_ExternalCANReceive(0,ctx,4)==1;++i){}
}
extern "C" SRNOS_SPIStatus SRNOS_MCP2518FDSequence(CANFDSPI_MODULE_ID dev,SRNOS_SPISequencePrepare fn,void *context) {
    for(unsigned i=0;i<=4;++i) {
        uint8_t tx[74],*rx=nullptr;int n=fn(context,i,tx,&rx);
        if(!n)return SRNOS_SPI_OK;
        if(n<0)return SRNOS_SPI_ERROR;
        assert(i<4 && n>0 && n<=74 && rx);
        if(DRV_SPI_TransferData(dev,tx,rx,(uint16_t)n))return SRNOS_SPI_ERROR;
    }
    assert(false);return SRNOS_SPI_ERROR;
}
static void check_frames(void) {
    check_init(fd);SRNOS_ExternalCANCtx *ctx=SRNOS_ExternalCANCtxOf(0);
    for(unsigned dlc=0;dlc<16;++dlc) {
        SRNOS_CANFDFrame frame{};frame.id=0x1234567;frame.length=SRNOS_CANFDDlcToLength((uint8_t)dlc);
        frame.flags=SRNOS_CANFD_FRAME_FLAG_EXT|SRNOS_CANFD_FRAME_FLAG_BRS;
        for(unsigned i=0;i<64;++i)frame.data[i]=(uint8_t)(i^0x5a);
        assert(fd_backend->get_tx_free(4)==3 && fd_backend->try_push(4,&frame));
        const tx_wire &wire=tx_wire_log.back();
        assert(wire.object.bF.ctrl.FDF && wire.object.bF.ctrl.BRS && wire.object.bF.ctrl.IDE && !wire.object.bF.ctrl.RTR);
        assert(wire.object.bF.ctrl.DLC==dlc && wire.length==frame.length);
        assert((((uint32_t)wire.object.bF.id.SID<<18)|wire.object.bF.id.EID)==frame.id);
        assert(!std::memcmp(wire.data,frame.data,frame.length));
        queue_rx(true,(uint8_t)dlc,true,dlc%2!=0,true);
        poll(ctx);
        assert(last_fd.id==((0x456u<<18)|0x23456u) && last_fd.length==frame.length);
        assert(last_fd.flags==(SRNOS_CANFD_FRAME_FLAG_EXT|SRNOS_CANFD_FRAME_FLAG_ESI|(dlc%2?SRNOS_CANFD_FRAME_FLAG_BRS:0)));
        for(unsigned i=0;i<64;++i)assert(last_fd.data[i]==(i<frame.length?(uint8_t)(i^0xa5u):0));
    }
    assert(fd_received==16 && !classic_received && ctx->stats->rx_frames==16);
    assert(!ctx->rx_head && !ctx->rx_tail); /* No truncation into Classic debug log. */
    queue_rx(false,8);queue_rx(true,8,false,false,false,true);
    poll(ctx);
    assert(fd_received==16 && fd_rejected==2 && ctx->stats->rx_errors==2);
    SRNOS_CANFDFrame bad{};bad.id=0x123;bad.flags=SRNOS_CANFD_FRAME_FLAG_BRS;
    unsigned before=spi_calls;
    for(unsigned length=9;length<64;++length)if(SRNOS_CANFDLengthToDlc((uint8_t)length)==0xff) {
        bad.length=(uint8_t)length;assert(!fd_backend->try_push(4,&bad));
    }
    bad.length=65;assert(!fd_backend->try_push(4,&bad));
    bad.length=8;bad.flags=0;assert(!fd_backend->try_push(4,&bad));
    bad.flags=SRNOS_CANFD_FRAME_FLAG_BRS|SRNOS_CANFD_FRAME_FLAG_ESI;assert(!fd_backend->try_push(4,&bad));
    bad.flags=SRNOS_CANFD_FRAME_FLAG_BRS;bad.id=0x800;assert(!fd_backend->try_push(4,&bad));
    SRNOS_CANFrame wrong{};wrong.id=0x123;wrong.dlc=8;
    assert(!SRNOS_ExternalCANBackendTryPush(4,&wrong,SRNOS_CAN_ROUTER_TX_CLASS_URGENT) && before==spi_calls);
    /* Finite RX FIFO, hardware refills only after consuming a slot. */
    for(unsigned i=0;i<16;++i)queue_rx(true,15);
    refill_rx=true;
    SRNOS_ExternalCANService service{0,ctx,4,true,true,true,0};
    SRNOS_ExternalCANServiceStep(service);
    assert(rx_wire.size()==16 && yield_count==1 && service.rx_ready);
    refill_rx=false;while(!rx_wire.empty())SRNOS_ExternalCANServiceStep(service);

    check_init(classic);ctx=SRNOS_ExternalCANCtxOf(0);
    for(unsigned length=0;length<=8;++length) {
        SRNOS_CANFrame frame{};frame.id=0x123;frame.dlc=(uint8_t)length;
        for(unsigned i=0;i<8;++i)frame.data[i]=(uint8_t)i;
        assert(classic_backend->get_tx_free(4)==3);
        assert(classic_backend->try_push(4,&frame,SRNOS_CAN_ROUTER_TX_CLASS_URGENT));
        const tx_wire &wire=tx_wire_log.back();assert(!wire.object.bF.ctrl.FDF && !wire.object.bF.ctrl.BRS && wire.length==length);
        assert(!std::memcmp(wire.data,frame.data,length));
    }
    queue_rx(true,0);queue_rx(true,8);queue_rx(true,15);queue_rx(false,8);
    poll(ctx);
    assert(classic_rejected==3 && classic_received==1 && !fd_received && last_classic.dlc==8);
    assert(ctx->stats->rx_errors==3 && ctx->stats->rx_frames==1 && ctx->rx_head==1);
    bad.id=0x123;assert(!SRNOS_ExternalCANFDBackendTryPush(4,&bad));

    SRNOS_CANControllerConfig no_brs={SRNOS_CAN_MODE_FD,125000u,125000u,false};
    check_init(no_brs);bad.flags=0;bad.length=64;
    assert(fd_backend->get_tx_free(4)==3 && fd_backend->try_push(4,&bad));
    assert(tx_wire_log.back().object.bF.ctrl.FDF && !tx_wire_log.back().object.bF.ctrl.BRS);
}
static void check_workers(void) {
    for(const auto &config:{classic,fd}) {
        check_init(config);auto *ctx=SRNOS_ExternalCANCtxOf(0);
        SRNOS_ExternalCANService service{0,ctx,4,config.mode==SRNOS_CAN_MODE_FD,true,true,0};
        queue_rx(service.fd,8);SRNOS_ExternalCANServiceStep(service);
        assert(rx_wire.empty() && !service.rx_ready && !service.tx_ready);
        worker_flags=SRNOS_EXTERNAL_CAN_EVT_RX;
        SRNOS_ExternalCANServiceWait(service);
        assert(service.rx_ready && observed_wait==5 && service.flags==SRNOS_EXTERNAL_CAN_EVT_RX);
        queue_rx(service.fd,8);SRNOS_ExternalCANServiceStep(service);
        assert(rx_wire.empty());
    }
}
static void check_service_budgets(void) {
    for(const auto &config:{classic,fd})for(bool congested:{false,true}) {
        check_init(config);auto *ctx=SRNOS_ExternalCANCtxOf(0);
        const bool is_fd=config.mode==SRNOS_CAN_MODE_FD;
        unsigned rx_count=congested?depth(rx_con)/2:4;
        for(unsigned i=0;i<rx_count;++i)queue_rx(is_fd,8);
        refill_rx=pump_tx=pump_irq=true;clock_us=0;
        SRNOS_ExternalCANRegisterWorker(ctx);
        // Initial cached status is empty: the first status read must select 8:1.
        assert(!ctx->rx_fifo.status);
        SRNOS_ExternalCANService service{0,ctx,4,is_fd,true,true,0};
        SRNOS_ExternalCANServiceStep(service);
        assert(service_order.size()==32 && yield_count==1 && service.rx_ready && service.tx_ready);
        for(unsigned i=0;i<32;++i)
            assert(service_order[i]==(congested?(i%9<8?'R':'T'):(i%8<4?'R':'T')));
        unsigned waits=wait_calls;SRNOS_ExternalCANServiceWait(service);assert(wait_calls==waits);
        // A slow frame consumes the time budget before the 32-frame limit.
        service_order.clear();spi_cost_us=100;clock_us=0;
        SRNOS_ExternalCANServiceStep(service);
        assert(yield_count==2 && service_order.size()==4 && clock_us==1200);
        for(char item:service_order)assert(item=='R');
    }
}
static void check_failures(void) {
    check_init(fd);unsigned startup_transfers=spi_calls;
    for(unsigned at=1;at<=startup_transfers;++at) {
        auto *ctx=reset(fd);fail_spi_at=at;SRNOS_ExternalCANWorker(nullptr);
        assert(!ctx->init_done && !ctx->task && !lock_depth && ctx->stats->init_errors);
    }
    auto *ctx=reset(fd);refuse_config=true;SRNOS_ExternalCANWorker(nullptr);
    assert(!ctx->init_done && !ctx->task && mode_delays==SRNOS_EXTERNAL_CAN_MODE_ATTEMPTS-1u);
    ctx=reset(fd);refuse_final=true;SRNOS_ExternalCANWorker(nullptr);
    assert(!ctx->init_done && !ctx->task && mode_delays==SRNOS_EXTERNAL_CAN_MODE_ATTEMPTS-1u);
    ctx=reset(fd);ack_after=3;assert(SRNOS_ExternalCANHwInit(0)==0 && mode_delays>=2);
    /* All TX/RX stages, failure before/after the physical write. For UINC,
     * retry requires an observed old UA; an advanced UA must not be replayed. */
    for(bool after:{false,true})for(unsigned stage=1;stage<=4;++stage) {
        check_init(fd);ctx=SRNOS_ExternalCANCtxOf(0);queue_rx(true,15);
        fail_after=after;fail_spi_at=spi_calls+stage;
        assert(SRNOS_ExternalCANReceive(0,ctx,4)==-1);
        assert(!fd_received);
        fail_spi_at=0;poll(ctx);assert(fd_received==1 && rx_wire.empty());
        if(stage<=3) {
            check_init(fd);ctx=SRNOS_ExternalCANCtxOf(0);
            SRNOS_CANFDFrame f{};f.id=0x123;f.length=64;f.flags=SRNOS_CANFD_FRAME_FLAG_BRS;
            fail_after=after;fail_spi_at=spi_calls+stage;
            bool owned=fd_backend->try_push(4,&f);assert(owned==(stage==3));
            fail_spi_at=0;
            if(owned) {
                assert(!fd_backend->get_tx_free(4));
                assert(SRNOS_MCPFifoReconcile(&ctx->tx_fifo));
            } else assert(fd_backend->try_push(4,&f));
            assert(tx_wire_log.size()==1);
        }
    }
    check_init(fd);ctx=SRNOS_ExternalCANCtxOf(0);auto_ack=false;
    SRNOS_CANFDFrame frame{};frame.id=1;frame.length=8;frame.flags=SRNOS_CANFD_FRAME_FLAG_BRS;
    for(unsigned round=0;round<4;++round) {
        for(unsigned i=0;i<12;++i)assert(fd_backend->try_push(4,&frame));
        assert(!fd_backend->try_push(4,&frame));tx_depth=0;
    }
    assert(tx_wire_log.size()==48); /* >3 complete TX pointer wraps */
    for(unsigned round=0;round<5;++round) {
        for(unsigned i=0;i<16;++i)queue_rx(true,15);
        poll(ctx);assert(rx_wire.empty());
    }
    assert(fd_received==80); /* five RX wraps */
    queue_rx(true,15);fail_spi_at=spi_calls+4;fail_after=true;
    assert(SRNOS_ExternalCANReceive(0,ctx,4)==-1);
    for(unsigned i=0;i<4;++i) {
        fail_spi_at=spi_calls+1;assert(SRNOS_ExternalCANReceive(0,ctx,4)==-1);
        assert(ctx->rx_fifo.pending && fd_received==80);
    }
    fail_spi_at=0;poll(ctx);assert(fd_received==81 && rx_wire.empty());
    // An unrelated UA cannot prove a commit outcome: quarantine without replay.
    for(bool transmit:{false,true}) {
        check_init(fd);ctx=SRNOS_ExternalCANCtxOf(0);fail_after=false;
        if(transmit) {
            fail_spi_at=spi_calls+3;assert(fd_backend->try_push(4,&frame));
            tx_index=2;
        } else {
            queue_rx(true,15);fail_spi_at=spi_calls+4;
            assert(SRNOS_ExternalCANReceive(0,ctx,4)==-1);rx_index=2;
        }
        fail_spi_at=0;auto *fifo=transmit?&ctx->tx_fifo:&ctx->rx_fifo;
        assert(!SRNOS_MCPFifoReconcile(fifo) && fifo->pending && fifo->quarantined);
        unsigned calls=spi_calls;assert(!SRNOS_MCPFifoReconcile(fifo) && spi_calls==calls);
        assert(tx_wire_log.empty() && !fd_received);
    }
}
int main(void) {
    SRNOS_CAN_PrepareResources();assert(resource_prepares==1);
    SRNOS_ExternalCANCtx *unfrozen=reset(fd);frozen=false;
    assert(SRNOS_ExternalCANHwInit(0)==-1 && !spi_calls && !unfrozen->init_done && !lock_depth);
    assert(!unfrozen->task);
    frozen=true;
    assert(SRNOS_ExternalCANHwInit(0)==0 && unfrozen->init_done);
    const uint32_t nominals[]={125000,250000,500000,1000000};
    const uint32_t datas[]={500000,1000000,2000000,4000000,5000000,8000000};
    for(uint32_t nominal:nominals) {
        check_init({SRNOS_CAN_MODE_CLASSIC,nominal,0,false});
        check_init({SRNOS_CAN_MODE_FD,nominal,nominal,false});
        for(uint32_t data:datas)if(data>=nominal)check_init({SRNOS_CAN_MODE_FD,nominal,data,true});
    }
    check_frames();check_workers();check_service_budgets();check_failures();
    puts("CAN4 real vendor/registers: Classic/FD mode handshake, exact independent timing, 2016B RAM, all DLC/64B TX+RX, strict mode filtering, failure/timeout/token retry: PASS");
}
