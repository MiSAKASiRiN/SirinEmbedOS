#ifndef SRNOS_TELEMETRY_HPP
#define SRNOS_TELEMETRY_HPP
#include "SirinEmbedOS/Services/Telemetry.h"
#include <type_traits>
namespace SRNOS { namespace Telemetry {
template<class T> class Value {
    static_assert(std::is_same<T,float>::value || std::is_same<T,int32_t>::value ||
        std::is_same<T,uint32_t>::value || std::is_same<T,bool>::value,
        "Telemetry supports float, int32_t, uint32_t and bool only");
    SRNOS_TelemetryHandle handle_=SRNOS_TELEMETRY_INVALID_HANDLE;
    SRNOS_TelemetryResult status_;
    static constexpr SRNOS_TelemetryType type() {
        return std::is_same<T,float>::value ? SRNOS_TELEMETRY_FLOAT32 :
            std::is_same<T,int32_t>::value ? SRNOS_TELEMETRY_INT32 :
            std::is_same<T,uint32_t>::value ? SRNOS_TELEMETRY_UINT32 : SRNOS_TELEMETRY_BOOL;
    }
public:
    Value(const char *name,const char *unit,uint16_t maxHz) {
        const SRNOS_TelemetryDescriptor d={name,unit,type(),maxHz};
        status_=SRNOS_TelemetryRegister(&d,&handle_);
    }
    Value(const Value&)=delete; Value& operator=(const Value&)=delete;
    SRNOS_TelemetryResult Status() const { return status_; }
    SRNOS_TelemetryHandle Handle() const { return handle_; }
    SRNOS_TelemetryResult Publish(T value,uint64_t sampledUs,bool valid=true) const {
        SRNOS_TelemetryValue v={};
        if constexpr(std::is_same<T,float>::value)v.f32=value;
        else if constexpr(std::is_same<T,int32_t>::value)v.i32=value;
        else if constexpr(std::is_same<T,uint32_t>::value)v.u32=value;
        else v.boolean=value;
        return status_==SRNOS_TELEMETRY_OK ? SRNOS_TelemetryPublish(handle_,v,sampledUs,valid) : status_;
    }
};
} }
#endif
