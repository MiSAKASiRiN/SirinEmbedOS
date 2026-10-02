#include "imu_calibration_protocol.h"
#include "SirinEmbedOS/Services/HostLink/IMUCalibrationProtocol.h"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SRNOS_HostLinkHandlerFn handlers[2];
static SRNOS_HostLinkDeferredFn deferred;
static SRNOS_IMUStaticStatus report;
static SRNOS_HostLinkResult registration_result=SRNOS_HOST_LINK_OK, submit_result=SRNOS_HOST_LINK_OK;
static unsigned calls, submissions;
static uint8_t submitted[2];
SRNOS_HostLinkResult SRNOS_HostLinkRegisterHandler(SRNOS_HostLinkRegistration *r,uint8_t s,uint8_t m,uint8_t k,SRNOS_HostLinkQos q,SRNOS_HostLinkHandlerFn fn,void *u) {
 (void)r;assert(s==SRNOS_HOST_LINK_SERVICE_CONFIG && m>=9 && m<=10);
 assert(k==SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST) && q==SRNOS_HOST_LINK_QOS_RELIABLE && !u);
 if(registration_result==SRNOS_HOST_LINK_OK)handlers[m-9]=fn;
 return registration_result;
}
SRNOS_HostLinkResult SRNOS_HostLinkSubmitDeferred(uint8_t s,uint8_t m,uint32_t seq,const uint8_t *p,size_t n,SRNOS_HostLinkDeferredFn fn,void *u) {
 assert(s==SRNOS_HOST_LINK_SERVICE_CONFIG && m==9 && seq==42 && n==2 && !u);
 submissions++;memcpy(submitted,p,2);deferred=fn;return submit_result;
}
bool SRNOS_IMUCalibrationGetStaticStatus(SRNOS_IMUStaticStatus *out) {*out=report;return true;}
SRNOS_IMUStaticResult SRNOS_IMUCalibrationRunStatic(uint8_t mask,SRNOS_IMUStaticStatus *out) {
 assert(mask==3);calls++;*out=report;return report.result;
}
static uint32_t le32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void) {
 uint8_t p[2]={1,3},out[128],flags;size_t n;
 SRNOS_ProtocolView req={.header={.service=SRNOS_HOST_LINK_SERVICE_CONFIG,.message=9,.sequence=42},.payload=p,.payload_len=2};
 assert(SRNOS_IMUCalibrationProtocolRegister(NULL,false)==SRNOS_HOST_LINK_OK);
 assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE);
 assert(SRNOS_IMUCalibrationProtocolRegister(NULL,true)==SRNOS_HOST_LINK_OK);
 req.payload_len=1;assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_BAD_LENGTH);req.payload_len=2;
 p[0]=2;assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);p[0]=1;
 p[1]=0;assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);
 p[1]=4;assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);p[1]=3;
 report.phase=SRNOS_IMU_STATIC_SAMPLING;
 assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_BUSY && !submissions);
 report.phase=SRNOS_IMU_STATIC_IDLE;
 assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL);
 assert(!calls && n==0 && flags==0 && submissions==1);
 report=(SRNOS_IMUStaticStatus){.job_id=0x12345678,.phase=SRNOS_IMU_STATIC_DONE,.requested_mask=3,.saved_mask=3,.failed_sensor=255,.elapsed_ms=5000,.samples={501,500},.saved_revision={7,8},.gyro_bias_rad_s={{.5f,0,0},{0,0,0}},.temperature_c={60,59}};
 assert(deferred(NULL,req.header.service,9,submitted,2,out,67,&n,&flags)==SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR && !calls);
 assert(deferred(NULL,req.header.service,9,submitted,2,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_OK);
 assert(calls==1 && n==68 && out[0]==1 && out[1]==4 && out[2]==3 && out[3]==3 && out[4]==255 && out[5]==0);
 assert(le32(out+8)==0x12345678 && le32(out+12)==5000 && le32(out+16)==501 && le32(out+20)==500);
 assert(le32(out+28)==7 && le32(out+32)==8 && le32(out+36)==0x3f000000 && le32(out+60)==0x42700000);
 report.phase=SRNOS_IMU_STATIC_FAILED;report.result=SRNOS_IMU_STATIC_STORAGE_ERROR;report.saved_mask=1;report.failed_sensor=1;report.storage_status[1]=SRNOS_STORAGE_IO_ERROR;
 assert(deferred(NULL,req.header.service,9,submitted,2,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_STORAGE_ERROR);
 assert(n==68 && out[3]==1 && out[4]==1 && out[6]==SRNOS_IMU_STATIC_STORAGE_ERROR && out[26]==SRNOS_STORAGE_IO_ERROR);
 report.result=SRNOS_IMU_STATIC_MOVING;
 assert(deferred(NULL,req.header.service,9,submitted,2,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_CALIBRATION_ERROR);
 req.header.message=10;req.payload_len=0;
 assert(handlers[1](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_OK && n==68 && out[6]==SRNOS_IMU_STATIC_MOVING);
 assert(handlers[1](NULL,&req,out,67,&n,&flags)==SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR && n==0);
 req.payload_len=1;assert(handlers[1](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_BAD_LENGTH);
 registration_result=SRNOS_HOST_LINK_ERR_BUSY;
 assert(SRNOS_IMUCalibrationProtocolRegister(NULL,false)==registration_result);
 req.header.message=9;req.payload_len=2;submit_result=SRNOS_HOST_LINK_ERR_BUSY;
 assert(handlers[0](NULL,&req,out,sizeof out,&n,&flags)==SRNOS_HOST_LINK_STATUS_BUSY);
 puts("IMU calibration protocol: validation, deferred completion, failure/partial save encoding, query and frozen registration passed");
}
