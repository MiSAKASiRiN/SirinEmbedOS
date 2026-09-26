#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Parameters/lib_params.hpp"
#include "cmsis_os2.h"
#include <cstdint>

namespace {
/* Only these bounded business values are remotely writable. No UART/CAN/IMU
 * configuration, service selection or controller object is registered. */
omx::params::Param<float> kp("speed.kp", 1.0f, 0.0f, 30.0f, PARAMS_MUTABLE_BUSINESS);
omx::params::Param<float> ki("speed.ki", 0.0f, 0.0f, 10.0f, PARAMS_MUTABLE_BUSINESS);
omx::params::Param<float> kd("speed.kd", 0.0f, 0.0f, 5.0f, PARAMS_MUTABLE_BUSINESS);
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
    config->load_parameters = false; /* Explicit SAVE/LOAD remains available. */
}

extern "C" void SRNOS_ApplicationMain(void)
{
    const uint16_t indices[] = {
        static_cast<uint16_t>(kp.Index()), static_cast<uint16_t>(ki.Index()),
        static_cast<uint16_t>(kd.Index())
    };
    for (;;) {
        PidValues local{};
        size_t written = 0;
        if (params_snapshot(indices, 3, reinterpret_cast<uint8_t *>(&local),
                            sizeof(local), &written) == PARAMS_STATUS_OK &&
            written == sizeof(local)) {
            last_snapshot = local;
        }
        osDelay(10);
    }
}
