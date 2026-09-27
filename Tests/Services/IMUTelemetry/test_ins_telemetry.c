#include "SirinEmbedOS/Services/HostLink/INSTelemetry.h"
#include "SirinEmbedOS/Kernel/System.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned depth,config_count,publications;
static uint64_t now=100000;
static tskptt_host_comm_telemetry_config_t configs[3];
static srn_link_handler_fn handler;
static SRNOS_SystemConfig system_config;
static SRNOS_INSStatus ins_status;
static srn_link_result_t send_result=SRN_LINK_OK;
void test_task_enter_critical(void){++depth;}
void test_task_exit_critical(void){assert(depth);--depth;}
uint64_t DWT_GetUptimeUs(void){return now;}
bool SRNOS_INSGetStatus(SRNOS_INSStatus *s){*s=ins_status;return true;}
const SRNOS_SystemConfig *SRNOS_SystemGetConfig(void){return &system_config;}
srn_link_result_t TSKPTT_HostComm_RegisterTelemetryEx(const tskptt_host_comm_telemetry_config_t *c)
{assert(config_count<3);configs[config_count++]=*c;return SRN_LINK_OK;}
srn_link_result_t srn_link_register_handler(srn_link_t *l,uint8_t s,uint8_t m,uint8_t k,srn_link_qos_t q,srn_link_handler_fn fn,void *u)
{(void)l;(void)k;(void)q;(void)u;assert(s==4&&m==9);handler=fn;return SRN_LINK_OK;}
srn_link_result_t TSKPTT_HostComm_PublishTelemetry(uint16_t id,uint32_t seq,uint32_t time,uint16_t period,uint8_t count,const uint8_t *data,size_t n)
{assert(id>=0x102&&id<=0x104&&seq&&time&&period&&count&&data&&n<=432&&!depth);++publications;return send_result;}
#include "../../../../Source/Services/HostLink/ins_telemetry.c"
static void golden(const char *directory,const char *name,const uint8_t *bytes,unsigned n){
 char path[1024];snprintf(path,sizeof(path),"%s/%s",directory,name);FILE *f=fopen(path,"r");assert(f);
 for(unsigned i=0;i<n;i++){unsigned b;assert(fscanf(f,"%2x",&b)==1);assert(bytes[i]==b);}fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);uint8_t bytes[256];
 SRNOS_INSSnapshot snap={0};
 snap.status.phase=SRNOS_INS_RUNNING;
 snap.attitude=(SRNOS_INSAttitude){.sequence=0x12345678,.epoch=7,.flags=769,
 .attitude_us=UINT64_C(0x1122334455667788),.published_us=UINT64_C(0x11223344556677aa),
 .input_sequence={12,13},.input_age_us={100,200},.source_mask=3,.new_sample_mask=3,
 .weights={.5f,.5f},.quaternion_wxyz={1,0,0,0},.euler_rad={.25f,-.5f,1},
 .gyro_rad_s={.125f,-.25f,.5f},.accel_m_s2={1.25f,-2.5f,9.75f}};
 snap.magnetic=(SRNOS_MagneticSample){.header={5,20,1000,1100,SRNOS_SENSOR_READY},
 .raw_magnetic={-32768,-123,32767},.raw_temperature=-42,.magnetic_uT={1.25f,-2.5f,9.75f},.temperature_c=23.5f};
 snap.pressure=(SRNOS_PressureSample){.header={6,24,2000,2100,SRNOS_SENSOR_READY},
 .raw_pressure=-123456,.raw_temperature=765432,.pressure_pa=101325,.temperature_c=23.5f};
 assert(SRNOS_INSEncodeAttitude(&snap.attitude,snap.status.phase,bytes,108));golden(argv[1],"golden_ins.hex",bytes,108);
 assert(SRNOS_INSEncodeMagnetic(&snap.magnetic,bytes,52));golden(argv[1],"golden_mag.hex",bytes,52);
 assert(SRNOS_INSEncodePressure(&snap.pressure,bytes,44));golden(argv[1],"golden_pressure.hex",bytes,44);
 assert(!SRNOS_INSEncodeAttitude(&snap.attitude,SRNOS_INS_RUNNING,bytes,107));
 assert(SRNOS_INSTelemetryRegister(NULL,NULL)==0&&config_count==3);
 assert(SRNOS_INSTelemetryRegister(NULL,NULL)==SRN_LINK_ERR_BUSY);
 assert(configs[0].required_period_us==1000&&configs[1].required_period_us==10000&&configs[2].required_period_us==62500);
 SRNOS_INSTelemetryObserve(&snap);assert(!streams[0].enqueued);
 for(unsigned i=0;i<3;i++)configs[i].on_config(configs[i].user,true,configs[i].required_period_us);
 SRNOS_INSTelemetryObserve(&snap);assert(!streams[0].enqueued); /* subscription begins with NEW samples */
 snap.attitude.attitude_us=1100;
 for(unsigned n=1;n<=65;n++){
  snap.attitude.sequence=n;snap.magnetic.header.sequence=n;snap.pressure.header.sequence=n;
  SRNOS_INSTelemetryObserve(&snap);SRNOS_INSTelemetryObserve(&snap); /* repeats never enqueue twice */
 }
 assert(streams[0].enqueued==64&&streams[0].overflow==1);
 assert(streams[1].enqueued==16&&streams[1].overflow==49);
 assert(streams[2].enqueued==8&&streams[2].overflow==57);
 send_result=SRN_LINK_ERR_FULL;configs[0].poll(configs[0].user,100);assert(streams[0].tail==0);
 send_result=SRN_LINK_OK;
 for(unsigned i=0;i<3;i++){configs[i].poll(configs[i].user,100);assert(streams[i].tail>0);}
 assert(streams[0].tail==4&&streams[1].tail==8&&streams[2].tail==8);
 configs[0].on_delivery(configs[0].user,4,SRN_LINK_FAILED);assert(streams[0].failed==4);
 for(unsigned i=0;i<3;i++){configs[i].on_config(configs[i].user,false,0);assert(streams[i].tail==streams[i].head);}
 assert(streams[0].discarded==60);
 configs[0].on_config(configs[0].user,true,1000);
 snap.attitude.flags=0;snap.attitude.sequence++;SRNOS_INSTelemetryObserve(&snap);assert(streams[0].head==64);
 snap.attitude.flags=SRNOS_INS_VALID;snap.attitude.sequence++;snap.attitude.attitude_us=now;
 SRNOS_INSTelemetryObserve(&snap);unsigned before=publications;
 configs[0].poll(configs[0].user,100);assert(before==publications);
 now+=4000;configs[0].poll(configs[0].user,104);assert(publications==before+1);
 ins_status.phase=SRNOS_INS_RUNNING;ins_status.heater.source=-1;
 uint8_t flags;size_t len;srn_cobs_prot_view_t req={0};
 assert(handler(NULL,&req,bytes,sizeof(bytes),&len,&flags)==0&&len==256&&bytes[64]==255);
 assert(!depth);puts("INS telemetry: golden records, new-only streams, overflow, fair batches, lifecycle and status passed");
}
