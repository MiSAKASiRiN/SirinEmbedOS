// Included inside the example's anonymous namespace; only public SDK APIs.
uint32_t read_u32(const uint8_t *p,size_t n,size_t at) {
    uint32_t v=0;SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(p,n),at,&v);return v;
}
float read_f32(const uint8_t *p,size_t n,size_t at) {auto bits=read_u32(p,n,at);float v;std::memcpy(&v,&bits,4);return v;}
template<class T> bool encode_words(const T &v,uint8_t *out,size_t cap,size_t *len) {
    static_assert(sizeof(T)%4==0);if(cap<sizeof(T))return false;
    for(size_t at=0;at<sizeof(T);at+=4){uint32_t w;std::memcpy(&w,reinterpret_cast<const uint8_t*>(&v)+at,4);SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(out,cap),at,w);}
    *len=sizeof(T);return true;
}
uint16_t save_turntable(void *,uint8_t,uint8_t,const uint8_t *data,size_t n,uint8_t *out,size_t cap,size_t *len,uint8_t *flags) {
    *flags=0;*len=0;uint32_t mask=0;
    for(unsigned i=0;i<2;i++) {
        const size_t at=5+52*i;SRNOS_IMUCalibration c{};c.sensor=static_cast<SRNOS_SensorId>(i);c.model_version=2;c.axes_version=1;
        for(unsigned j=0;j<3;j++){c.gyro_bias_rad_s[j]=read_f32(data,n,at+4*j);c.accel_bias_m_s2[j]=read_f32(data,n,at+12+4*j);c.accel_scale[j]=read_f32(data,n,at+24+4*j);c.gyro_scale[j]=read_f32(data,n,at+40+4*j);}c.temperature_c=read_f32(data,n,at+36);
        const auto result=SRNOS_IMUCalibrationSave(&c,5000);calibration_job.store[i].store(result);if(result==SRNOS_STORAGE_OK)mask|=1u<<i;
    }
    SRNOS_IMUCalibrationStatus c{};SRNOS_IMUCalibrationGetStatus(&c);
    for(unsigned i=0;i<2;i++)calibration_job.revisions[i].store(c.saved_revision[i]);
    calibration_job.saved.store(mask);calibration_job.saving.store(2);
    uint32_t response[5]={mask,uint32_t(c.save_status[0]),uint32_t(c.save_status[1]),c.saved_revision[0],c.saved_revision[1]};
    if(!encode_words(response,out,cap,len))return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    return mask==3?SRNOS_HOST_LINK_STATUS_OK:SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
}
uint16_t turntable_command(void *,const SRNOS_ProtocolView *r,uint8_t *out,size_t cap,size_t *len,uint8_t *flags) {
    *len=0;*flags=0;if(!r->payload_len)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    Turntable::JobStatus s{};if(!calibration_job.published.get(s))return SRNOS_HOST_LINK_STATUS_BUSY;
    const auto *p=r->payload;const auto n=r->payload_len;
    const uint32_t job=n>=5?read_u32(p,n,1):0;
    switch(p[0]) {
    case 0: case 2:
        if(n!=(p[0]==0?1:5))return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        if(p[0]==2&&job!=s.job)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
        if(p[0]==2)calibration_job.heartbeat.store(SRNOS_TimeMs());
        return encode_words(s,out,cap,len)?SRNOS_HOST_LINK_STATUS_OK:SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    case 1:
        if(n!=9)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        if(!job||read_u32(p,n,5)!=Turntable::RecipeSignature)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
        if(s.phase>=1&&s.phase<=8)return SRNOS_HOST_LINK_STATUS_BUSY;
        if(job==s.job||inhibit.load()||calibration_job.start.load()||calibration_job.saving.load())return SRNOS_HOST_LINK_STATUS_BUSY;
        calibration_job.abort.store(0);calibration_job.heartbeat.store(SRNOS_TimeMs());calibration_job.start.store(job);return SRNOS_HOST_LINK_STATUS_OK;
    case 3: {
        if(n!=9)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        const uint32_t index=read_u32(p,n,5);if(job!=s.job||index>=s.records||index>=24)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
        Turntable::CalibrationRecord record{};if(!calibration_job.records[index].get(record))return SRNOS_HOST_LINK_STATUS_BUSY;
        return encode_words(record,out,cap,len)?SRNOS_HOST_LINK_STATUS_OK:SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    case 4:
        if(n!=109)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        if(job!=s.job||s.phase!=uint32_t(Turntable::JobPhase::Result)||s.records!=24||calibration_job.saving.load())return SRNOS_HOST_LINK_STATUS_BUSY;
        for(unsigned i=0;i<2;i++) {
            const size_t at=5+52*i;SRNOS_IMUCalibration c{};c.sensor=static_cast<SRNOS_SensorId>(i);c.model_version=2;c.axes_version=1;
            for(unsigned j=0;j<3;j++){c.gyro_bias_rad_s[j]=read_f32(p,n,at+4*j);c.accel_bias_m_s2[j]=read_f32(p,n,at+12+4*j);c.accel_scale[j]=read_f32(p,n,at+24+4*j);c.gyro_scale[j]=read_f32(p,n,at+40+4*j);}c.temperature_c=read_f32(p,n,at+36);
            if(SRNOS_IMUCalibrationValidate(&c)!=SRNOS_STORAGE_OK)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
        }
        calibration_job.saving.store(1);
        if(SRNOS_HostLinkSubmitDeferred(r->header.service,r->header.message,r->header.sequence,p,n,save_turntable,nullptr)!=SRNOS_HOST_LINK_OK){calibration_job.saving.store(0);return SRNOS_HOST_LINK_STATUS_BUSY;}
        return SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL;
    case 5:
        if(n!=5)return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        if(job!=s.job)return SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT;
        if(calibration_job.saving.load())return SRNOS_HOST_LINK_STATUS_BUSY;
        calibration_job.abort.store(1);return SRNOS_HOST_LINK_STATUS_OK;
    default:return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    }
}
