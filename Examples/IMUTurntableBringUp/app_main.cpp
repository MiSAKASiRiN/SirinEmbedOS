#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Devices/Motors/GM6020/GM6020.hpp"
#include "SirinEmbedOS/Services/Telemetry.hpp"
#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include "control.hpp"
#include "calibration_job.hpp"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Devices/Sensors.h"
#include <atomic>

namespace {
using SRNOS::Devices::DJI_GM6020;
using SRNOS::Devices::GM6020State;
using SRNOS::Telemetry::Value;
namespace Turntable = SRNOS::Examples::Turntable;
struct MotorObservation {
    DJI_GM6020 motor;
    Value<uint32_t> encoder;
    Value<float> angle;
    Value<bool> online;
    Value<float> unwrapped;
    Value<float> speed;
    Value<int32_t> command;
    Value<int32_t> rpm;
    Value<int32_t> current;
    Value<uint32_t> temperature;
};
MotorObservation motors[] = {
    {DJI_GM6020{1}, {"turntable.motor1.encoder_raw","count",1000},
     {"turntable.motor1.angle_deg","deg",1000},{"turntable.motor1.online","",100},
     {"turntable.motor1.unwrapped_deg","deg",1000},{"turntable.motor1.speed_dps","deg/s",1000},
     {"turntable.motor1.voltage_raw","raw",1000},{"turntable.motor1.feedback_rpm","rpm",1000},
     {"turntable.motor1.current_raw","raw",1000},{"turntable.motor1.temperature_c","C",100}},
    {DJI_GM6020{2}, {"turntable.motor2.encoder_raw","count",1000},
     {"turntable.motor2.angle_deg","deg",1000},{"turntable.motor2.online","",100},
     {"turntable.motor2.unwrapped_deg","deg",1000},{"turntable.motor2.speed_dps","deg/s",1000},
     {"turntable.motor2.voltage_raw","raw",1000},{"turntable.motor2.feedback_rpm","rpm",1000},
     {"turntable.motor2.current_raw","raw",1000},{"turntable.motor2.temperature_c","C",100}},
    {DJI_GM6020{3}, {"turntable.motor3.encoder_raw","count",1000},
     {"turntable.motor3.angle_deg","deg",1000},{"turntable.motor3.online","",100},
     {"turntable.motor3.unwrapped_deg","deg",1000},{"turntable.motor3.speed_dps","deg/s",1000},
     {"turntable.motor3.voltage_raw","raw",1000},{"turntable.motor3.feedback_rpm","rpm",1000},
     {"turntable.motor3.current_raw","raw",1000},{"turntable.motor3.temperature_c","C",100}},
};
Value<uint32_t> phase_value{"turntable.phase","",1000};
Value<uint32_t> fault_value{"turntable.fault","",1000};
Value<uint32_t> fault_axis_value{"turntable.fault_axis","",1000};
Value<float> fault_measurement_value{"turntable.fault_measurement","",1000};
Value<uint32_t> sequence_value{"turntable.sequence_revision","",10};
Turntable::Controller controller;
Turntable::CalibrationJob calibration_job;
SRNOS_CANAdapterOps guarded_ops;
std::atomic<uint32_t> inhibit{0}, updated_ms{0};
std::atomic<bool> published{false};
uint64_t previous_us;

size_t poll_guarded(void *object, SRNOS_CANTxKind kind, SRNOS_CANTxItem *out,
                    size_t max, uint32_t now_ms)
{
    const auto *base=SRNOS::Devices::DJI_GM6020_CanOps();
    const size_t count=base->poll_tx(object,kind,out,max,now_ms);
    if(published.load(std::memory_order_acquire) &&
       uint32_t(SRNOS_TimeMs()-updated_ms.load(std::memory_order_acquire))>20)
        inhibit.store(static_cast<uint32_t>(Turntable::Fault::Timing),std::memory_order_release);
    if(inhibit.load(std::memory_order_acquire) || !published.load(std::memory_order_acquire))
        for(size_t i=0;i<count;++i) out[i].group_i16.value=0;
    return count;
}
bool bind_motors(void *)
{
    guarded_ops=*SRNOS::Devices::DJI_GM6020_CanOps();
    guarded_ops.poll_tx=poll_guarded;
    if(phase_value.Status()!=SRNOS_TELEMETRY_OK || fault_value.Status()!=SRNOS_TELEMETRY_OK ||
       sequence_value.Status()!=SRNOS_TELEMETRY_OK || fault_axis_value.Status()!=SRNOS_TELEMETRY_OK || fault_measurement_value.Status()!=SRNOS_TELEMETRY_OK) return false;
    for(auto &m:motors) {
        if(m.encoder.Status()!=SRNOS_TELEMETRY_OK || m.angle.Status()!=SRNOS_TELEMETRY_OK ||
           m.online.Status()!=SRNOS_TELEMETRY_OK || m.unwrapped.Status()!=SRNOS_TELEMETRY_OK ||
           m.speed.Status()!=SRNOS_TELEMETRY_OK || m.command.Status()!=SRNOS_TELEMETRY_OK || m.rpm.Status()!=SRNOS_TELEMETRY_OK || m.current.Status()!=SRNOS_TELEMETRY_OK || m.temperature.Status()!=SRNOS_TELEMETRY_OK) return false;
        m.motor.set_offline_timeout(20);
        if(SRNOS_CANRouterAttachAdapter(SRNOS_CAN_CH_NTFDCAN3,&m.motor,&guarded_ops,1)!=0) return false;
    }
    return true;
}
#include "calibration_protocol.inc"

uint16_t controlled_stop(void *,const SRNOS_ProtocolView *request,uint8_t *,size_t,
                        size_t *length,uint8_t *flags)
{
    *length=0;*flags=0;
    if(request->payload_len!=0) return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    calibration_job.abort.store(1); // Cancel means hold; never use the failed fixed-voltage descent.
    return SRNOS_HOST_LINK_STATUS_OK;
}
SRNOS_HostLinkResult setup_host(SRNOS_HostLinkRegistration *registration,void *)
{
    // Normal shutdown: MCU keeps the loop alive through gravity descent/unload.
    // Fault inhibit remains separate and is never bypassed by this request.
    // Enabled INS registers raw IMU + INS telemetry through SDK setup_host.
    const auto result=SRNOS_HostLinkRegisterHandler(registration,0x40,2,
        SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),SRNOS_HOST_LINK_QOS_CONTROL,
        turntable_command,nullptr);
    if(result!=SRNOS_HOST_LINK_OK)return result;
    return SRNOS_HostLinkRegisterHandler(registration,0x40,1,
        SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),SRNOS_HOST_LINK_QOS_CONTROL,
        controlled_stop,nullptr);
}
[[maybe_unused]] Turntable::Gravity gravity_sample(uint64_t now)
{
    static float filtered[2][3]{};
    static uint64_t previous[2]{};
    float sum[3]{};float unit[2][3]{};
    for(unsigned i=0;i<2;++i) {
        SRNOS_IMUSample s{};
        if(!SRNOS_SensorGetIMU(i==0?SRNOS_SENSOR_BMI088:SRNOS_SENSOR_LSM6DS3,&s) ||
           s.header.state!=SRNOS_SENSOR_READY || !(s.header.valid_fields&SRNOS_SENSOR_ACCEL) ||
           now<s.header.acquisition_end_us || now-s.header.acquisition_end_us>10000) return {};
        // Native sensor axes -> fixed SDK board axes, matching sensor_frames.c.
        float v[3]={s.accel_m_s2[0],-s.accel_m_s2[1],-s.accel_m_s2[2]};
        if(i==1){v[0]=s.accel_m_s2[1];v[1]=-s.accel_m_s2[0];v[2]=s.accel_m_s2[2];}
        const float elapsed=previous[i] ? float(s.header.acquisition_end_us-previous[i])*1e-6f : 1.0f;
        const float alpha=previous[i] ? elapsed/(.2f+elapsed) : 1.0f;
        previous[i]=s.header.acquisition_end_us;
        for(unsigned j=0;j<3;++j){filtered[i][j]+=(v[j]-filtered[i][j])*alpha;v[j]=filtered[i][j];}
        const float norm=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!std::isfinite(norm)||norm<8.8f||norm>10.8f)return {};
        for(unsigned j=0;j<3;++j){unit[i][j]=v[j]/norm;sum[j]+=unit[i][j]*.5f;}
    }
    float dot=0;for(unsigned j=0;j<3;++j)dot+=unit[0][j]*unit[1][j];
    if(dot<.99863f)return {}; // >3 degrees disagreement: hold, do not chase either sensor.
    return {true,sum[0],sum[1],sum[2]};
}
SRNOS_StepResult observe(void *,const SRNOS_ThreadWake *)
{
    GM6020State samples[3]{};
    Turntable::Feedback feedback[3]{};
    for(unsigned i=0;i<3;++i) {
        motors[i].motor.snapshot(samples[i]);
        feedback[i]={samples[i].mech_angle_raw,samples[i].online,samples[i].temp_C,samples[i].speed_raw};
    }
    const uint64_t now=SRNOS_TimeUs();
    const float dt=previous_us ? float(now-previous_us)*1e-6f : .001f;
    previous_us=now;
    const auto reason=static_cast<Turntable::Fault>(inhibit.load(std::memory_order_acquire));
    if(reason!=Turntable::Fault::None) controller.stop(reason);
    calibration_job.tick(controller,feedback,dt,SRNOS_TimeMs());
    if (Turntable::MotorControlEnabled || calibration_job.engaged) {
        controller.step(feedback,dt,false,gravity_sample(now));
    } else {
        for(auto &a:controller.axis) {a.command=0;a.enabled=false;}
    }
    if(controller.fault!=Turntable::Fault::None)
        inhibit.store(static_cast<uint32_t>(controller.fault),std::memory_order_release);
    for(unsigned i=0;i<3;++i) motors[i].motor.set_give_cmd(controller.axis[i].command);
    updated_ms.store(SRNOS_TimeMs(),std::memory_order_release);
    published.store(true,std::memory_order_release);
    bool ok=sequence_value.Publish(Turntable::SequenceRevision,now)==SRNOS_TELEMETRY_OK;
    for(unsigned i=0;i<3;++i) {
        auto &m=motors[i]; const auto &s=samples[i]; const auto &a=controller.axis[i];
        const bool motion_valid=s.online && a.enabled && controller.phase!=Turntable::Phase::Waiting && controller.phase!=Turntable::Phase::Fault;
        ok &= m.encoder.Publish(s.mech_angle_raw,now,s.online)==SRNOS_TELEMETRY_OK;
        ok &= m.angle.Publish(s.mech_angle_deg,now,s.online)==SRNOS_TELEMETRY_OK;
        ok &= m.online.Publish(s.online,now)==SRNOS_TELEMETRY_OK;
        ok &= m.unwrapped.Publish(a.position,now,motion_valid)==SRNOS_TELEMETRY_OK;
        ok &= m.speed.Publish(a.speed,now,motion_valid)==SRNOS_TELEMETRY_OK;
        ok &= m.command.Publish(a.command,now)==SRNOS_TELEMETRY_OK;
        ok &= m.rpm.Publish(s.speed_raw,now,s.online)==SRNOS_TELEMETRY_OK;
        ok &= m.current.Publish(s.iq_raw,now,s.online)==SRNOS_TELEMETRY_OK;
        ok &= m.temperature.Publish(s.temp_C,now,s.online)==SRNOS_TELEMETRY_OK;
    }
    ok &= phase_value.Publish(static_cast<uint32_t>(controller.phase),now)==SRNOS_TELEMETRY_OK;
    ok &= fault_value.Publish(static_cast<uint32_t>(controller.fault),now)==SRNOS_TELEMETRY_OK;
    ok &= fault_axis_value.Publish(controller.fault_axis,now)==SRNOS_TELEMETRY_OK;
    ok &= fault_measurement_value.Publish(controller.fault_measurement,now)==SRNOS_TELEMETRY_OK;
    if(!ok) inhibit.store(static_cast<uint32_t>(Turntable::Fault::Controller),std::memory_order_release);
    return SRNOS_STEP_CONTINUE; // Keep zero output and fault telemetry alive on faults.
}
}
extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->bringup_indicators=true;
    config->usb_enabled=true;config->host_link_enabled=true;
    config->host_link_storage_enabled=false;config->load_parameters=false;
    config->system_monitor_enabled=true;config->ins.enabled=true;
    config->sensors.heater_enabled=true;
    config->sensors.heater_target_c=60.0f;
    config->can[2]={SRNOS_CAN_MODE_CLASSIC,1000000u,0u,false};
    config->bind_channels=bind_motors;config->bind_channels_user=nullptr;
    config->host_link_setup=setup_host;config->host_link_user=nullptr;
    SRNOS_ThreadConfig task{};
    task.name="TurntableView";task.step=observe;task.stack_bytes=3072;task.priority=24;
    task.mode=SRNOS_TASK_PERIODIC;task.period_ms=1;
    config->threads[0]=task;config->thread_count=1;
}
extern "C" void SRNOS_ApplicationMain(void) {}
