#include "ins_internal.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "stm32h7xx.h"
static SRNOS_INSSnapshot published;
static uint32_t max_copy_us;
static uint32_t enter(void) {uint32_t m=__get_PRIMASK();__disable_irq();return m;}
static void leave(uint32_t m) {__set_PRIMASK(m);}
/* Includes both copy and freshness decoration. Timer read is lock-safe. */
static void end_copy(uint32_t key,uint64_t begin) {
    uint64_t elapsed=SRNOS_TimeGetUptimeUs()-begin;
    if(elapsed>max_copy_us) max_copy_us=(uint32_t)elapsed;
    leave(key);
}
static void decorate_attitude(SRNOS_INSAttitude *out,uint64_t now) {
    if(!out->attitude_us||now<out->attitude_us||now-out->attitude_us>5000) out->flags&=~SRNOS_INS_VALID;
}
static void decorate_status(SRNOS_INSStatus *out,uint64_t now) {
    out->max_copy_us=max_copy_us;
    if(out->updated_us&&now>=out->updated_us&&now-out->updated_us>5000) {
        out->flags&=~SRNOS_INS_VALID;
        if(out->phase==SRNOS_INS_RUNNING||out->phase==SRNOS_INS_DEGRADED) out->phase=SRNOS_INS_OFFLINE;
    }
}
static void decorate_sample(SRNOS_SampleHeader *h,uint64_t now,uint32_t limit) {
    if(h->state==SRNOS_SENSOR_READY && (!h->acquisition_end_us || now<h->acquisition_end_us || now-h->acquisition_end_us>limit)) {
        h->state=SRNOS_SENSOR_STALE;h->valid_fields=0;
    }
}
bool SRNOS_INSGetSnapshot(SRNOS_INSSnapshot *out) {
    if(!out) return false;
    uint32_t key=enter();uint64_t now=SRNOS_TimeGetUptimeUs();*out=published;
    decorate_attitude(&out->attitude,now);decorate_status(&out->status,now);
    for(unsigned i=0;i<2;i++) decorate_sample(&out->imu[i].header,now,5000);
    decorate_sample(&out->magnetic.header,now,100000);decorate_sample(&out->pressure.header,now,250000);
    end_copy(key,now);return true;
}
bool SRNOS_INSGetAttitude(SRNOS_INSAttitude *out) {
    if(!out) return false;
    uint32_t key=enter();uint64_t now=SRNOS_TimeGetUptimeUs();*out=published.attitude;
    decorate_attitude(out,now);end_copy(key,now);return true;
}
bool SRNOS_INSGetStatus(SRNOS_INSStatus *out) {
    if(!out) return false;
    uint32_t key=enter();uint64_t now=SRNOS_TimeGetUptimeUs();*out=published.status;
    decorate_status(out,now);end_copy(key,now);return true;
}
bool SRNOS_SensorGetHeaterStatus(SRNOS_HeaterStatus *out) {
    if(!out) return false;
    uint32_t key=enter();*out=published.status.heater;leave(key);return true;
}
void SRNOS_INSPublish(const SRNOS_INSSnapshot *s) {
    uint32_t key=enter();uint64_t begin=SRNOS_TimeGetUptimeUs();published=*s;
    published.status.max_copy_us=max_copy_us;end_copy(key,begin);
}
