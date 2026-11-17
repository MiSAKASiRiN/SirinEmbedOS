#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/Logging/Log.h"

extern "C" bool AppRegisterHeartbeatTask(SRNOS_SystemConfig *config);

extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->ins.enabled = false;
    config->sensors.heater_enabled = false;
    SRNOS_ASSERT(AppRegisterHeartbeatTask(config));
}

extern "C" void SRNOS_ApplicationMain(void)
{
    (void)SRNOS_LOGI_SRC("App", "Application started");
}
