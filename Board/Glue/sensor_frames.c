#include "sensor_frames.h"
void SRNOS_BoardSensorToBoard(SRNOS_SensorId id,const float in[3],float out[3])
{
    const float x=in[0],y=in[1],z=in[2];
    if(id==SRNOS_SENSOR_BMI088) { out[0]=x;out[1]=-y;out[2]=-z; }
    else { out[0]=y;out[1]=-x;out[2]=z; }
}
