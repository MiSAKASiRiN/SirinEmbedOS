#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Services/Telemetry.hpp"
#include <cstdint>

namespace {
/* Only these bounded business values are remotely writable. No UART/CAN/IMU
 * configuration, service selection or controller object is registered. */
SRNOS::Parameters::Persistent<float> kp(0x1001u,"speed.kp",1.0f,0.0f,30.0f);
SRNOS::Parameters::Persistent<float> ki(0x1002u,"speed.ki",0.0f,0.0f,10.0f);
SRNOS::Parameters::Persistent<float> kd(0x1003u,"speed.kd",0.0f,0.0f,5.0f);
SRNOS::Telemetry::Value<int32_t> encoder("motor.encoder","count",100);
int32_t simulated_encoder;
struct PidValues { float kp, ki, kd; };
static_assert(sizeof(PidValues) == 3u * sizeof(float));
/* Owned only by this example's Application thread; available to a debugger.
 * A real controller would consume the local values at its own cycle boundary. */
PidValues last_snapshot{};
}

extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->usb_enabled = true;
    config->host_link_enabled = true;
    config->parameter_application_id = 0x50494431u; /* PID1: this application owns the record. */
    config->parameter_compatibility_version = 1;
    config->load_parameters = true;
    config->host_link_storage_enabled = true;
}

static SRNOS_StepResult application_step(void *arg,const SRNOS_ThreadWake *wake)
{
 (void)arg;(void)wake;
 /* Demo counter only: a real motor producer publishes its measured snapshot. */
 if(simulated_encoder==INT32_MAX)simulated_encoder=0;
 (void)encoder.Publish(++simulated_encoder,SRNOS_TimeUs());
 const uint16_t indices[]={static_cast<uint16_t>(kp.Index()),static_cast<uint16_t>(ki.Index()),static_cast<uint16_t>(kd.Index())};

        PidValues local{};
        size_t written = 0;
        if (SRNOS_ParametersSnapshot(indices, 3, reinterpret_cast<uint8_t *>(&local),
                            sizeof(local), &written) == SRNOS_PARAMETERS_STATUS_OK &&
            written == sizeof(local)) {
            last_snapshot = local;
        }


 return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result srnos_example_create_result;
static SRNOS_Thread application_task;
extern "C" void SRNOS_ApplicationMain(void)
{
 SRNOS_ThreadConfig task{};task.name="ParameterView";task.step=application_step;task.stack_bytes=4096;task.priority=24;task.mode=SRNOS_TASK_PERIODIC;task.period_ms=10;
 srnos_example_create_result=SRNOS_ThreadCreate(&task,&application_task);
}
