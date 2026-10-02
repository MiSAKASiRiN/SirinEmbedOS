#include "SirinEmbedOS/Services/Telemetry.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
extern "C" bool SRNOS_TelemetryFreeze(void);
extern "C" bool SRNOS_TelemetryRead(uint16_t,SRNOS_TelemetrySample *);
extern "C" void test_task_enter_critical(void);
extern "C" void test_task_exit_critical(void);
static unsigned critical_depth,critical_entries;
static bool interrupt_context;
static int scheduler_state;
extern "C" int xPortIsInsideInterrupt(void) { return interrupt_context?1:0; }
extern "C" int xTaskGetSchedulerState(void) { return scheduler_state; }
extern "C" void test_task_enter_critical(void) { assert(!critical_depth);++critical_depth;++critical_entries; }
extern "C" void test_task_exit_critical(void) { assert(critical_depth==1);--critical_depth; }
static SRNOS::Telemetry::Value<float> temperature{"motor.temperature","C",1000};
static SRNOS::Telemetry::Value<int32_t> signed_value{"signed","",500};
static SRNOS::Telemetry::Value<uint32_t> unsigned_value{"unsigned","count",1000};
static SRNOS::Telemetry::Value<bool> enabled{"enabled","",100};
int main()
{
    assert(temperature.Status()==SRNOS_TELEMETRY_OK);
    SRNOS_TelemetryDescriptor d={"motor.temperature","C",SRNOS_TELEMETRY_FLOAT32,1000};SRNOS_TelemetryHandle id;
    assert(SRNOS_TelemetryRegister(&d,&id)==SRNOS_TELEMETRY_INVALID);
    d.name="bad";d.max_hz=1001;assert(SRNOS_TelemetryRegister(&d,&id)==SRNOS_TELEMETRY_INVALID);
    d.max_hz=5;char name[20];
    for(unsigned i=4;i<32;++i){std::snprintf(name,sizeof(name),"copied-%u",i);d.name=name;assert(SRNOS_TelemetryRegister(&d,&id)==SRNOS_TELEMETRY_OK && id==i);}
    d.name="overflow";assert(SRNOS_TelemetryRegister(&d,&id)==SRNOS_TELEMETRY_FULL);
    assert(temperature.Publish(1.0f,1)==SRNOS_TELEMETRY_CONTEXT);
    assert(SRNOS_TelemetryFreeze());scheduler_state=2;
    assert(SRNOS_TelemetryRegister(&d,&id)==SRNOS_TELEMETRY_FROZEN);
    SRNOS_TelemetrySample sample={};assert(SRNOS_TelemetryRead(temperature.Handle(),&sample));
    assert(!sample.valid && sample.sequence==0 && sample.sampled_us==0);
    assert(temperature.Publish(12.25f,UINT64_C(0x123456789))==SRNOS_TELEMETRY_OK);
    assert(SRNOS_TelemetryRead(temperature.Handle(),&sample));
    assert(sample.valid && sample.value.f32==12.25f && sample.sequence==1 && sample.sampled_us==UINT64_C(0x123456789));
    assert(temperature.Publish(-3.0f,5,false)==SRNOS_TELEMETRY_OK);
    assert(SRNOS_TelemetryRead(temperature.Handle(),&sample));assert(!sample.valid && sample.sequence==2 && sample.value.f32==-3.0f);
    assert(signed_value.Publish(-123,8)==SRNOS_TELEMETRY_OK);
    assert(unsigned_value.Publish(UINT32_MAX,9)==SRNOS_TELEMETRY_OK);
    assert(enabled.Publish(true,10)==SRNOS_TELEMETRY_OK);
    interrupt_context=true;unsigned before=critical_entries;
    assert(temperature.Publish(99,99)==SRNOS_TELEMETRY_CONTEXT);assert(critical_entries==before);
    interrupt_context=false;
    for(unsigned i=0;i<10000;++i)assert(unsigned_value.Publish(i,i)==SRNOS_TELEMETRY_OK);
    assert(SRNOS_TelemetryRead(unsigned_value.Handle(),&sample));assert(sample.sequence==10001 && sample.value.u32==9999);
    assert(!critical_depth);
    std::puts("Telemetry static C++ registration, copied directory, bounded cache, validity/time/sequence and ISR restrictions: PASS");
}
