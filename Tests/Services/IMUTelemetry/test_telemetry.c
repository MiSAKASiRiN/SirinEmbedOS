#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned depth, config_count, publications;
static uint64_t now=10000;
static tskptt_host_comm_telemetry_config_t configs[2];
static srn_link_handler_fn status_handler;
static uint8_t sent[432]; static size_t sent_len;
static srn_link_result_t result=SRN_LINK_OK;
void test_task_enter_critical(void) { ++depth; }
void test_task_exit_critical(void) { assert(depth); --depth; }
uint64_t DWT_GetUptimeUs(void) { return now; }
bool SRNOS_SensorGetStatus(SRNOS_SensorId id,SRNOS_SensorStatus *s)
{ (void)id; *s=(SRNOS_SensorStatus){.state=SRNOS_SENSOR_OFFLINE,.samples=65,.last_sample_us=1000}; return true; }
srn_link_result_t TSKPTT_HostComm_RegisterTelemetryEx(const tskptt_host_comm_telemetry_config_t *c)
{ assert(config_count<2); configs[config_count++]=*c; return SRN_LINK_OK; }
srn_link_result_t srn_link_register_handler(srn_link_t *l,uint8_t s,uint8_t m,uint8_t k,srn_link_qos_t q,srn_link_handler_fn fn,void *u)
{ (void)l;(void)s;(void)m;(void)k;(void)q;(void)u;status_handler=fn;return SRN_LINK_OK; }
srn_link_result_t TSKPTT_HostComm_PublishTelemetry(uint16_t id,uint32_t sequence,uint32_t time,uint16_t period,uint8_t count,const uint8_t *samples,size_t n)
{
    assert(id==0x100 || id==0x101); assert(period==1000 && time==500 && sequence>0);
    assert(count<=6 && n==count*72u && depth==0); ++publications;
    if(result==SRN_LINK_OK) { memcpy(sent,samples,n);sent_len=n; }
    return result;
}
#include "../../../..//Source/Services/HostLink/imu_telemetry.c"
int main(int argc,char **argv)
{
    assert(argc==2); assert(SRNOS_IMUTelemetryRegister(NULL,NULL)==SRN_LINK_OK);
    SRNOS_IMUSample sample={.header={.sequence=0x12345678,.valid_fields=19,
        .acquisition_start_us=UINT64_C(0x1122334455667788),.acquisition_end_us=UINT64_C(0x11223344556677aa),.state=SRNOS_SENSOR_READY},
        .raw_accel={-32768,-123,32767},.raw_gyro={-456,0,789},.raw_temperature=-42,
        .accel_m_s2={1.25f,-2.5f,9.75f},.gyro_rad_s={-0.125f,0.25f,0.5f},.temperature_c=23.5f};
    uint8_t bytes[72]; assert(SRNOS_IMUTelemetryEncode(&sample,bytes,72));
    assert(!SRNOS_IMUTelemetryEncode(&sample,bytes,71));
    FILE *f=fopen(argv[1],"r"); assert(f);
    for(unsigned i=0;i<72;i++) { unsigned b; assert(fscanf(f,"%2x",&b)==1); assert(bytes[i]==b); } fclose(f);
    SRNOS_IMUTelemetryObserve(0,&sample,NULL); assert(streams[0].head==0);
    configs[0].on_config(configs[0].user,true,1000);
    sample.header.acquisition_start_us=500; sample.header.acquisition_end_us=1000;
    for(unsigned i=1;i<=65;i++) {sample.header.sequence=i;SRNOS_IMUTelemetryObserve(0,&sample,NULL);}
    assert(streams[0].head-streams[0].tail==64 && streams[0].overflow==1 && streams[0].enqueued==64);
    result=SRN_LINK_ERR_FULL; configs[0].poll(configs[0].user,10); assert(streams[0].tail==0);
    result=SRN_LINK_OK; configs[0].poll(configs[0].user,10); assert(streams[0].tail==6 && sent_len==432);
    configs[0].on_delivery(configs[0].user,6,SRN_LINK_DELIVERED); assert(streams[0].sent==6);
    configs[0].on_config(configs[0].user,false,1000); assert(streams[0].discarded==58);
    assert(streams[0].tail==streams[0].head);
    configs[1].on_config(configs[1].user,true,1000);
    SRNOS_IMUTelemetryObserve(1,&sample,NULL); now=4999;
    unsigned before=publications; configs[1].poll(configs[1].user,4); assert(before==publications);
    now=5000; configs[1].poll(configs[1].user,5); assert(publications==before+1);
    configs[1].on_delivery(configs[1].user,1,SRN_LINK_FAILED); assert(streams[1].failed==1);
    uint8_t status_bytes[92],flags;size_t len;srn_cobs_prot_view_t req={0};
    assert(status_handler(NULL,&req,status_bytes,92,&len,&flags)==0 && len==92 && status_bytes[6]==SRNOS_SENSOR_OFFLINE);
    assert(depth==0); puts("IMU telemetry: shared golden record, subscription, bounded queue, overflow, backpressure, timeout flush, completion and offline status passed");
}
