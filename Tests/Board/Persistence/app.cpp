#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "SirinEmbedOS/Services/Telemetry.hpp"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include <atomic>

/* Test-only explicit commands. No EEPROM writes or CAN traffic on boot.
 * Back up physical contents before arming; see README. */
extern "C" {
volatile uint32_t SRNOS_TestPersistenceArm;
volatile uint32_t SRNOS_TestPersistenceCommand;
volatile uint32_t SRNOS_TestPersistenceCANFramesPerMs;
volatile uint32_t SRNOS_TestPersistenceCompleted;
volatile int SRNOS_TestPersistenceResult;
volatile SRNOS_INSStatus SRNOS_TestPersistenceINS;
volatile SRNOS_SystemMonitorSnapshot SRNOS_TestPersistenceMonitor;
}
static SRNOS::Parameters::Persistent<float> kp{0x1001,"bench.kp",1,0,50};
static SRNOS::Parameters::Persistent<float> ki{0x1002,"bench.ki",0,0,10};
static SRNOS::Telemetry::Value<int32_t> values[]={
    {"bench.encoder0","count",1000},{"bench.encoder1","count",1000},
    {"bench.encoder2","count",1000},{"bench.encoder3","count",1000},
    {"bench.encoder4","count",1000},{"bench.encoder5","count",1000},
    {"bench.encoder6","count",1000},{"bench.encoder7","count",1000}};
static std::atomic<uint32_t> tx_accepted[2],tx_rejected[2],rx_count[2];
static void receive(SRNOS_CANChannel bus,const SRNOS_CANFrame *,void *) {
    rx_count[bus==1?0:1].fetch_add(1,std::memory_order_relaxed);
}
static SRNOS_StepResult produce(void *,const SRNOS_ThreadWake *) {
    static uint32_t seq;const uint64_t t=SRNOS_TimeUs();++seq;
    for(unsigned i=0;i<8;i++) (void)values[i].Publish((int32_t)(seq+i),t);
    unsigned rate=SRNOS_TestPersistenceArm==0x53524E56?SRNOS_TestPersistenceCANFramesPerMs:0;
    if(rate>3)rate=3;
    for(unsigned b=0;b<2;b++)for(unsigned i=0;i<rate;i++) {
        SRNOS_CANFrame frame{};frame.id=b?0x603:0x601;frame.dlc=8;
        for(unsigned j=0;j<4;j++)frame.data[j]=(uint8_t)(seq>>(j*8));
        if(SRNOS_CANRouterPostOneshot(b?3:1,&frame,4,100)==0)++tx_accepted[b];else ++tx_rejected[b];
    }
    return SRNOS_STEP_CONTINUE;
}
static SRNOS_StepResult manage(void *,const SRNOS_ThreadWake *) {
    SRNOS_INSStatus ins;SRNOS_INSGetStatus(&ins);
    /* Publish debugger copies without exposing mutable service internals. */
    const uint8_t *src=(const uint8_t *)&ins;volatile uint8_t *dst=(volatile uint8_t *)&SRNOS_TestPersistenceINS;
    for(size_t i=0;i<sizeof(ins);i++)dst[i]=src[i];
    SRNOS_SystemMonitorSnapshot monitor;SRNOS_SystemMonitorGetSnapshot(&monitor);
    src=(const uint8_t *)&monitor;dst=(volatile uint8_t *)&SRNOS_TestPersistenceMonitor;
    for(size_t i=0;i<sizeof(monitor);i++)dst[i]=src[i];
    if(SRNOS_TestPersistenceArm!=0x53524E56)return SRNOS_STEP_CONTINUE;
    const uint32_t command=SRNOS_TestPersistenceCommand;SRNOS_TestPersistenceCommand=0;
    int result=0;
    if(command==1) { (void)kp.Set(kp.Get()==2?3:2);result=SRNOS_ParametersSave(); }
    else if(command==2)result=SRNOS_ParametersLoad();
    else if(command==3)result=SRNOS_ParametersResetDefaults();
    else if(command==4 || command==5) {
        SRNOS_IMUCalibration v{};v.sensor=command==4?SRNOS_SENSOR_BMI088:SRNOS_SENSOR_LSM6DS3;
        v.model_version=v.axes_version=1;v.accel_scale[0]=v.accel_scale[1]=v.accel_scale[2]=1;v.temperature_c=60;
        result=SRNOS_IMUCalibrationSave(&v,5000); // Identity correction tests persistence, not calibration accuracy.
    }
    if(command) {SRNOS_TestPersistenceResult=result;++SRNOS_TestPersistenceCompleted;}
    return SRNOS_STEP_CONTINUE;
}
extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config) {
    config->parameter_application_id=0x54455354;config->parameter_compatibility_version=1;
    (void)SRNOS_CANRouterSubscribeRaw(1,{0x603,0},receive,nullptr);
    (void)SRNOS_CANRouterSubscribeRaw(3,{0x601,0},receive,nullptr);
}
extern "C" void SRNOS_ApplicationMain(void) {
    SRNOS_ThreadConfig task{};SRNOS_Thread handle;
    task.name="ValueProducer";task.step=produce;task.stack_bytes=2048;task.priority=24;
    task.mode=SRNOS_TASK_PERIODIC;task.period_ms=1;
    SRNOS_TestPersistenceResult=SRNOS_ThreadCreate(&task,&handle);
    task.name="StoreTest";task.step=manage;task.stack_bytes=4096;task.priority=8;task.period_ms=100;
    SRNOS_TestPersistenceResult=SRNOS_ThreadCreate(&task,&handle);
}
