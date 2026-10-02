#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "Services/Parameters/parameters_internal.h"
#include "Services/Storage/storage_internal.h"
#include <atomic>
#include <thread>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <limits>
static uint8_t saved[SRNOS_STORAGE_PARAMETERS_MAX_BYTES];
static size_t saved_length;
static uint32_t stored_owner=0x12345678, revision;
static uint16_t stored_schema=1;
static unsigned reads,writes;
static bool late_crc;
static SRNOS_StorageStatus read_result=SRNOS_STORAGE_OK,write_result=SRNOS_STORAGE_OK;
static void (*read_hook)();
static void (*write_hook)();
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=(uint32_t)p[i]<<(8*i);return v;}
extern "C" {
int SRNOS_DebugPortWriteExact(const void *,size_t){return 0;}
SRNOS_StorageStatus SRNOS_StorageVisitRecord(SRNOS_StorageRecord r,uint32_t owner,uint16_t schema,SRNOS_StorageVisitor visitor,void *user,size_t *length,uint32_t *rev,uint32_t){
    assert(r==SRNOS_STORAGE_RECORD_PARAMETERS);++reads;*length=0;*rev=0;
    if(read_hook)read_hook();
    if(read_result!=SRNOS_STORAGE_OK)return read_result;
    if(!saved_length)return SRNOS_STORAGE_NOT_FOUND;
    if(owner!=stored_owner||schema!=stored_schema)return SRNOS_STORAGE_INCOMPATIBLE;
    for(size_t offset=0;offset<saved_length;){size_t n=saved_length-offset;if(n>17)n=17; // Deliberately split every possible field boundary.
        if(!visitor(user,offset,saved+offset,n))return SRNOS_STORAGE_CORRUPT;offset+=n;}
    if(late_crc)return SRNOS_STORAGE_CORRUPT;
    *length=saved_length;*rev=revision;return SRNOS_STORAGE_OK;
}
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord r,uint32_t owner,uint16_t schema,const void *p,size_t length,uint32_t *rev,uint32_t){
    assert(r==SRNOS_STORAGE_RECORD_PARAMETERS);++writes;
    if(write_hook)write_hook();
    if(write_result!=SRNOS_STORAGE_OK)return write_result;
    assert(length<=sizeof(saved));std::memcpy(saved,p,length);saved_length=length;stored_owner=owner;stored_schema=schema;*rev=++revision;return SRNOS_STORAGE_OK;
}
}
using SRNOS::Parameters::Persistent;
static SRNOS_ParametersState state(){SRNOS_ParametersState s{};assert(SRNOS_ParametersGetState(&s)==SRNOS_PARAMETERS_STATUS_OK);return s;}
static void set_a_123(){assert(SRNOS_ParametersSetValue("pid.a","123")==SRNOS_PARAMETERS_STATUS_OK);assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_EEPROM_BUSY);}
int main(int argc,char **argv){
    if(argc>1&&!std::strcmp(argv[1],"capacity")){
        for(uint32_t i=1;i<=SRNOS_PARAMETERS_MAX_COUNT;++i){char name[20];std::snprintf(name,sizeof(name),"p%u",i);SRNOS_PersistentParameterDefinition d{};d.stable_id=i;d.name=name;d.type=SRNOS_PARAMETERS_TYPE_UINT32;d.maximum.u32=10;uint16_t index;assert(SRNOS_ParametersRegisterPersistent(&d,&index)==SRNOS_PARAMETERS_STATUS_OK);}
        Persistent<bool> over(1000,"over",false,false,true);assert(over.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_NO_SPACE);
        std::puts("Parameters bounded capacity: PASS");return 0;
    }
    if(argc>1&&!std::strcmp(argv[1],"definitions")){
        Persistent<float> a(7,"a",1.f,0.f,2.f);Persistent<float> duplicate(7,"b",1.f,0.f,2.f);
        assert(a.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_OK);assert(duplicate.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_DUPLICATE);
        Persistent<float> bad(8,"bad",NAN,0.f,2.f);assert(bad.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
        assert(SRNOS_ParametersFreeze()!=SRNOS_PARAMETERS_STATUS_OK);std::puts("Parameters definition checks: PASS");return 0;
    }
    if(argc>1&&!std::strcmp(argv[1],"zero")){
        Persistent<float> zero(0,"bad-id",1.f,0.f,2.f);
        assert(zero.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);
        SRNOS_ParametersConfigure(0x12345678,1);
        assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);return 0;
    }
    if(argc>1&&!std::strcmp(argv[1],"null")){
        uint16_t index;
        assert(SRNOS_ParametersRegisterPersistent(nullptr,&index)==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);
        SRNOS_ParametersConfigure(0,1);
        assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);return 0;
    }
    if(argc>1&&!std::strcmp(argv[1],"empty")){
        SRNOS_ParametersConfigure(0,1);assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_OK);assert(SRNOS_ParametersLoadStartup(0)==SRNOS_PARAMETERS_STATUS_OK);assert(reads==0);return 0;
    }
    Persistent<uint32_t> a(30,"pid.a",1,0,100000),b(10,"pid.b",1,0,100000);
    Persistent<float> gain(20,"gain",1.f,0.f,10.f);
    SRNOS_PersistentParameterDefinition d{};char name[]="enabled";d.stable_id=40;d.name=name;d.type=SRNOS_PARAMETERS_TYPE_BOOL;d.maximum.boolean=true;uint16_t bool_index;
    assert(SRNOS_ParametersRegisterPersistent(&d,&bool_index)==SRNOS_PARAMETERS_STATUS_OK);name[0]='X';
    assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);
    SRNOS_ParametersConfigure(stored_owner,1);assert(SRNOS_ParametersFreeze()==SRNOS_PARAMETERS_STATUS_OK);
    Persistent<uint32_t> late(50,"late",0,0,1);assert(late.RegistrationStatus()==SRNOS_PARAMETERS_STATUS_LOCKED);
    SRNOS_ParametersEntryInfo info{};assert(SRNOS_ParametersGetByIndex(bool_index,&info)==SRNOS_PARAMETERS_STATUS_OK&&!std::strcmp(info.name,"enabled"));
    assert(SRNOS_ParametersSetValue("pid.a","100001")==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
    assert(SRNOS_ParametersSetValue("pid.a"," -1")==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
    assert(SRNOS_ParametersSetValue("gain","nan")==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
    assert(gain.Set(INFINITY)==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
    assert(SRNOS_ParametersSetValue("gain","1x")==SRNOS_PARAMETERS_STATUS_PARSE_ERROR);
    uint8_t badbool=2;assert(SRNOS_ParametersSetBinaryByIndex(bool_index,&badbool,1)==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE);
    uint8_t good[4],bad[4];put32(good,8);put32(bad,100001);
    SRNOS_ParametersBinaryWrite batch[]={{a.Index(),good,4},{b.Index(),bad,4}};size_t failed=9;
    assert(SRNOS_ParametersSetBinaryBatch(batch,2,&failed)==SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE&&failed==1&&a.Get()==1&&b.Get()==1);
    assert(SRNOS_ParametersLoadStartup(2000)==SRNOS_PARAMETERS_STATUS_NOT_FOUND&&state().defaulted==4);
    const uint16_t indices[]={a.Index(),b.Index()};std::atomic<bool> done{false};
    std::thread writer([&]{for(uint32_t i=0;i<10000;++i){uint8_t value[4];put32(value,i);SRNOS_ParametersBinaryWrite w[]={{a.Index(),value,4},{b.Index(),value,4}};assert(SRNOS_ParametersSetBinaryBatch(w,2,nullptr)==SRNOS_PARAMETERS_STATUS_OK);}done=true;});
    do{uint8_t values[8];size_t n;assert(SRNOS_ParametersSnapshot(indices,2,values,sizeof(values),&n)==SRNOS_PARAMETERS_STATUS_OK);assert(n==8&&get32(values)==get32(values+4));}while(!done.load());writer.join();
    assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_OK&&!state().dirty);
    const auto generation=state().generation;const auto nw=writes;
    assert(a.Set(a.Get())==SRNOS_PARAMETERS_STATUS_OK&&state().generation==generation);
    assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_OK&&writes==nw);
    assert(a.Set(55)==SRNOS_PARAMETERS_STATUS_OK);write_hook=set_a_123;
    assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_OK&&state().dirty&&state().saved_generation<state().generation);write_hook=nullptr;
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&a.Get()==55&&!state().dirty);
    read_hook=set_a_123;assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_CONFLICT&&a.Get()==123);read_hook=nullptr;
    // Registration order 30/10/20 differs from canonical persisted ID order.
    assert(get32(saved+8)==10&&get32(saved+20)==20&&get32(saved+32)==30);
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&a.Get()==55&&b.Get()==9999);
    // A legacy image larger than the current registry streams unknown IDs.
    const size_t short_length=saved_length;
    for(uint32_t i=0;i<40;++i){uint8_t *p=saved+saved_length;std::memset(p,0,12);put32(p,1000+i);p[4]=SRNOS_PARAMETERS_TYPE_UINT32;p[5]=4;put32(p+8,i);saved_length+=12;}
    saved[6]=44;assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&state().ignored==40&&state().loaded==4);
    assert(a.Set(77)==SRNOS_PARAMETERS_STATUS_OK);late_crc=true;
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_CRC_ERROR&&a.Get()==77);late_crc=false;
    saved_length=short_length;saved[6]=4;
    // An old unknown ID is ignored; a newly added parameter uses its default.
    put32(saved+8,5);assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&b.Get()==1&&state().ignored==1&&state().defaulted==1&&state().dirty);
    // Duplicate ID invalidates the image instead of choosing an arbitrary value.
    put32(saved+8,get32(saved+20));assert(a.Set(77)==SRNOS_PARAMETERS_STATUS_OK);
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_BAD_DATA&&a.Get()==77);
    read_result=SRNOS_STORAGE_CORRUPT;assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_CRC_ERROR&&a.Get()==77);
    assert(SRNOS_ParametersLoadStartup(2000)==SRNOS_PARAMETERS_STATUS_CRC_ERROR&&a.Get()==1&&state().defaulted==4);
    read_result=SRNOS_STORAGE_OK;assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_OK);
    // A type or range change defaults only the incompatible entry.
    saved[8+4]=SRNOS_PARAMETERS_TYPE_FLOAT;assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&state().defaulted==1);
    assert(SRNOS_ParametersSave()==SRNOS_PARAMETERS_STATUS_OK);put32(saved+8+8,100001);
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_OK&&a.Get()==1&&state().defaulted==1);
    ++stored_schema;assert(a.Set(42)==SRNOS_PARAMETERS_STATUS_OK);
    assert(SRNOS_ParametersLoad()==SRNOS_PARAMETERS_STATUS_LAYOUT_MISMATCH&&a.Get()==42);
    const auto nr=reads;assert(SRNOS_ParametersLoadStartup(0)==SRNOS_PARAMETERS_STATUS_TIMEOUT&&reads==nr&&a.Get()==1);
    assert(a.Set(42)==SRNOS_PARAMETERS_STATUS_OK);write_result=SRNOS_STORAGE_TIMEOUT;
    assert(SRNOS_ParametersSaveWithTimeout(1)==SRNOS_PARAMETERS_STATUS_TIMEOUT&&a.Get()==42&&state().dirty&&!state().saved_valid);
    assert(SRNOS_ParametersLoadWithTimeout(UINT32_MAX)==SRNOS_PARAMETERS_STATUS_INVALID_PARAM);
    std::puts("Persistent C/C++ registry, snapshots, stable IDs, defaults, generations and storage races: PASS");
}
