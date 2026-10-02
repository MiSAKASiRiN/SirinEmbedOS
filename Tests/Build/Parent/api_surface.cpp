/* Public API smoke test compiled as an external SDK consumer. Every installed
 * public header is also compiled alone and together by check_sdk.py. */
#include "SirinEmbedOS/Config.h"
#include "SirinEmbedOS/Core/SirinOS.hpp"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Kernel/Sync.h"
#include "SirinEmbedOS/Services/Referee/UI/Types.h"
#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "SirinEmbedOS/Services/Telemetry.hpp"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include <type_traits>

/* Enforce the effective compiler search path as well as inspecting CMake flags. */
#if __has_include("FreeRTOS.h") || __has_include("cmsis_os2.h") || \
    __has_include("stm32h7xx_hal.h") || __has_include("arm_math.h")
#error "SirinEmbedOS implementation headers must not be visible to a parent application"
#endif

static_assert(std::is_same_v<decltype(SRNOS_TimeMs()), uint32_t>);
static_assert(std::is_same_v<decltype(SRNOS_TimeUs()), uint64_t>);
static_assert(std::is_same_v<decltype(SRNOS_ThreadCreate(nullptr, nullptr)), SRNOS_Result>);
static_assert(SRNOS_NO_WAIT == 0);
static_assert(SRNOS_WAIT_FOREVER == UINT32_MAX);
static_assert(std::is_constructible_v<SRNOS::Parameters::Persistent<float>,
    uint32_t,const char *,float,float,float>);
static_assert(std::is_constructible_v<SRNOS::Telemetry::Value<int32_t>,
    const char *,const char *,uint16_t>);
static_assert(std::is_same_v<decltype(SRNOS_IMUCalibrationSave(nullptr,0)),SRNOS_StorageStatus>);

/* Names may change; the referee's packed wire records must not. */
static_assert(sizeof(SRNOS_RefereeUIInterfaceFigure) == 15);
static_assert(sizeof(SRNOS_RefereeUIInterfaceLine) == 15);
static_assert(sizeof(SRNOS_RefereeUIInterfaceNumber) == 15);
static_assert(sizeof(SRNOS_RefereeUIInterfaceString) == 45);
static_assert(sizeof(SRNOS_RefereeUIFrameHeader) == 13);
static_assert(sizeof(SRNOS_RefereeUI1Frame) == 30);
static_assert(sizeof(SRNOS_RefereeUI2Frame) == 45);
static_assert(sizeof(SRNOS_RefereeUI5Frame) == 90);
static_assert(sizeof(SRNOS_RefereeUI7Frame) == 120);
static_assert(sizeof(SRNOS_RefereeUIStringFrame) == 60);
