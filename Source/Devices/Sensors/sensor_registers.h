#pragma once
#include "sensor_codec.h"
#include <stddef.h>
typedef struct {
    void *context;
    bool (*read)(void *,uint8_t,uint8_t *,size_t);
    bool (*write)(void *,uint8_t,uint8_t);
} SRNOS_SensorRegisterIO;
typedef struct {
    SRNOS_SensorId id;
    SRNOS_SensorState state;
    unsigned phase, index;
    uint64_t deadline_us, retry_us;
    uint8_t identity, readback[8];
    SRNOS_SPACoefficients coefficients;
} SRNOS_SensorRegisters;
/* One bounded init step, never sleeps while holding the bus. READY is sticky
 * until caller reports transport/stale failure. Unsupported IDs only re-probe. */
void SRNOS_SensorInitStep(SRNOS_SensorRegisters *,const SRNOS_SensorRegisterIO *,uint64_t now_us);
void SRNOS_SensorInitFailed(SRNOS_SensorRegisters *,SRNOS_SensorState,uint64_t now_us);
