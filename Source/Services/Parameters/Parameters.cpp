#include "parameters_internal.h"
#include "Services/Storage/storage_internal.h"
#include "Services/Logging/debug_port_internal.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgParams SRNOS_DebugDbgParams;
namespace {
using SRNOS::Parameters::detail::ValueGuard;
using Value=SRNOS_ParameterValue;
static_assert(sizeof(float)==4 && sizeof(Value)==4,"CONFIG values require binary32 scalars");
struct Entry {
    uint32_t id;
    char name[SRNOS_PARAMETERS_MAX_NAME_LEN+1u];
    SRNOS_ParametersType type;
    Value value,initial,minimum,maximum;
};
static Entry entries[SRNOS_PARAMETERS_MAX_COUNT];
static size_t count;
static bool frozen, configured;
static SRNOS_ParametersStatus registration_error=SRNOS_PARAMETERS_STATUS_OK;
static SRNOS_ParametersState state{};
constexpr uint32_t magic=0x31505253u; /* SRP1, little endian */
constexpr size_t header_bytes=8u, item_bytes=12u;
/* SAVE owns a bounded immutable image. LOAD streams into tentative values. */
static uint8_t image[header_bytes+item_bytes*SRNOS_PARAMETERS_MAX_COUNT];
static_assert(SRNOS_PARAMETERS_MAX_COUNT>0 && SRNOS_PARAMETERS_MAX_COUNT<=255,"parameter count must fit CONFIG wire count");
static_assert(sizeof(image)<=SRNOS_STORAGE_PARAMETERS_MAX_BYTES,"parameter image exceeds partition");
static uint16_t size_of(SRNOS_ParametersType t) { return t==SRNOS_PARAMETERS_TYPE_BOOL?1u:4u; }
static bool context() { return SRNOS::Parameters::detail::TaskContext(); }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t get32(const uint8_t *p) { uint32_t r=0;for(unsigned i=0;i<4;++i)r|=(uint32_t)p[i]<<(i*8);return r; }
static void encode(SRNOS_ParametersType t,Value v,uint8_t *p) {
    uint32_t bits=0;
    if(t==SRNOS_PARAMETERS_TYPE_BOOL){p[0]=v.boolean?1u:0u;return;}
    if(t==SRNOS_PARAMETERS_TYPE_FLOAT)std::memcpy(&bits,&v.f32,4);
    else if(t==SRNOS_PARAMETERS_TYPE_INT32)std::memcpy(&bits,&v.i32,4);
    else bits=v.u32;
    put32(p,bits);
}
static SRNOS_ParametersStatus decode(SRNOS_ParametersType t,const uint8_t *p,size_t n,Value *v) {
    if(!p||!v||n!=size_of(t))return SRNOS_PARAMETERS_STATUS_TYPE_MISMATCH;
    *v={};
    if(t==SRNOS_PARAMETERS_TYPE_BOOL){if(*p>1)return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;v->boolean=*p!=0;}
    else {uint32_t bits=get32(p);if(t==SRNOS_PARAMETERS_TYPE_FLOAT){std::memcpy(&v->f32,&bits,4);if(!std::isfinite(v->f32))return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;}
        else if(t==SRNOS_PARAMETERS_TYPE_INT32)std::memcpy(&v->i32,&bits,4);else v->u32=bits;}
    return SRNOS_PARAMETERS_STATUS_OK;
}
static bool valid_value(const Entry& e,Value v) {
    switch(e.type){
    case SRNOS_PARAMETERS_TYPE_FLOAT:return std::isfinite(v.f32)&&std::isfinite(e.minimum.f32)&&std::isfinite(e.maximum.f32)&&v.f32>=e.minimum.f32&&v.f32<=e.maximum.f32;
    case SRNOS_PARAMETERS_TYPE_INT32:return v.i32>=e.minimum.i32&&v.i32<=e.maximum.i32;
    case SRNOS_PARAMETERS_TYPE_UINT32:return v.u32>=e.minimum.u32&&v.u32<=e.maximum.u32;
    case SRNOS_PARAMETERS_TYPE_BOOL:return v.boolean>=e.minimum.boolean&&v.boolean<=e.maximum.boolean;
    default:return false;}
}
static bool equal(const Entry& e,Value a,Value b) {
    /* Value guards compare fixed scalar bits only; wire encoding stays outside.
     * Bool padding is not a value. Float bit equality also preserves -0. */
    return e.type==SRNOS_PARAMETERS_TYPE_BOOL ? a.boolean==b.boolean : std::memcmp(&a,&b,4)==0;
}
static void changed() { if(++state.generation==0){++state.generation;state.saved_valid=false;}state.dirty=true; }
static uint32_t hash_bytes(uint32_t h,const void *p,size_t n){auto b=static_cast<const uint8_t*>(p);while(n--)h=(h^*b++)*16777619u;return h;}
static uint32_t hash_name(const char *s){return hash_bytes(2166136261u,s,std::strlen(s));}
static size_t find_name(const char *name){if(name)for(size_t i=0;i<count;++i)if(!std::strcmp(name,entries[i].name))return i;return count;}
static size_t find_id(uint32_t id){for(size_t i=0;i<count;++i)if(entries[i].id==id)return i;return count;}
static SRNOS_ParametersStatus storage_status(SRNOS_StorageStatus s){
    switch(s){case SRNOS_STORAGE_OK:return SRNOS_PARAMETERS_STATUS_OK;
    case SRNOS_STORAGE_BUSY:return SRNOS_PARAMETERS_STATUS_EEPROM_BUSY;
    case SRNOS_STORAGE_DISABLED:return SRNOS_PARAMETERS_STATUS_DISABLED;
    case SRNOS_STORAGE_CONTEXT_ERROR:return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    case SRNOS_STORAGE_NOT_FOUND:return SRNOS_PARAMETERS_STATUS_NOT_FOUND;
    case SRNOS_STORAGE_CORRUPT:return SRNOS_PARAMETERS_STATUS_CRC_ERROR;
    case SRNOS_STORAGE_INCOMPATIBLE:return SRNOS_PARAMETERS_STATUS_LAYOUT_MISMATCH;
    case SRNOS_STORAGE_TIMEOUT:return SRNOS_PARAMETERS_STATUS_TIMEOUT;
    case SRNOS_STORAGE_INVALID_ARGUMENT:return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    case SRNOS_STORAGE_READ_ONLY:return SRNOS_PARAMETERS_STATUS_READ_ONLY;
    default:return SRNOS_PARAMETERS_STATUS_EEPROM_ERROR;}
}
static SRNOS_ParametersStatus begin_operation(uint32_t timeout,uint32_t *generation){
    if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(timeout>INT32_MAX)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    ValueGuard guard;
    if(!frozen||!configured)return SRNOS_PARAMETERS_STATUS_NOT_INITIALIZED;
    if(!state.application_id && count)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    if(state.busy)return SRNOS_PARAMETERS_STATUS_EEPROM_BUSY;
    state.busy=true;*generation=state.generation;return SRNOS_PARAMETERS_STATUS_OK;
}
static SRNOS_ParametersStatus finish_load(SRNOS_ParametersStatus result){
    ValueGuard guard;state.last_load=result;state.busy=false;
    if(result!=SRNOS_PARAMETERS_STATUS_OK&&result!=SRNOS_PARAMETERS_STATUS_CONFLICT&&
       result!=SRNOS_PARAMETERS_STATUS_EEPROM_BUSY&&result!=SRNOS_PARAMETERS_STATUS_DISABLED){
        state.saved_valid=false;state.dirty=count!=0;
    }
    return result;
}
struct LoadState {
    Value decoded[SRNOS_PARAMETERS_MAX_COUNT];
    uint8_t fragment[item_bytes];
    size_t consumed,used;
    uint16_t expected,parsed,loaded,ignored;
    uint32_t previous_id;
    bool header_done,invalid;
};
static bool load_chunk(void *context,size_t offset,const uint8_t *data,size_t length){
    auto &s=*static_cast<LoadState*>(context);
    if(offset!=s.consumed){s.invalid=true;return false;}
    while(length){
        const size_t goal=s.header_done?item_bytes:header_bytes;
        const size_t take=length<goal-s.used?length:goal-s.used;
        std::memcpy(s.fragment+s.used,data,take);s.used+=take;s.consumed+=take;data+=take;length-=take;
        if(s.used!=goal)continue;
        const uint8_t *p=s.fragment;s.used=0;
        if(!s.header_done){
            s.expected=get16(p+6);
            if(get32(p)!=magic||get16(p+4)!=1||header_bytes+(size_t)s.expected*item_bytes>SRNOS_STORAGE_PARAMETERS_MAX_BYTES){s.invalid=true;return false;}
            s.header_done=true;continue;
        }
        const uint32_t id=get32(p);
        /* Canonical ascending IDs detect duplicates even for unknown entries,
         * without an image-sized bitmap or a quadratic search. */
        if(++s.parsed>s.expected||id<=s.previous_id||p[6]||p[7]||
           p[4]<SRNOS_PARAMETERS_TYPE_FLOAT||p[4]>SRNOS_PARAMETERS_TYPE_BOOL||
           p[5]!=size_of((SRNOS_ParametersType)p[4])||
           (p[4]==SRNOS_PARAMETERS_TYPE_BOOL&&(p[9]||p[10]||p[11]))){s.invalid=true;return false;}
        s.previous_id=id;
        const size_t i=find_id(id);if(i==count){++s.ignored;continue;}
        if(p[4]!=(uint8_t)entries[i].type){++s.ignored;continue;}
        Value v{};
        if(decode(entries[i].type,p+8,p[5],&v)!=SRNOS_PARAMETERS_STATUS_OK||!valid_value(entries[i],v)){++s.ignored;continue;}
        s.decoded[i]=v;++s.loaded;
    }
    return true;
}
static SRNOS_ParametersStatus load(uint32_t timeout,bool startup){
    uint32_t generation=0;auto result=begin_operation(timeout,&generation);if(result!=SRNOS_PARAMETERS_STATUS_OK)return result;
    if(!count)return finish_load(SRNOS_PARAMETERS_STATUS_OK);
    LoadState pending{};
    for(size_t i=0;i<count;++i)pending.decoded[i]=entries[i].initial;
    size_t length=0;uint32_t revision=0;
    result=storage_status(SRNOS_StorageVisitRecord(SRNOS_STORAGE_RECORD_PARAMETERS,state.application_id,state.compatibility_version,load_chunk,&pending,&length,&revision,timeout));
    if(pending.invalid)result=SRNOS_PARAMETERS_STATUS_BAD_DATA;
    if(result==SRNOS_PARAMETERS_STATUS_OK&&(!pending.header_done||pending.used||pending.parsed!=pending.expected||length!=header_bytes+(size_t)pending.expected*item_bytes))result=SRNOS_PARAMETERS_STATUS_BAD_DATA;
    {
        ValueGuard guard;
        if(state.generation!=generation)return finish_load(SRNOS_PARAMETERS_STATUS_CONFLICT);
        if(result==SRNOS_PARAMETERS_STATUS_OK||startup){
            bool modified=false;
            for(size_t i=0;i<count;++i){const Value next=result==SRNOS_PARAMETERS_STATUS_OK?pending.decoded[i]:entries[i].initial;
                modified|=!equal(entries[i],entries[i].value,next);entries[i].value=next;}
            if(modified)changed();
            state.loaded=result==SRNOS_PARAMETERS_STATUS_OK?pending.loaded:0;
            state.defaulted=(uint16_t)(count-state.loaded);
            state.ignored=result==SRNOS_PARAMETERS_STATUS_OK?pending.ignored:0;
            state.saved_valid=result==SRNOS_PARAMETERS_STATUS_OK;
            state.storage_revision=result==SRNOS_PARAMETERS_STATUS_OK?revision:0;
            state.dirty=!state.saved_valid||state.defaulted!=0;
            state.saved_generation=state.dirty?0:state.generation;
        }
    }
    return finish_load(result);
}
static SRNOS_ParametersStatus format(size_t i,SRNOS_ParametersBinaryValue kind,char *out,size_t cap){
    if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(!out||!cap)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;
    Value v{};{ValueGuard guard;const auto &e=entries[i];v=kind==SRNOS_PARAMETERS_BINARY_VALUE_CURRENT?e.value:kind==SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT?e.initial:kind==SRNOS_PARAMETERS_BINARY_VALUE_MIN?e.minimum:e.maximum;}
    int n=0;switch(entries[i].type){case SRNOS_PARAMETERS_TYPE_FLOAT:n=std::snprintf(out,cap,"%.9g",(double)v.f32);break;
    case SRNOS_PARAMETERS_TYPE_INT32:n=std::snprintf(out,cap,"%ld",(long)v.i32);break;
    case SRNOS_PARAMETERS_TYPE_UINT32:n=std::snprintf(out,cap,"%lu",(unsigned long)v.u32);break;
    default:n=std::snprintf(out,cap,"%s",v.boolean?"true":"false");break;}
    return n<0?SRNOS_PARAMETERS_STATUS_ERROR:(size_t)n>=cap?SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL:SRNOS_PARAMETERS_STATUS_OK;
}
}
extern "C" {
void SRNOS_ParametersConfigure(uint32_t app,uint16_t version){ValueGuard guard;if(!frozen){state.application_id=app;state.compatibility_version=version;configured=true;state.last_load=state.last_save=SRNOS_PARAMETERS_STATUS_NOT_INITIALIZED;}}
SRNOS_ParametersStatus SRNOS_ParametersRegisterPersistent(const SRNOS_PersistentParameterDefinition *d,uint16_t *index){
    if(index)*index=UINT16_MAX;
    if(frozen)return SRNOS_PARAMETERS_STATUS_LOCKED;
    if(!d||!index||!d->name||!d->stable_id)return registration_error=SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    size_t n=0;while(n<=SRNOS_PARAMETERS_MAX_NAME_LEN&&d->name[n])++n;
    if(!n||n>SRNOS_PARAMETERS_MAX_NAME_LEN)return registration_error=SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    if(find_id(d->stable_id)!=count||find_name(d->name)!=count)return registration_error=SRNOS_PARAMETERS_STATUS_DUPLICATE;
    if(count==SRNOS_PARAMETERS_MAX_COUNT)return registration_error=SRNOS_PARAMETERS_STATUS_NO_SPACE;
    Entry e{};e.id=d->stable_id;e.type=d->type;e.initial=e.value=d->default_value;e.minimum=d->minimum;e.maximum=d->maximum;
    if(!valid_value(e,e.initial))return registration_error=SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;
    std::memcpy(e.name,d->name,n+1);entries[count]=e;*index=(uint16_t)count++;state.dirty=true;
    return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersInit(void){return registration_error;}
SRNOS_ParametersStatus SRNOS_ParametersFreeze(void){
    if(frozen)return SRNOS_PARAMETERS_STATUS_OK;
    if(registration_error!=SRNOS_PARAMETERS_STATUS_OK)return registration_error;
    if(count&&(!configured||!state.application_id||!state.compatibility_version))return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    frozen=true;state.generation=1;state.dirty=count!=0;
    std::memset((void*)&SRNOS_DebugDbgParams,0,sizeof(SRNOS_DebugDbgParams));
    SRNOS_DebugDbgParams.registered=(uint32_t)count;SRNOS_DebugDbgParams.max_count=SRNOS_PARAMETERS_MAX_COUNT;
    SRNOS_DebugDbgParams.payload_size=(uint32_t)SRNOS_ParametersPayloadSize();SRNOS_DebugDbgParams.eeprom_capacity=SRNOS_STORAGE_PARAMETERS_MAX_BYTES;
    SRNOS_DebugDbgParams.layout_hash=SRNOS_ParametersLayoutHash();return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersGet(uint16_t i,Value *v){if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;if(!v)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;ValueGuard guard;*v=entries[i].value;return SRNOS_PARAMETERS_STATUS_OK;}
SRNOS_ParametersStatus SRNOS_ParametersSet(uint16_t i,const Value *v){if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;if(!v)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;if(!valid_value(entries[i],*v))return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;ValueGuard guard;if(!equal(entries[i],entries[i].value,*v)){entries[i].value=*v;changed();}return SRNOS_PARAMETERS_STATUS_OK;}
SRNOS_ParametersStatus SRNOS_ParametersGetState(SRNOS_ParametersState *out){if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;if(!out)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;ValueGuard guard;*out=state;return SRNOS_PARAMETERS_STATUS_OK;}
SRNOS_ParametersStatus SRNOS_ParametersResetDefaults(void){
    if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    ValueGuard guard;bool modified=false;
    for(size_t i=0;i<count;++i){modified|=!equal(entries[i],entries[i].value,entries[i].initial);entries[i].value=entries[i].initial;}
    if(modified)changed();
    return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersLoadStartup(uint32_t t){
    if(t||!count)return load(t,true);
    auto result=SRNOS_ParametersResetDefaults();
    if(result!=SRNOS_PARAMETERS_STATUS_OK)return result;
    ValueGuard guard;state.last_load=SRNOS_PARAMETERS_STATUS_TIMEOUT;state.loaded=0;state.defaulted=(uint16_t)count;state.saved_valid=false;state.dirty=true;
    return SRNOS_PARAMETERS_STATUS_TIMEOUT;
}
SRNOS_ParametersStatus SRNOS_ParametersLoad(void){return load(0,false);}
SRNOS_ParametersStatus SRNOS_ParametersLoadWithTimeout(uint32_t t){return load(t,false);}
SRNOS_ParametersStatus SRNOS_ParametersSave(void){return SRNOS_ParametersSaveWithTimeout(0);}
SRNOS_ParametersStatus SRNOS_ParametersSaveWithTimeout(uint32_t t){
    uint32_t generation=0;auto result=begin_operation(t,&generation);if(result!=SRNOS_PARAMETERS_STATUS_OK)return result;
    Value values[SRNOS_PARAMETERS_MAX_COUNT]{};
    {ValueGuard guard;
        if(!count||(state.saved_valid&&!state.dirty)){state.last_save=SRNOS_PARAMETERS_STATUS_OK;state.busy=false;return SRNOS_PARAMETERS_STATUS_OK;}
        for(size_t i=0;i<count;++i)values[i]=entries[i].value;
        generation=state.generation;
    }
    put32(image,magic);put16(image+4,1);put16(image+6,(uint16_t)count);
    uint16_t order[SRNOS_PARAMETERS_MAX_COUNT];
    for(size_t i=0;i<count;++i){size_t j=i;while(j&&entries[order[j-1]].id>entries[i].id){order[j]=order[j-1];--j;}order[j]=(uint16_t)i;}
    for(size_t n=0;n<count;++n){const size_t i=order[n];uint8_t *p=image+header_bytes+n*item_bytes;std::memset(p,0,item_bytes);put32(p,entries[i].id);p[4]=(uint8_t)entries[i].type;p[5]=(uint8_t)size_of(entries[i].type);encode(entries[i].type,values[i],p+8);}
    uint32_t revision=0;
    result=storage_status(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_PARAMETERS,state.application_id,state.compatibility_version,image,header_bytes+count*item_bytes,&revision,t));
    {ValueGuard guard;
        if(result==SRNOS_PARAMETERS_STATUS_OK){state.saved_valid=true;state.saved_generation=generation;state.storage_revision=revision;state.dirty=state.generation!=generation;}
        else if(result!=SRNOS_PARAMETERS_STATUS_EEPROM_BUSY&&result!=SRNOS_PARAMETERS_STATUS_DISABLED){state.saved_valid=false;state.dirty=true;}
        state.last_save=result;state.busy=false;
    }
    return result;
}
size_t SRNOS_ParametersCount(void){return count;}
size_t SRNOS_ParametersPayloadSize(void){size_t n=0;for(size_t i=0;i<count;++i)n+=size_of(entries[i].type);return n;}
uint32_t SRNOS_ParametersLayoutHash(void){uint32_t h=2166136261u;for(size_t i=0;i<count;++i){uint8_t b[4];put32(b,entries[i].id);h=hash_bytes(h,b,4);h=hash_bytes(h,entries[i].name,std::strlen(entries[i].name));b[0]=(uint8_t)entries[i].type;h=hash_bytes(h,b,1);for(const Value v:{entries[i].initial,entries[i].minimum,entries[i].maximum}){encode(entries[i].type,v,b);h=hash_bytes(h,b,size_of(entries[i].type));}}return h;}
SRNOS_ParametersStatus SRNOS_ParametersGetByIndex(size_t i,SRNOS_ParametersEntryInfo *out){if(!out)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;const auto&e=entries[i];*out={(uint32_t)i,e.name,hash_name(e.name),e.id,e.type,size_of(e.type),1};return SRNOS_PARAMETERS_STATUS_OK;}
SRNOS_ParametersStatus SRNOS_ParametersGetByName(const char *n,SRNOS_ParametersEntryInfo *out){return SRNOS_ParametersGetByIndex(find_name(n),out);}
SRNOS_ParametersStatus SRNOS_ParametersGetValueByIndex(size_t i,char *o,size_t n){return format(i,SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetDefaultValueByIndex(size_t i,char *o,size_t n){return format(i,SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT,o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetMinValueByIndex(size_t i,char *o,size_t n){return format(i,SRNOS_PARAMETERS_BINARY_VALUE_MIN,o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetMaxValueByIndex(size_t i,char *o,size_t n){return format(i,SRNOS_PARAMETERS_BINARY_VALUE_MAX,o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetValue(const char *s,char *o,size_t n){return SRNOS_ParametersGetValueByIndex(find_name(s),o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetDefaultValue(const char *s,char *o,size_t n){return SRNOS_ParametersGetDefaultValueByIndex(find_name(s),o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetMinValue(const char *s,char *o,size_t n){return SRNOS_ParametersGetMinValueByIndex(find_name(s),o,n);}
SRNOS_ParametersStatus SRNOS_ParametersGetMaxValue(const char *s,char *o,size_t n){return SRNOS_ParametersGetMaxValueByIndex(find_name(s),o,n);}
SRNOS_ParametersStatus SRNOS_ParametersSetValue(const char *s,const char *v){return SRNOS_ParametersSetValueByIndex(find_name(s),v);}
SRNOS_ParametersStatus SRNOS_ParametersSetValueByIndex(size_t i,const char *text){
    if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(!text||!*text)return SRNOS_PARAMETERS_STATUS_PARSE_ERROR;
    if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;
    Value v{};char *end=nullptr;errno=0;
    switch(entries[i].type){case SRNOS_PARAMETERS_TYPE_FLOAT:v.f32=std::strtof(text,&end);break;
    case SRNOS_PARAMETERS_TYPE_INT32:{long long n=std::strtoll(text,&end,10);if(n<INT32_MIN||n>INT32_MAX)return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;v.i32=(int32_t)n;break;}
    case SRNOS_PARAMETERS_TYPE_UINT32:{const char *p=text;while(*p==' '||*p=='\t')++p;if(*p=='-')return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;unsigned long long n=std::strtoull(text,&end,10);if(n>UINT32_MAX)return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;v.u32=(uint32_t)n;break;}
    default:if(!std::strcmp(text,"true")||!std::strcmp(text,"1"))v.boolean=true;else if(!std::strcmp(text,"false")||!std::strcmp(text,"0"))v.boolean=false;else return SRNOS_PARAMETERS_STATUS_PARSE_ERROR;return SRNOS_ParametersSet((uint16_t)i,&v);}
    if(end==text||!end)return SRNOS_PARAMETERS_STATUS_PARSE_ERROR;
    while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')++end;
    if(*end)return SRNOS_PARAMETERS_STATUS_PARSE_ERROR;
    if(errno==ERANGE)return SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;
    return SRNOS_ParametersSet((uint16_t)i,&v);
}
SRNOS_ParametersStatus SRNOS_ParametersCopyBinaryByIndex(size_t i,SRNOS_ParametersBinaryValue kind,uint8_t *out,size_t cap,size_t *written){
    if(written)*written=0;if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(!out||!written||kind>SRNOS_PARAMETERS_BINARY_VALUE_MAX)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;
    if(cap<size_of(entries[i].type))return SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL;
    Value v{};{ValueGuard guard;const auto&e=entries[i];v=kind==SRNOS_PARAMETERS_BINARY_VALUE_CURRENT?e.value:kind==SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT?e.initial:kind==SRNOS_PARAMETERS_BINARY_VALUE_MIN?e.minimum:e.maximum;}
    encode(entries[i].type,v,out);*written=size_of(entries[i].type);return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersValidateBinaryByIndex(size_t i,const uint8_t *p,size_t n){if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;Value v{};auto r=decode(entries[i].type,p,n,&v);return r!=SRNOS_PARAMETERS_STATUS_OK?r:valid_value(entries[i],v)?SRNOS_PARAMETERS_STATUS_OK:SRNOS_PARAMETERS_STATUS_OUT_OF_RANGE;}
SRNOS_ParametersStatus SRNOS_ParametersSetBinaryByIndex(size_t i,const uint8_t *p,size_t n){if(i>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;Value v{};auto r=decode(entries[i].type,p,n,&v);return r!=SRNOS_PARAMETERS_STATUS_OK?r:SRNOS_ParametersSet((uint16_t)i,&v);}
SRNOS_ParametersStatus SRNOS_ParametersSetBinaryBatch(const SRNOS_ParametersBinaryWrite *writes,size_t n,size_t *failed){
    if(failed)*failed=n;if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(!writes||!n||n>SRNOS_PARAMETERS_MAX_COUNT)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    Value values[SRNOS_PARAMETERS_MAX_COUNT]{};
    for(size_t k=0;k<n;++k){auto r=SRNOS_ParametersValidateBinaryByIndex(writes[k].index,writes[k].data,writes[k].len);for(size_t j=0;j<k;++j)if(writes[j].index==writes[k].index)r=SRNOS_PARAMETERS_STATUS_DUPLICATE;if(r!=SRNOS_PARAMETERS_STATUS_OK){if(failed)*failed=k;return r;}decode(entries[writes[k].index].type,writes[k].data,writes[k].len,&values[k]);}
    ValueGuard guard;bool modified=false;for(size_t k=0;k<n;++k){auto&e=entries[writes[k].index];modified|=!equal(e,e.value,values[k]);e.value=values[k];}if(modified)changed();return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersSnapshot(const uint16_t *indices,size_t n,uint8_t *out,size_t capacity,size_t *written){
    if(written)*written=0;if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;
    if(!indices||!out||!written||!n||n>SRNOS_PARAMETERS_MAX_COUNT)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;
    size_t bytes=0;for(size_t i=0;i<n;++i){if(indices[i]>=count)return SRNOS_PARAMETERS_STATUS_NOT_FOUND;bytes+=size_of(entries[indices[i]].type);}if(bytes>capacity)return SRNOS_PARAMETERS_STATUS_BUFFER_TOO_SMALL;
    Value values[SRNOS_PARAMETERS_MAX_COUNT]{};{ValueGuard guard;for(size_t i=0;i<n;++i)values[i]=entries[indices[i]].value;}
    size_t offset=0;for(size_t i=0;i<n;++i){encode(entries[indices[i]].type,values[i],out+offset);offset+=size_of(entries[indices[i]].type);}*written=offset;return SRNOS_PARAMETERS_STATUS_OK;
}
SRNOS_ParametersStatus SRNOS_ParametersDumpRegistry(SRNOS_ParametersDumpCallback cb,void *user){if(!context())return SRNOS_PARAMETERS_STATUS_CONTEXT_ERROR;if(!cb)return SRNOS_PARAMETERS_STATUS_INVALID_PARAM;for(size_t i=0;i<count;++i){SRNOS_ParametersEntryInfo info{};SRNOS_ParametersGetByIndex(i,&info);char a[32],b[32],c[32],d[32];format(i,SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,a,sizeof(a));format(i,SRNOS_PARAMETERS_BINARY_VALUE_DEFAULT,b,sizeof(b));format(i,SRNOS_PARAMETERS_BINARY_VALUE_MIN,c,sizeof(c));format(i,SRNOS_PARAMETERS_BINARY_VALUE_MAX,d,sizeof(d));cb(&info,a,b,c,d,user);}return SRNOS_PARAMETERS_STATUS_OK;}
static void dump_line(const SRNOS_ParametersEntryInfo *i,const char *v,const char *,const char *,const char *,void *){char b[128];int n=std::snprintf(b,sizeof(b),"%s=%s\n",i->name,v);if(n>0&&(size_t)n<sizeof(b))(void)SRNOS_DebugPortWriteExact(b,(size_t)n);}
SRNOS_ParametersStatus SRNOS_ParametersDumpDebugPort(void){return SRNOS_ParametersDumpRegistry(dump_line,nullptr);}
const char *SRNOS_ParametersStatusName(SRNOS_ParametersStatus s){static const char *const names[]={"ok","error","invalid_param","not_found","duplicate","no_space","type_mismatch","out_of_range","buffer_too_small","parse_error","eeprom_error","eeprom_busy","bad_data","layout_mismatch","crc_error","not_initialized","locked","read_only","conflict","timeout","disabled","context_error"};return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"unknown";}
const char *SRNOS_ParametersTypeName(SRNOS_ParametersType t){static const char *const n[]={"invalid","float","int32","uint32","bool"};return (unsigned)t<5?n[t]:"invalid";}
}
