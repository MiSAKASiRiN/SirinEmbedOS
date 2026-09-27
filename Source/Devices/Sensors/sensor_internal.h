#pragma once
#include "SirinEmbedOS/Devices/Sensors.h"
void SRNOS_SensorsPrepare(const SRNOS_SensorConfig *);
void SRNOS_LSMWorker(void *);
void SRNOS_EnvironmentWorker(void *);
void SRNOS_BMIWorker(void *);
