#include "sensor_registers.h"
#include <string.h>
typedef struct { uint8_t reg,value,mask; } setting;
static const setting lsm[]={ {0x12,0x44,0xff},{0x0d,0,0xff},{0x0e,0,0xff},{0x0a,0,0xff},{0x10,0x88,0xff},{0x11,0x8c,0xff} };
static const setting lis[]={ {0x62,0x10,0xff},{0x61,0,0xff},{0x60,0x8c,0xff} };
static const setting spa[]={ {0x06,0x43,0xff},{0x07,0x40,0xff},{0x09,0,0xff},{0x08,7,0x0f} };
void SRNOS_SensorInitFailed(SRNOS_SensorRegisters *s,SRNOS_SensorState reason,uint64_t now) {
    s->state=reason; s->phase=0; s->index=0; s->retry_us=now+1000000;
}
void SRNOS_SensorInitStep(SRNOS_SensorRegisters *s,const SRNOS_SensorRegisterIO *io,uint64_t now) {
    if(s->state==SRNOS_SENSOR_READY || now<s->retry_us) return;
    uint8_t v=0;
    const bool islsm=s->id==SRNOS_SENSOR_LSM6DS3, islis=s->id==SRNOS_SENSOR_LIS2MDL;
    const setting *table=islsm?lsm:islis?lis:spa;
    unsigned count=islsm?sizeof(lsm)/sizeof(*lsm):islis?sizeof(lis)/sizeof(*lis):sizeof(spa)/sizeof(*spa);
    uint8_t reset_reg=islsm?0x12:islis?0x60:0x0c;
    switch(s->phase) {
    case 0:
        s->state=SRNOS_SENSOR_INITIALIZING;
        if(!io->read(io->context,islsm?0x0f:islis?0x4f:0x0d,&v,1)) goto offline;
        s->identity=v;
        if(v!=(islsm?0x6a:islis?0x40:0x11)) {
            SRNOS_SensorInitFailed(s,(v==0 || v==255)?SRNOS_SENSOR_OFFLINE:SRNOS_SENSOR_UNSUPPORTED,now); return;
        }
        s->phase=1; return;
    case 1:
        if(!io->write(io->context,reset_reg,islsm?1:islis?0x20:9)) goto offline;
        s->deadline_us=now+100000; s->phase=2; return;
    case 2:
        if(!io->read(io->context,islsm?0x12:islis?0x60:0x08,&v,1)) goto offline;
        if(islsm?(v&1):islis?(v&0x20):((v&0xc0)!=0xc0)) {
            if(now>=s->deadline_us) SRNOS_SensorInitFailed(s,SRNOS_SENSOR_CONFIG_ERROR,now);
            return;
        }
        if(!islsm && !islis) {
            uint8_t bytes[21];
            if(!io->read(io->context,0x10,bytes,sizeof(bytes))) goto offline;
            /* All-0/all-FF coefficients are not a usable factory trim. */
            unsigned any=0, all=255;
            for(unsigned i=0;i<21;i++) { any|=bytes[i]; all&=bytes[i]; }
            if(!any || all==255) { SRNOS_SensorInitFailed(s,SRNOS_SENSOR_CONFIG_ERROR,now); return; }
            SRNOS_SPAReadCoefficients(bytes,&s->coefficients);
        }
        s->index=0; s->phase=3; return;
    case 3:
        if(!io->write(io->context,table[s->index].reg,table[s->index].value)) goto offline;
        s->phase=4; return;
    case 4:
        if(!io->read(io->context,table[s->index].reg,&v,1)) goto offline;
        s->readback[s->index]=v;
        if((v&table[s->index].mask)!=table[s->index].value) {
            SRNOS_SensorInitFailed(s,SRNOS_SENSOR_CONFIG_ERROR,now); return;
        }
        if(++s->index==count) s->state=SRNOS_SENSOR_READY;
        else s->phase=3;
        return;
    default: SRNOS_SensorInitFailed(s,SRNOS_SENSOR_CONFIG_ERROR,now); return;
    }
offline: SRNOS_SensorInitFailed(s,SRNOS_SENSOR_OFFLINE,now);
}
