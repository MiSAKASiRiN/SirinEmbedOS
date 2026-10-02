#include "telemetry_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
typedef struct {
    SRNOS_TelemetryDescriptor descriptor;
    char name[SRNOS_TELEMETRY_NAME_MAX+1u],unit[SRNOS_TELEMETRY_UNIT_MAX+1u];
    SRNOS_TelemetrySample sample;
} telemetry_entry_t;
static telemetry_entry_t entries[SRNOS_TELEMETRY_CAPACITY];
static uint16_t count;
static bool frozen;
static bool valid_text(const char *s,size_t max,bool nonempty)
{
    if(!s)return !nonempty;
    size_t n=0;while(n<=max && s[n])++n;
    return n<=max && (!nonempty || n!=0);
}
SRNOS_TelemetryResult SRNOS_TelemetryRegister(const SRNOS_TelemetryDescriptor *d,SRNOS_TelemetryHandle *out)
{
    if(frozen)return SRNOS_TELEMETRY_FROZEN;
    if(xPortIsInsideInterrupt()==pdTRUE || xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)return SRNOS_TELEMETRY_CONTEXT;
    if(!d || !out || !valid_text(d->name,SRNOS_TELEMETRY_NAME_MAX,true) ||
       !valid_text(d->unit,SRNOS_TELEMETRY_UNIT_MAX,false) || d->type<SRNOS_TELEMETRY_FLOAT32 ||
       d->type>SRNOS_TELEMETRY_BOOL || d->max_hz<5 || d->max_hz>SRNOS_TELEMETRY_RATE_MAX_HZ || d->max_hz%5)
        return SRNOS_TELEMETRY_INVALID;
    for(unsigned i=0;i<count;++i)if(strcmp(entries[i].name,d->name)==0)return SRNOS_TELEMETRY_INVALID;
    if(count==SRNOS_TELEMETRY_CAPACITY)return SRNOS_TELEMETRY_FULL;
    telemetry_entry_t *e=&entries[count];e->descriptor=*d;
    memcpy(e->name,d->name,strlen(d->name)+1u);
    if(d->unit)memcpy(e->unit,d->unit,strlen(d->unit)+1u);
    e->descriptor.name=e->name;e->descriptor.unit=e->unit;
    *out=count++;return SRNOS_TELEMETRY_OK;
}
bool SRNOS_TelemetryFreeze(void) { frozen=true;return true; }
uint16_t SRNOS_TelemetryCount(void) { return count; }
const SRNOS_TelemetryDescriptor *SRNOS_TelemetryDescribe(uint16_t index)
{ return frozen && index<count ? &entries[index].descriptor : NULL; }
SRNOS_TelemetryResult SRNOS_TelemetryPublish(SRNOS_TelemetryHandle id,SRNOS_TelemetryValue value,uint64_t sampled_us,bool valid)
{
    if(xPortIsInsideInterrupt()==pdTRUE || xTaskGetSchedulerState()!=taskSCHEDULER_RUNNING)return SRNOS_TELEMETRY_CONTEXT;
    if(!frozen || id>=count)return SRNOS_TELEMETRY_INVALID;
    taskENTER_CRITICAL();
    SRNOS_TelemetrySample *s=&entries[id].sample;
    s->value=value;s->sampled_us=sampled_us;s->valid=valid;++s->sequence;
    taskEXIT_CRITICAL();return SRNOS_TELEMETRY_OK;
}
bool SRNOS_TelemetryRead(uint16_t id,SRNOS_TelemetrySample *out)
{
    if(!frozen || !out || id>=count)return false;
    taskENTER_CRITICAL();*out=entries[id].sample;taskEXIT_CRITICAL();return true;
}
