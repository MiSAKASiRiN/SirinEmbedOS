#include "imu_calibration_protocol.h"
#include "SirinEmbedOS/Services/HostLink/IMUCalibrationProtocol.h"
#include "SirinEmbedOS/Services/IMUCalibration.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include <string.h>
static bool enabled;
static bool encode(const SRNOS_IMUStaticStatus *s,uint8_t *out,size_t cap,size_t *length) {
    *length=0;if(cap<SRNOS_IMU_CALIBRATION_RESPONSE_BYTES)return false;
    memset(out,0,SRNOS_IMU_CALIBRATION_RESPONSE_BYTES);
    SRNOS_Memory_MutableByteSpan b=SRNOS_Memory_MutableBytes(out,SRNOS_IMU_CALIBRATION_RESPONSE_BYTES);
    out[0]=1;out[1]=(uint8_t)s->phase;out[2]=s->requested_mask;out[3]=s->saved_mask;out[4]=s->failed_sensor;
    SRNOS_Endian_StoreLE16(b,6,(uint16_t)s->result);
    SRNOS_Endian_StoreLE32(b,8,s->job_id);SRNOS_Endian_StoreLE32(b,12,s->elapsed_ms);
    for(unsigned i=0;i<2;i++) {
        SRNOS_Endian_StoreLE32(b,16+4*i,s->samples[i]);
        SRNOS_Endian_StoreLE16(b,24+2*i,(uint16_t)s->storage_status[i]);
        SRNOS_Endian_StoreLE32(b,28+4*i,s->saved_revision[i]);
        for(unsigned j=0;j<3;j++) {uint32_t bits;memcpy(&bits,&s->gyro_bias_rad_s[i][j],4);SRNOS_Endian_StoreLE32(b,36+i*12+j*4,bits);}
        uint32_t bits;memcpy(&bits,&s->temperature_c[i],4);SRNOS_Endian_StoreLE32(b,60+4*i,bits);
    }
    *length=SRNOS_IMU_CALIBRATION_RESPONSE_BYTES;return true;
}
static uint16_t status_code(SRNOS_IMUStaticResult r) {
    if(r==SRNOS_IMU_STATIC_OK)return SRNOS_HOST_LINK_STATUS_OK;
    if(r==SRNOS_IMU_STATIC_BUSY)return SRNOS_HOST_LINK_STATUS_BUSY;
    if(r==SRNOS_IMU_STATIC_STORAGE_ERROR)return SRNOS_HOST_LINK_STATUS_STORAGE_ERROR;
    if(r==SRNOS_IMU_STATIC_INVALID)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    /* Detailed physical failure lives in the versioned response body. */
    return SRNOS_HOST_LINK_STATUS_CALIBRATION_ERROR;
}
static uint16_t run(void *user,uint8_t service,uint8_t message,const uint8_t *request,size_t n,
                    uint8_t *out,size_t cap,size_t *length,uint8_t *flags) {
    (void)user;*length=0;*flags=0;
    if(service!=SRNOS_HOST_LINK_SERVICE_CONFIG || message!=SRNOS_IMU_CALIBRATION_COMMAND_MESSAGE || n!=2)
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    if(cap<SRNOS_IMU_CALIBRATION_RESPONSE_BYTES)return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    SRNOS_IMUStaticStatus report;
    SRNOS_IMUStaticResult r=SRNOS_IMUCalibrationRunStatic(request[1],&report);
    if(!encode(&report,out,cap,length))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    return status_code(r);
}
static uint16_t command(void *user,const SRNOS_ProtocolView *r,uint8_t *out,size_t cap,size_t *length,uint8_t *flags) {
    (void)user;(void)out;(void)cap;*length=0;*flags=0;
    if(r->payload_len!=2)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    if(r->payload[0]!=1 || !r->payload[1] || (r->payload[1]&~3u))return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
    if(!enabled)return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    SRNOS_IMUStaticStatus report;
    if(!SRNOS_IMUCalibrationGetStaticStatus(&report))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    if(report.phase>=SRNOS_IMU_STATIC_PREPARING && report.phase<=SRNOS_IMU_STATIC_SAVING)return SRNOS_HOST_LINK_STATUS_BUSY;
    return SRNOS_HostLinkSubmitDeferred(r->header.service,r->header.message,r->header.sequence,r->payload,2,run,NULL)==SRNOS_HOST_LINK_OK?
        SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL:SRNOS_HOST_LINK_STATUS_BUSY;
}
static uint16_t status(void *user,const SRNOS_ProtocolView *r,uint8_t *out,size_t cap,size_t *length,uint8_t *flags) {
    (void)user;*length=0;*flags=0;
    if(r->payload_len)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    SRNOS_IMUStaticStatus report;
    return SRNOS_IMUCalibrationGetStaticStatus(&report) && encode(&report,out,cap,length)?
        SRNOS_HOST_LINK_STATUS_OK:SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
}
SRNOS_HostLinkResult SRNOS_IMUCalibrationProtocolRegister(SRNOS_HostLinkRegistration *registration,bool allow) {
    SRNOS_HostLinkResult r=SRNOS_HostLinkRegisterHandler(registration,SRNOS_HOST_LINK_SERVICE_CONFIG,
        SRNOS_IMU_CALIBRATION_COMMAND_MESSAGE,SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),SRNOS_HOST_LINK_QOS_RELIABLE,command,NULL);
    if(r==SRNOS_HOST_LINK_OK)r=SRNOS_HostLinkRegisterHandler(registration,SRNOS_HOST_LINK_SERVICE_CONFIG,
        SRNOS_IMU_CALIBRATION_STATUS_MESSAGE,SRNOS_HOST_LINK_KIND_MASK(SRNOS_PROTOCOL_KIND_REQUEST),SRNOS_HOST_LINK_QOS_RELIABLE,status,NULL);
    if(r==SRNOS_HOST_LINK_OK)enabled=allow;
    return r;
}
