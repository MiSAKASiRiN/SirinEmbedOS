#pragma once
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include "SirinEmbedOS/Devices/IMU/mahony_filter.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t start_us;
    uint32_t count;
    float mean[6], m2[6];
} SRNOS_INSWindow;
typedef struct {
    SRNOS_INSConfig config;
    SRNOS_INSSnapshot snapshot;
    struct SRNOS_MahonyState filter;
    SRNOS_INSWindow window;
    uint64_t probe_us, thermal_start_us, last_step_us, convergence_us;
    uint64_t good_since[2];
    uint32_t used_sequence[2];
    unsigned primary;
    bool ever_available, session_active, seeded, running, warmup_expired;
    SRNOS_IMUCalibration calibration[2];
    uint8_t calibration_mask;
    bool calibration_pending;
} SRNOS_INSCore;
void SRNOS_INSCoreInit(SRNOS_INSCore *,const SRNOS_INSConfig *,uint64_t now);
void SRNOS_INSCoreSetCalibration(SRNOS_INSCore *,const SRNOS_IMUCalibration values[2],uint8_t mask);
/* true requests a startup panic; no HAL/RTOS dependencies in the engine. */
bool SRNOS_INSCoreStep(SRNOS_INSCore *,const SRNOS_IMUSample samples[2],
    const SRNOS_MagneticSample *,const SRNOS_PressureSample *,
    const SRNOS_HeaterStatus *,uint64_t now);
void SRNOS_HeaterInit(bool enabled,float target,bool hardware_ok);
SRNOS_HeaterStatus SRNOS_HeaterStep(const SRNOS_IMUSample samples[2],uint64_t now);
void SRNOS_INSPublish(const SRNOS_INSSnapshot *);
void SRNOS_INSPrepare(const SRNOS_INSConfig *);
void SRNOS_INSWorker(void *);
#ifdef __cplusplus
}
#endif
