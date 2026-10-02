#ifndef SRNOS_PARAMETERS_HPP
#define SRNOS_PARAMETERS_HPP
#include "SirinEmbedOS/Services/Parameters/Parameters.h"
#include <type_traits>
namespace SRNOS::Parameters {
/* Declares a business parameter with an explicit stable ID, default and range.
 * Static construction or ApplicationConfigure only. Registry owns every value;
 * no writable references, borrowed value pointers, or automatic EEPROM writes. */
template<class T> class Persistent final {
    static_assert(std::is_same_v<T,float> || std::is_same_v<T,int32_t> ||
                  std::is_same_v<T,uint32_t> || std::is_same_v<T,bool>,"Unsupported persistent scalar");
    static SRNOS_ParameterValue encode(T v) {
        SRNOS_ParameterValue r{};
        if constexpr(std::is_same_v<T,float>) r.f32=v;
        else if constexpr(std::is_same_v<T,int32_t>) r.i32=v;
        else if constexpr(std::is_same_v<T,uint32_t>) r.u32=v;
        else r.boolean=v;
        return r;
    }
    static constexpr SRNOS_ParametersType type() {
        if constexpr(std::is_same_v<T,float>) return SRNOS_PARAMETERS_TYPE_FLOAT;
        else if constexpr(std::is_same_v<T,int32_t>) return SRNOS_PARAMETERS_TYPE_INT32;
        else if constexpr(std::is_same_v<T,uint32_t>) return SRNOS_PARAMETERS_TYPE_UINT32;
        else return SRNOS_PARAMETERS_TYPE_BOOL;
    }
public:
    Persistent(uint32_t stable_id,const char *name,T value,T minimum,T maximum):fallback_(value) {
        const SRNOS_PersistentParameterDefinition d{stable_id,name,type(),encode(value),encode(minimum),encode(maximum)};
        status_=SRNOS_ParametersRegisterPersistent(&d,&index_);
    }
    Persistent(const Persistent&)=delete;
    Persistent& operator=(const Persistent&)=delete;
    SRNOS_ParametersStatus RegistrationStatus() const { return status_; }
    uint16_t Index() const { return index_; }
    SRNOS_ParametersStatus Set(T value) {
        const auto v=encode(value);
        return status_==SRNOS_PARAMETERS_STATUS_OK?SRNOS_ParametersSet(index_,&v):status_;
    }
    T Get() const {
        SRNOS_ParameterValue v{};
        if(status_!=SRNOS_PARAMETERS_STATUS_OK || SRNOS_ParametersGet(index_,&v)!=SRNOS_PARAMETERS_STATUS_OK) return fallback_;
        if constexpr(std::is_same_v<T,float>) return v.f32;
        else if constexpr(std::is_same_v<T,int32_t>) return v.i32;
        else if constexpr(std::is_same_v<T,uint32_t>) return v.u32;
        else return v.boolean;
    }
private:
    uint16_t index_=UINT16_MAX;
    SRNOS_ParametersStatus status_=SRNOS_PARAMETERS_STATUS_NOT_INITIALIZED;
    T fallback_;
};
}
#endif
