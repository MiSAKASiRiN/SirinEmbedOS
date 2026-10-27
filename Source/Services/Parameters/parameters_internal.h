#ifndef SRNOS_PARAMETERS_INTERNAL_H
#define SRNOS_PARAMETERS_INTERNAL_H
#include "SirinEmbedOS/Services/Parameters/Parameters.h"
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_ParametersConfigure(uint32_t application_id,uint16_t compatibility_version);
SRNOS_ParametersStatus SRNOS_ParametersLoadStartup(uint32_t timeout_ms);
#ifdef __cplusplus
}
namespace SRNOS::Parameters::detail {
uint32_t LockValues();
void UnlockValues(uint32_t);
bool TaskContext();
class ValueGuard {
    uint32_t state_;
public:
    ValueGuard():state_(LockValues()){}
    ~ValueGuard(){UnlockValues(state_);}
    ValueGuard(const ValueGuard&)=delete;
    ValueGuard& operator=(const ValueGuard&)=delete;
};
}
#endif
#endif
