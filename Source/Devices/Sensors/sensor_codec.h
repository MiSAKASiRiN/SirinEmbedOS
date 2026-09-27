#pragma once
#include "SirinEmbedOS/Devices/Sensors.h"
typedef struct { int32_t c0,c1,c00,c10,c01,c11,c20,c21,c30,c31,c40; } SRNOS_SPACoefficients;
int32_t SRNOS_SensorSignExtend(uint32_t value, unsigned bits);
void SRNOS_SPAReadCoefficients(const uint8_t bytes[21], SRNOS_SPACoefficients *);
void SRNOS_SPAConvert(const SRNOS_SPACoefficients *,const uint8_t bytes[6],SRNOS_PressureSample *);
void SRNOS_LSMConvert(const uint8_t bytes[14],SRNOS_IMUSample *);
void SRNOS_LISConvert(const uint8_t bytes[8],SRNOS_MagneticSample *);
