#include "sensor_registers.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t regfile[256];
static bool connected,reset_stuck,wrong_readback;
static unsigned writes;
static SRNOS_SensorId device;
static bool read_reg(void *ctx,uint8_t reg,uint8_t *bytes,size_t n) {
    (void)ctx; if(!connected) return false;
    memcpy(bytes,regfile+reg,n);
    if(wrong_readback && (reg==0x11 || reg==0x61 || reg==0x07)) bytes[0]^=1;
    return true;
}
static bool write_reg(void *ctx,uint8_t reg,uint8_t v) {
    (void)ctx; if(!connected) return false; ++writes;
    regfile[reg]=v;
    if(device==SRNOS_SENSOR_LSM6DS3 && reg==0x12 && v==1 && !reset_stuck) regfile[reg]=0;
    if(device==SRNOS_SENSOR_LIS2MDL && reg==0x60 && v==0x20 && !reset_stuck) regfile[reg]=3;
    if(device==SRNOS_SENSOR_SPA06 && reg==0x0c) regfile[8]=reset_stuck?0:0xc0;
    return true;
}
static const SRNOS_SensorRegisterIO io={NULL,read_reg,write_reg};
static void reset(SRNOS_SensorId id) {
    memset(regfile,0,sizeof(regfile)); device=id; connected=true; reset_stuck=wrong_readback=false; writes=0;
    regfile[id==1?0x0f:id==2?0x4f:0x0d]=id==1?0x6a:id==2?0x40:0x11;
    regfile[0x10]=3; regfile[8]=0xc0;
}
static void drive(SRNOS_SensorRegisters *s,uint64_t start,unsigned steps) {
    for(unsigned i=0;i<steps;i++) SRNOS_SensorInitStep(s,&io,start+10000*i);
}
static void closef(float a,float b,float tolerance) { assert(fabsf(a-b)<tolerance); }
int main(void) {
    for(unsigned bits=12;bits<=24;bits+=4) {
        assert(SRNOS_SensorSignExtend(1u<<(bits-1),bits)==-(int32_t)(1u<<(bits-1)));
        assert(SRNOS_SensorSignExtend((1u<<bits)-1,bits)==-1);
        assert(SRNOS_SensorSignExtend((1u<<(bits-1))-1,bits)==(int32_t)((1u<<(bits-1))-1));
    }
    for(unsigned id=1;id<=3;id++) {
        reset(id); SRNOS_SensorRegisters s={.id=id}; drive(&s,0,25); assert(s.state==SRNOS_SENSOR_READY);
        if(id==1) assert(regfile[0x10]==0x88 && regfile[0x11]==0x8c && regfile[0x12]==0x44 && regfile[0x0d]==0 && regfile[0x0e]==0);
        if(id==2) assert(regfile[0x60]==0x8c && regfile[0x62]==0x10);
        if(id==3) assert(regfile[6]==0x43 && regfile[7]==0x40 && regfile[8]==7 && regfile[9]==0);
        reset(id); s=(SRNOS_SensorRegisters){.id=id}; regfile[id==1?0x0f:id==2?0x4f:0x0d]=0x55; drive(&s,0,20); assert(s.state==SRNOS_SENSOR_UNSUPPORTED && writes==0);
        reset(id); s=(SRNOS_SensorRegisters){.id=id}; connected=false; drive(&s,0,20); assert(s.state==SRNOS_SENSOR_OFFLINE && writes==0);
        connected=true; drive(&s,900000,4); assert(writes==0); drive(&s,1000000,25); assert(s.state==SRNOS_SENSOR_READY);
        reset(id); s=(SRNOS_SensorRegisters){.id=id}; reset_stuck=true; drive(&s,0,25); assert(s.state==SRNOS_SENSOR_CONFIG_ERROR);
        reset(id); s=(SRNOS_SensorRegisters){.id=id}; wrong_readback=true; drive(&s,0,25); assert(s.state==SRNOS_SENSOR_CONFIG_ERROR);
    }
    for(unsigned v=0x12;v<=0x21;v+=15) { reset(3); SRNOS_SensorRegisters s={.id=3}; regfile[0x0d]=v; drive(&s,0,2); assert(s.state==SRNOS_SENSOR_UNSUPPORTED && !writes); }
    uint8_t all[21]; memset(all,0xff,sizeof(all)); SRNOS_SPACoefficients c; SRNOS_SPAReadCoefficients(all,&c);
    assert(c.c0==-1 && c.c1==-1 && c.c00==-1 && c.c10==-1 && c.c01==-1 && c.c11==-1 && c.c20==-1 && c.c21==-1 && c.c30==-1 && c.c31==-1 && c.c40==-1);
    uint8_t minimum[21]={0x80,0x08,0x00,0x80,0x00,0x08,0x00,0x00,0x80,0,0x80,0,0x80,0,0x80,0,0x80,0,0x80,0x08,0};
    SRNOS_SPAReadCoefficients(minimum,&c); assert(c.c0==-2048 && c.c1==-2048 && c.c00==-524288 && c.c10==-524288 && c.c01==-32768 && c.c31==-2048 && c.c40==-2048);
    c=(SRNOS_SPACoefficients){.c0=50,.c1=10,.c00=100000,.c10=1000,.c20=100,.c30=10,.c40=8,.c01=20,.c11=4,.c21=2,.c31=8};
    /* Praw=3932160 gives p=0.5; Traw=-262144 gives t=-0.5.
     * 100000+500+25+1.25+0.5-0.5*(20+2+0.5+1)=100515. */
    uint8_t raw[6]={0x3c,0,0,0xfc,0,0}; SRNOS_PressureSample p={0}; SRNOS_SPAConvert(&c,raw,&p);
    closef(p.pressure_pa,100515.0f,0.001f); closef(p.temperature_c,20,0.001f);
    assert(p.raw_pressure==3932160 && p.raw_temperature==-262144);
    uint8_t lsm[14]={0,1,0x10,0x27,0xf0,0xd8,0,0,0,0x40,0,0xc0,0,0}; SRNOS_IMUSample i={0}; SRNOS_LSMConvert(lsm,&i);
    assert(i.raw_gyro[0]==10000 && i.raw_gyro[1]==-10000 && i.raw_accel[0]==16384 && i.raw_accel[1]==-16384);
    closef(i.gyro_rad_s[0],12.21730476f,0.0001f); closef(i.accel_m_s2[0],19.6019966f,0.0001f); closef(i.temperature_c,26,0.001f);
    uint8_t lis[8]={0x64,0,0x9c,0xff,0,0,8,0}; SRNOS_MagneticSample m={0}; SRNOS_LISConvert(lis,&m);
    closef(m.magnetic_uT[0],15,0.001f); closef(m.magnetic_uT[1],-15,0.001f); closef(m.temperature_c,26,0.001f);
    puts("Sensors: IDs, unsupported product/revision, reset/readback timeouts, disconnected retry, all coefficient widths, numeric compensation and SI conversion passed");
}
