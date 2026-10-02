#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "Services/Parameters/parameters_internal.h"
#include "Services/Storage/storage_internal.h"
#include <cassert>
#include <cerrno>
/* Private context adapter: no target/RTOS dependency in the parent interface.
 * A rejected ISR call must not run libc parsing against the interrupted TLS. */
static bool task_context=true;
namespace SRNOS::Parameters::detail {
uint32_t LockValues(){return 0;}
void UnlockValues(uint32_t){}
bool TaskContext(){return task_context;}
}
extern "C" {
int SRNOS_DebugPortWriteExact(const void *,size_t){return 0;}
SRNOS_StorageStatus SRNOS_StorageVisitRecord(SRNOS_StorageRecord,uint32_t,uint16_t,SRNOS_StorageVisitor,void *,size_t *,uint32_t *,uint32_t){assert(false);return SRNOS_STORAGE_DISABLED;}
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord,uint32_t,uint16_t,const void *,size_t,uint32_t *,uint32_t){assert(false);return SRNOS_STORAGE_DISABLED;}
}
int main(){
    SRNOS::Parameters::Persistent<float> gain(1,"gain",1.f,0.f,10.f);
    SRNOS_ParametersConfigure(1,1);assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_OK);
    task_context=false;errno=EDOM;
    assert(SRNOS_ParametersSetValueByIndex(gain.Index(),"1e1000")==SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR);
    assert(errno==EDOM);
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR);
    assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR);
    task_context=true;assert(gain.Get()==1.f);
}
