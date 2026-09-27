#include "sensor_codec.h"
int32_t SRNOS_SensorSignExtend(uint32_t v,unsigned bits) {
    uint32_t sign=1u<<(bits-1); v&=(sign<<1)-1;
    return (int32_t)(v^sign)-(int32_t)sign;
}
static int16_t le16(const uint8_t *b) { return (int16_t)SRNOS_SensorSignExtend((uint32_t)b[0]|(uint32_t)b[1]<<8,16); }
static int32_t be16(const uint8_t *b) { return SRNOS_SensorSignExtend((uint32_t)b[0]<<8|b[1],16); }
static int32_t be24(const uint8_t *b) { return SRNOS_SensorSignExtend((uint32_t)b[0]<<16|(uint32_t)b[1]<<8|b[2],24); }
void SRNOS_SPAReadCoefficients(const uint8_t *b,SRNOS_SPACoefficients *c) {
    c->c0=SRNOS_SensorSignExtend((uint32_t)b[0]<<4|b[1]>>4,12);
    c->c1=SRNOS_SensorSignExtend((uint32_t)(b[1]&15)<<8|b[2],12);
    c->c00=SRNOS_SensorSignExtend((uint32_t)b[3]<<12|(uint32_t)b[4]<<4|b[5]>>4,20);
    c->c10=SRNOS_SensorSignExtend((uint32_t)(b[5]&15)<<16|(uint32_t)b[6]<<8|b[7],20);
    c->c01=be16(b+8); c->c11=be16(b+10); c->c20=be16(b+12);
    c->c21=be16(b+14); c->c30=be16(b+16);
    c->c31=SRNOS_SensorSignExtend((uint32_t)b[18]<<4|b[19]>>4,12);
    c->c40=SRNOS_SensorSignExtend((uint32_t)(b[19]&15)<<8|b[20],12);
}
void SRNOS_SPAConvert(const SRNOS_SPACoefficients *c,const uint8_t *b,SRNOS_PressureSample *s) {
    s->raw_pressure=be24(b); s->raw_temperature=be24(b+3);
    double p=s->raw_pressure/7864320.0, t=s->raw_temperature/524288.0;
    s->pressure_pa=(float)(c->c00+p*(c->c10+p*(c->c20+p*(c->c30+p*c->c40)))+t*(c->c01+p*(c->c11+p*(c->c21+p*c->c31))));
    s->temperature_c=(float)(c->c0*0.5+c->c1*t);
}
void SRNOS_LSMConvert(const uint8_t *b,SRNOS_IMUSample *s) {
    s->raw_temperature=le16(b); s->temperature_c=25.0f+s->raw_temperature/256.0f;
    for(unsigned i=0;i<3;i++) {
        s->raw_gyro[i]=le16(b+2+2*i); s->raw_accel[i]=le16(b+8+2*i);
        s->gyro_rad_s[i]=s->raw_gyro[i]*(0.07f*0.017453292519943295f);
        s->accel_m_s2[i]=s->raw_accel[i]*(0.000122f*9.80665f);
    }
}
void SRNOS_LISConvert(const uint8_t *b,SRNOS_MagneticSample *s) {
    for(unsigned i=0;i<3;i++) { s->raw_magnetic[i]=le16(b+2*i); s->magnetic_uT[i]=s->raw_magnetic[i]*0.15f; }
    s->raw_temperature=le16(b+6); s->temperature_c=25.0f+s->raw_temperature/8.0f;
}
