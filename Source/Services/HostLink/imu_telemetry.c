#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include <string.h>
#include <limits.h>
#define CAPACITY 64u
typedef struct {
    SRNOS_IMUSample samples[CAPACITY];
    uint32_t head, tail, high_water, enqueued, overflow, sent, failed, discarded;
    SRNOS_SensorId id;
    bool enabled;
} imu_stream_t;
static imu_stream_t streams[2];
static bool registered;
static SRNOS_IMUSample batch[SRNOS_IMU_BATCH_MAX];
static uint8_t encoded[SRNOS_IMU_RECORD_BYTES*SRNOS_IMU_BATCH_MAX];
_Static_assert(sizeof(float)==4, "wire float32");

bool SRNOS_IMUTelemetryEncode(const SRNOS_IMUSample *s,uint8_t *out,size_t cap)
{
    if(!s || !out || cap<SRNOS_IMU_RECORD_BYTES) return false;
    memset(out,0,SRNOS_IMU_RECORD_BYTES);
    SRNOS_Memory_MutableByteSpan b=SRNOS_Memory_MutableBytes(out,SRNOS_IMU_RECORD_BYTES);
    out[0]=1; out[1]=(uint8_t)s->header.state;
    (void)SRNOS_Endian_StoreLE32(b,4,s->header.sequence);
    (void)SRNOS_Endian_StoreLE32(b,8,s->header.valid_fields);
    (void)SRNOS_Endian_StoreLE64(b,12,s->header.acquisition_start_us);
    (void)SRNOS_Endian_StoreLE64(b,20,s->header.acquisition_end_us);
    for(unsigned i=0;i<3;i++) {
        (void)SRNOS_Endian_StoreLE16(b,28+2*i,(uint16_t)s->raw_accel[i]);
        (void)SRNOS_Endian_StoreLE16(b,34+2*i,(uint16_t)s->raw_gyro[i]);
    }
    (void)SRNOS_Endian_StoreLE16(b,40,(uint16_t)s->raw_temperature);
    for(unsigned i=0;i<7;i++) {
        float value=i<3?s->accel_m_s2[i]:i<6?s->gyro_rad_s[i-3]:s->temperature_c;
        uint32_t bits; memcpy(&bits,&value,4);
        (void)SRNOS_Endian_StoreLE32(b,44+4*i,bits);
    }
    return true;
}
void SRNOS_IMUTelemetryObserve(SRNOS_SensorId id,const SRNOS_IMUSample *sample,void *user)
{
    (void)user;
    if((unsigned)id>SRNOS_SENSOR_LSM6DS3 || !sample || !registered) return;
    imu_stream_t *s=&streams[id];
    taskENTER_CRITICAL();
    if(s->enabled) {
        if(s->head-s->tail==CAPACITY) ++s->overflow;
        else {
            s->samples[s->head%CAPACITY]=*sample; ++s->head; ++s->enqueued;
            uint32_t depth=s->head-s->tail;
            if(depth>s->high_water) s->high_water=depth;
        }
    }
    taskEXIT_CRITICAL();
}
static void configure(void *user,bool enabled,uint32_t period)
{
    (void)period; imu_stream_t *s=user;
    taskENTER_CRITICAL();
    s->discarded+=s->head-s->tail; s->tail=s->head; s->enabled=enabled;
    taskEXIT_CRITICAL();
}
static void delivered(void *user,uint32_t count,srn_link_delivery_t result)
{
    imu_stream_t *s=user; taskENTER_CRITICAL();
    if(result==SRN_LINK_DELIVERED) s->sent+=count;
    else if(result==SRN_LINK_FAILED) s->failed+=count;
    else s->discarded+=count;
    taskEXIT_CRITICAL();
}
static void pump(void *user,uint32_t now_ms)
{
    (void)now_ms; imu_stream_t *s=user;
    const uint64_t now=DWT_GetUptimeUs();
    taskENTER_CRITICAL();
    unsigned count=s->head-s->tail;
    /* Flush by 4 ms so the 1 ms HostComm wake budget stays within 5 ms. */
    if(!count || (count<SRNOS_IMU_BATCH_MAX && now-s->samples[s->tail%CAPACITY].header.acquisition_end_us<4000)) {
        taskEXIT_CRITICAL(); return;
    }
    if(count>SRNOS_IMU_BATCH_MAX) count=SRNOS_IMU_BATCH_MAX;
    for(unsigned i=0;i<count;i++) batch[i]=s->samples[(s->tail+i)%CAPACITY];
    taskEXIT_CRITICAL();
    for(unsigned i=0;i<count;i++) (void)SRNOS_IMUTelemetryEncode(&batch[i],encoded+i*SRNOS_IMU_RECORD_BYTES,SRNOS_IMU_RECORD_BYTES);
    srn_link_result_t result=TSKPTT_HostComm_PublishTelemetry((uint16_t)(SRNOS_IMU_STREAM_BMI+s->id),
        batch[0].header.sequence,(uint32_t)batch[0].header.acquisition_start_us,1000,(uint8_t)count,
        encoded,count*SRNOS_IMU_RECORD_BYTES);
    if(result==SRN_LINK_ERR_FULL || result==SRN_LINK_ERR_BUSY) return;
    taskENTER_CRITICAL(); s->tail+=count;
    if(result!=SRN_LINK_OK) s->failed+=count;
    taskEXIT_CRITICAL();
}
static uint16_t status(void *user,const srn_cobs_prot_view_t *request,uint8_t *out,size_t cap,size_t *len,uint8_t *flags)
{
    (void)user; *len=0; *flags=0;
    if(request->payload_len) return SRN_LINK_STATUS_BAD_LENGTH;
    if(cap<92) return SRN_LINK_STATUS_INTERNAL_ERROR;
    memset(out,0,92); out[0]=1; out[1]=2;
    SRNOS_Memory_MutableByteSpan b=SRNOS_Memory_MutableBytes(out,92);
    for(unsigned i=0;i<2;i++) {
        SRNOS_SensorStatus health={0}; SRNOS_SensorGetStatus((SRNOS_SensorId)i,&health);
        imu_stream_t *s=&streams[i]; unsigned p=4+44*i;
        (void)SRNOS_Endian_StoreLE16(b,p,(uint16_t)(SRNOS_IMU_STREAM_BMI+i));
        out[p+2]=(uint8_t)health.state;
        (void)SRNOS_Endian_StoreLE32(b,p+4,health.samples);
        (void)SRNOS_Endian_StoreLE64(b,p+8,health.last_sample_us);
        taskENTER_CRITICAL();
        out[p+3]=s->enabled;
        (void)SRNOS_Endian_StoreLE16(b,p+16,(uint16_t)(s->head-s->tail));
        (void)SRNOS_Endian_StoreLE16(b,p+18,(uint16_t)s->high_water);
        uint32_t values[6]={s->enqueued,s->overflow,s->sent,s->failed,s->discarded,health.samples};
        taskEXIT_CRITICAL();
        for(unsigned n=0;n<6;n++) (void)SRNOS_Endian_StoreLE32(b,p+20+4*n,values[n]);
    }
    *len=92; return SRN_LINK_STATUS_OK;
}
srn_link_result_t SRNOS_IMUTelemetryRegister(srn_link_t *link,void *user)
{
    (void)user;
    if(registered) return SRN_LINK_ERR_BUSY;
    for(unsigned i=0;i<2;i++) {
        streams[i].id=(SRNOS_SensorId)i;
        const tskptt_host_comm_telemetry_config_t c={
            .stream_id=(uint16_t)(SRNOS_IMU_STREAM_BMI+i),.max_sample_size=sizeof(encoded),
            .required_period_us=1000,.policy=SRN_LINK_FIFO,.on_config=configure,.poll=pump,
            .on_delivery=delivered,.user=&streams[i]};
        srn_link_result_t r=TSKPTT_HostComm_RegisterTelemetryEx(&c);
        if(r!=SRN_LINK_OK) return r;
    }
    srn_link_result_t r=srn_link_register_handler(link,SRN_LINK_SERVICE_TELEMETRY,
        SRNOS_IMU_TELEMETRY_STATUS_MESSAGE,SRN_LINK_KIND_MASK(SRN_COBS_PROT_KIND_REQUEST),
        SRN_LINK_QOS_RELIABLE,status,NULL);
    registered=r==SRN_LINK_OK; return r;
}
