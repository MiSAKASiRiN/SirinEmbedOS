#pragma once
#include "SirinEmbedOS/Devices/Sensors.h"
/* Private fixed PCB rotations. Handles in==out. */
void SRNOS_BoardSensorToBoard(SRNOS_SensorId id,const float in[3],float out[3]);
