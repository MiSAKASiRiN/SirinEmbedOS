#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/Telemetry.h"
#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include "SirinEmbedOS/Services/INS.h"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace {
struct Source {SRNOS_TelemetryDescriptor descriptor;SRNOS_TelemetrySample sample;};
Source sources[32];unsigned source_count;
struct Adapter {void *object;SRNOS_CANAdapterOps ops;} adapters[3];unsigned adapter_count;
uint64_t now_us=1000;bool fail_publish;
SRNOS_HostLinkHandlerFn stop_handler;
}
extern "C" SRNOS_TelemetryResult SRNOS_TelemetryRegister(const SRNOS_TelemetryDescriptor *d,SRNOS_TelemetryHandle *h){assert(source_count<32);sources[source_count].descriptor=*d;*h=source_count++;return SRNOS_TELEMETRY_OK;}
extern "C" SRNOS_TelemetryResult SRNOS_TelemetryPublish(SRNOS_TelemetryHandle h,SRNOS_TelemetryValue v,uint64_t time,bool valid){if(fail_publish)return SRNOS_TELEMETRY_INVALID;auto &s=sources[h].sample;s.value=v;s.sampled_us=time;s.valid=valid;++s.sequence;return SRNOS_TELEMETRY_OK;}
extern "C" int SRNOS_CANRouterAttachAdapter(SRNOS_CANChannel ch,void *obj,const SRNOS_CANAdapterOps *ops,uint32_t period){assert(ch==3&&period==1&&adapter_count<3);assert(ops->on_rx&&ops->poll_tx&&ops->tick);adapters[adapter_count++]={obj,*ops};return 0;}
extern "C" int SRNOS_CANRouterDetachAdapter(SRNOS_CANChannel,void*){return 0;}
extern "C" bool SRNOS_SensorGetIMU(SRNOS_SensorId,SRNOS_IMUSample*){return false;}
extern "C" uint64_t SRNOS_TimeUs(void){return now_us;}
extern "C" uint32_t SRNOS_TimeMs(void){return uint32_t(now_us/1000);}
extern "C" SRNOS_HostLinkResult SRNOS_IMUTelemetryRegister(SRNOS_HostLinkRegistration *,void*){return SRNOS_HOST_LINK_OK;}
extern "C" SRNOS_HostLinkResult SRNOS_HostLinkRegisterHandler(SRNOS_HostLinkRegistration*,uint8_t service,uint8_t message,uint8_t mask,SRNOS_HostLinkQos qos,SRNOS_HostLinkHandlerFn handler,void*){assert(service==0x40&&(message==1||message==2)&&mask==1&&qos==SRNOS_HOST_LINK_QOS_CONTROL);if(message==1)stop_handler=handler;return SRNOS_HOST_LINK_OK;}
extern "C" bool SRNOS_INSGetStatus(SRNOS_INSStatus *out){*out={};return true;}
extern "C" SRNOS_StorageStatus SRNOS_IMUCalibrationSave(const SRNOS_IMUCalibration*,uint32_t){assert(false);return SRNOS_STORAGE_IO_ERROR;}
extern "C" SRNOS_StorageStatus SRNOS_IMUCalibrationValidate(const SRNOS_IMUCalibration*){return SRNOS_STORAGE_OK;}
extern "C" bool SRNOS_IMUCalibrationGetStatus(SRNOS_IMUCalibrationStatus *out){*out={};return true;}
extern "C" SRNOS_HostLinkResult SRNOS_HostLinkSubmitDeferred(uint8_t,uint8_t,uint32_t,const uint8_t*,size_t,SRNOS_HostLinkDeferredFn,void*){assert(false);return SRNOS_HOST_LINK_OK;}
static void feedback(unsigned id,unsigned raw){auto &a=adapters[id];SRNOS_CANFrame f{};f.id=0x205+id;f.dlc=8;f.data[0]=raw>>8;f.data[1]=raw&255;f.data[6]=25;a.ops.on_rx(a.object,&f,SRNOS_TimeMs());}
static int tx(unsigned id){auto &a=adapters[id];SRNOS_CANTxItem item{};assert(a.ops.poll_tx(a.object,SRNOS_CAN_TX_KIND_PERIODIC,&item,1,SRNOS_TimeMs())==1);assert(item.format==SRNOS_CAN_TX_FORMAT_GROUP_I16&&item.group_i16.group_id==0x1ff&&item.group_i16.slot==id);return item.group_i16.value;}
int main(){
    assert(source_count==32);SRNOS_SystemConfig config{};SRNOS_ApplicationConfigure(&config);
    assert(config.usb_enabled&&config.host_link_enabled&&config.bringup_indicators&&config.system_monitor_enabled);
    assert(config.ins.enabled&&config.sensors.heater_enabled&&config.sensors.heater_target_c==60.0f&&!config.host_link_storage_enabled);
    assert(config.can[2].mode==SRNOS_CAN_MODE_CLASSIC&&config.can[2].nominal_bitrate==1000000);
    assert(config.thread_count==1&&config.threads[0].period_ms==1&&config.threads[0].stack_bytes==3072);
    assert(config.bind_channels(nullptr));assert(config.host_link_setup(nullptr,nullptr)==SRNOS_HOST_LINK_OK);
    auto step=config.threads[0].step;
    assert(tx(0)==0&&tx(1)==0&&tx(2)==0);
    for(unsigned t=0;t<2500;++t){feedback(0,381);feedback(1,4850);feedback(2,4044);assert(step(nullptr,nullptr)==SRNOS_STEP_CONTINUE);now_us+=1000;}
    assert(sources[31].sample.value.u32==24);
    assert(sources[27].sample.value.u32==0&&sources[28].sample.value.u32==0);
    assert(tx(0)==0&&tx(1)==0&&tx(2)==0); // Temporary bypass must never enable motion.
    for(unsigned i=0;i<3;++i){SRNOS_CANRxFilter f{};assert(adapters[i].ops.rx_filter_at(adapters[i].object,0,&f));assert(f.id==0x205+i&&f.flags==0);assert(sources[9*i].sample.valid);}
    {SRNOS_ProtocolView normal{};size_t length=0;uint8_t flags=0;
     const int before=tx(2);assert(before==0);
     assert(stop_handler(nullptr,&normal,nullptr,0,&length,&flags)==SRNOS_HOST_LINK_STATUS_OK);
     assert(tx(2)==before); // Normal stop must not trip router-side zero inhibit.
     feedback(0,381);feedback(1,4850);feedback(2,4044);step(nullptr,nullptr);now_us+=1000;
     assert(sources[27].sample.value.u32==0&&sources[28].sample.value.u32==0);}
    now_us+=21000;assert(tx(2)==0); // Independent router-side freshness watchdog.
    feedback(0,381);feedback(1,4850);feedback(2,4044);step(nullptr,nullptr);
    assert(sources[31].sample.value.u32==24);
    assert(sources[27].sample.value.u32==10&&sources[28].sample.value.u32==6);
    assert(tx(0)==0&&tx(1)==0&&tx(2)==0);
    for(unsigned i=0;i<3;++i)assert(!sources[9*i+3].sample.valid&&!sources[9*i+4].sample.valid);
    SRNOS_ProtocolView request{};size_t length=99;uint8_t flags=99;
    request.payload_len=1;assert(stop_handler(nullptr,&request,nullptr,0,&length,&flags)==SRNOS_HOST_LINK_STATUS_BAD_LENGTH);
    request.payload_len=0;assert(stop_handler(nullptr,&request,nullptr,0,&length,&flags)==SRNOS_HOST_LINK_STATUS_OK);
    assert(length==0&&flags==0&&tx(2)==0);
    fail_publish=true;now_us+=1000;assert(step(nullptr,nullptr)==SRNOS_STEP_CONTINUE);assert(tx(2)==0);
    std::puts("PASS: CAN3 IDs, coherent telemetry, motor bypass, stale-command zero and STOP");
}
