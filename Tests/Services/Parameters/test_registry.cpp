#include "SirinEmbedOS/Services/Parameters/lib_params.hpp"
#include "SirinEmbedOS/Devices/EEPROM/lib_24c02_bl.h"
#include <atomic>
#include <thread>
#include <cassert>
#include <cstring>
#include <cstdio>
static uint8_t eeprom[256];
extern "C" {
uint32_t HAL_GetTick(void) { return 0; }
int SRN_DebugPort_WriteExact(const void *, size_t) { return 0; }
lib_24c02_bl_status_t lib_24c02_bl_init(lib_24c02_bl_t *,bsp_i2c_bus_id_t,uint16_t) { return LIB_24C02_BL_OK; }
lib_24c02_bl_status_t lib_24c02_bl_read(lib_24c02_bl_t *,uint8_t addr,uint8_t *out,size_t n,uint32_t) { std::memcpy(out,eeprom+addr,n); return LIB_24C02_BL_OK; }
lib_24c02_bl_status_t lib_24c02_bl_write(lib_24c02_bl_t *,uint8_t addr,const uint8_t *src,size_t n,uint32_t,uint32_t) { std::memcpy(eeprom+addr,src,n); return LIB_24C02_BL_OK; }
}
int main()
{
    using omx::params::Param;
    Param<uint32_t> a("pid.a",1,0,100000,PARAMS_MUTABLE_BUSINESS);
    Param<uint32_t> b("pid.b",1,0,100000,PARAMS_MUTABLE_BUSINESS);
    Param<uint32_t> immutable("read_only",7,0,100);
    assert(SRNOS_ParametersFreeze()==PARAMS_STATUS_OK);
    Param<uint32_t> late("late",0,0,1,PARAMS_MUTABLE_BUSINESS);
    assert(late.RegistrationStatus()==PARAMS_STATUS_LOCKED);
    assert(params_set_value("read_only","2")==PARAMS_STATUS_READ_ONLY);
    params_entry_info_t info{};
    assert(params_get_by_name("pid.a",&info)==PARAMS_STATUS_OK && info.access==PARAMS_MUTABLE_BUSINESS);
    assert(params_get_by_name("read_only",&info)==PARAMS_STATUS_OK && info.access==PARAMS_READ_ONLY);
    assert(params_set_value("pid.a","100001")==PARAMS_STATUS_OUT_OF_RANGE);
    assert(a.Get()==1);
    uint32_t good=8,bad=100001;
    params_binary_write_t writes[]={{0,reinterpret_cast<const uint8_t *>(&good),4},{1,reinterpret_cast<const uint8_t *>(&bad),4}};
    size_t failed=9;
    assert(params_set_binary_batch(writes,2,&failed)==PARAMS_STATUS_OUT_OF_RANGE && failed==1);
    assert(a.Get()==1 && b.Get()==1);
    const uint16_t indices[]={0,1};
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for(uint32_t i=0;i<10000;++i) {
            writes[0].data=reinterpret_cast<const uint8_t *>(&i); writes[1].data=reinterpret_cast<const uint8_t *>(&i);
            assert(params_set_binary_batch(writes,2,nullptr)==PARAMS_STATUS_OK);
        }
        done=true;
    });
    do {
        uint32_t values[2]; size_t n=0;
        assert(params_snapshot(indices,2,reinterpret_cast<uint8_t *>(values),sizeof(values),&n)==PARAMS_STATUS_OK);
        assert(n==8 && values[0]==values[1]);
    } while(!done.load());
    writer.join();
    assert(params_save()==PARAMS_STATUS_OK);
    assert(a.Set(123)==PARAMS_STATUS_OK);
    assert(immutable.Set(22)==PARAMS_STATUS_OK);
    assert(params_load()==PARAMS_STATUS_OK && a.Get()==9999 && immutable.Get()==22);
    eeprom[0x40+20]^=1;
    assert(a.Set(123)==PARAMS_STATUS_OK);
    assert(params_load()==PARAMS_STATUS_CRC_ERROR && a.Get()==123);
    std::puts("Parameter freeze/access/atomic batch/snapshot/persistence: PASS");
}
