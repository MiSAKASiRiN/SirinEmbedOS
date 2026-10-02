#include "storage_internal.h"
#include "Devices/EEPROM/eeprom_internal.h"
#include "SirinEmbedOS/Core/C/CRC32.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
#include <string.h>
#include <limits.h>

/* Fixed, explicitly serialized records. Only the caller advances EEPROM; no
 * service task, dynamic memory or critical section around copy/CRC/I/O. */
#define SRNOS_STORAGE_HEADER_BYTES 64u
#define SRNOS_STORAGE_FORMAT 1u
static const uint8_t record_magic[8] = {'S','R','N','O','S','N','V','1'};
static const uint8_t commit_magic[8] = {'S','R','N','O','S','O','K','1'};
static struct {
    bool prepared;
    SRNOS_StorageConfig config;
    SRNOS_StorageSnapshot snapshot;
    SRNOS_StorageIdentity identity;
    uint8_t header[64], commit[64], scratch[64];
    uint32_t started, timeout;
} storage;
typedef struct { uint16_t base, size; } SRNOS_StoragePartition;
typedef struct { uint32_t length, revision, crc; } SRNOS_StorageRecordMeta;

static bool context_ok(void)
{
    return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI() &&
        osKernelGetState() == osKernelRunning && osThreadGetId() != NULL;
}
static uint16_t u16(const uint8_t *p, size_t at)
{ uint16_t v = 0; (void)SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(p,64),at,&v); return v; }
static uint32_t u32(const uint8_t *p, size_t at)
{ uint32_t v = 0; (void)SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(p,64),at,&v); return v; }
static void put16(uint8_t *p,size_t at,uint16_t v)
{ (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(p,64),at,v); }
static void put32(uint8_t *p,size_t at,uint32_t v)
{ (void)SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(p,64),at,v); }
static bool partition(SRNOS_StorageRecord record, SRNOS_StoragePartition *p)
{
    switch (record) {
    case SRNOS_STORAGE_RECORD_IDENTITY: p->base=0; p->size=1024; return true;
    case SRNOS_STORAGE_RECORD_BMI: p->base=0x400; p->size=1024; return true;
    case SRNOS_STORAGE_RECORD_LSM: p->base=0x800; p->size=1024; return true;
    case SRNOS_STORAGE_RECORD_PARAMETERS: p->base=0x1000; p->size=8192; return true;
    default: return false;
    }
}
static SRNOS_StorageStatus eeprom_status(SRNOS_EEPROMStatus status)
{
    switch(status) {
    case SRNOS_EEPROM_OK: return SRNOS_STORAGE_OK;
    case SRNOS_EEPROM_BUSY: return SRNOS_STORAGE_BUSY;
    case SRNOS_EEPROM_TIMEOUT: return SRNOS_STORAGE_TIMEOUT;
    case SRNOS_EEPROM_VERIFY_ERROR: return SRNOS_STORAGE_VERIFY_ERROR;
    case SRNOS_EEPROM_CONTEXT_ERROR: return SRNOS_STORAGE_CONTEXT_ERROR;
    case SRNOS_EEPROM_READ_ONLY: return SRNOS_STORAGE_READ_ONLY;
    default: return SRNOS_STORAGE_IO_ERROR;
    }
}
static uint32_t remaining(void)
{
    uint32_t elapsed = HAL_GetTick() - storage.started;
    return elapsed >= storage.timeout ? 0 : storage.timeout - elapsed;
}
static SRNOS_StorageStatus transfer(bool write,uint16_t address,void *data,size_t size)
{
    uint32_t left = remaining();
    if (!left) return SRNOS_STORAGE_TIMEOUT;
    SRNOS_EEPROMStatus status = write ?
        SRNOS_EEPROMStorageWriteAsync(address,data,size,left) :
        SRNOS_EEPROMStorageReadAsync(address,data,size,left);
    if (status != SRNOS_EEPROM_OK) return eeprom_status(status);
    for (;;) {
        if (!remaining()) {
            status = SRNOS_EEPROMCancel();
            return status == SRNOS_EEPROM_IO_ERROR ? SRNOS_STORAGE_IO_ERROR : SRNOS_STORAGE_TIMEOUT;
        }
        status = SRNOS_EEPROMPoll();
        if (status != SRNOS_EEPROM_IN_PROGRESS) return eeprom_status(status);
        osDelay(1u);
    }
}
static SRNOS_StorageStatus acquire(uint32_t timeout,bool write)
{
    if (!context_ok()) return SRNOS_STORAGE_CONTEXT_ERROR;
    if (!storage.prepared || !storage.config.enabled) return SRNOS_STORAGE_DISABLED;
    if (timeout > INT32_MAX) return SRNOS_STORAGE_INVALID_ARGUMENT;
    SRNOS_StorageStatus status = eeprom_status(SRNOS_EEPROMStorageAcquire());
    uint32_t irq = __get_PRIMASK(); __disable_irq();
    if (status == SRNOS_STORAGE_BUSY) ++storage.snapshot.busy_count;
    if (status == SRNOS_STORAGE_OK) {
        storage.snapshot.busy = true;
        if (write) ++storage.snapshot.write_count; else ++storage.snapshot.read_count;
    }
    __set_PRIMASK(irq);
    if (status != SRNOS_STORAGE_OK) return status;
    storage.started = HAL_GetTick();
    storage.timeout = timeout ? timeout : (write ? SRNOS_STORAGE_WRITE_TIMEOUT_MS : SRNOS_STORAGE_READ_TIMEOUT_MS);
    return SRNOS_STORAGE_OK;
}
static SRNOS_StorageStatus release(SRNOS_StorageStatus status)
{
    uint32_t irq = __get_PRIMASK(); __disable_irq();
    storage.snapshot.last_status = status;
    if (status != SRNOS_STORAGE_OK) ++storage.snapshot.error_count;
    storage.snapshot.busy = false;
    __set_PRIMASK(irq);
    SRNOS_EEPROMStorageRelease();
    return status;
}
static bool all_value(const uint8_t *p,size_t n,uint8_t value)
{ for(size_t i=0;i<n;++i) if(p[i]!=value) return false; return true; }
static SRNOS_StorageStatus read_meta(SRNOS_StorageRecord record,SRNOS_StoragePartition p,
    uint32_t owner,uint16_t schema,SRNOS_StorageRecordMeta *meta)
{
    SRNOS_StorageStatus st = transfer(false,p.base,storage.header,64);
    if (st != SRNOS_STORAGE_OK) return st;
    if (all_value(storage.header,64,0xff) || all_value(storage.header,64,0)) return SRNOS_STORAGE_NOT_FOUND;
    if (memcmp(storage.header,record_magic,8)) return SRNOS_STORAGE_CORRUPT;
    if (u16(storage.header,8)!=SRNOS_STORAGE_FORMAT || u16(storage.header,10)!=(uint16_t)record ||
        u32(storage.header,20)!=owner || u16(storage.header,24)!=schema) return SRNOS_STORAGE_INCOMPATIBLE;
    meta->length = u32(storage.header,12); meta->revision=u32(storage.header,16); meta->crc=u32(storage.header,60);
    if (u16(storage.header,26)!=64 || !all_value(storage.header+28,32,0) ||
        !meta->length || meta->length > (uint32_t)p.size-128u) return SRNOS_STORAGE_CORRUPT;
    st = transfer(false,(uint16_t)(p.base+p.size-64u),storage.commit,64);
    if(st != SRNOS_STORAGE_OK) return st;
    if(memcmp(storage.commit,commit_magic,8) || u16(storage.commit,8)!=(uint16_t)record ||
        u16(storage.commit,10)!=SRNOS_STORAGE_FORMAT || u32(storage.commit,12)!=meta->length ||
        u32(storage.commit,16)!=meta->revision || u32(storage.commit,20)!=meta->crc ||
        !all_value(storage.commit+24,36,0) || u32(storage.commit,60)!=SRNOS_CRC32(storage.commit,60)) return SRNOS_STORAGE_CORRUPT;
    return SRNOS_STORAGE_OK;
}
/* Reading in 64 B blocks preserves bounded scratch and driver's 32 B bus
 * slices. Optional compare detects unchanged content without a full image. */
static SRNOS_StorageStatus read_data(SRNOS_StoragePartition p,SRNOS_StorageRecordMeta meta,
    void *out,const void *compare,bool *equal,SRNOS_StorageVisitor visitor,void *context)
{
    uint32_t crc=SRNOS_CRC32Update(SRNOS_CRC32_INIT,storage.header,60);
    if(equal) *equal=true;
    for(size_t off=0;off<meta.length;) {
        size_t n=meta.length-off; if(n>64) n=64;
        SRNOS_StorageStatus st=transfer(false,(uint16_t)(p.base+64u+off),storage.scratch,n);
        if(st!=SRNOS_STORAGE_OK) return st;
        crc=SRNOS_CRC32Update(crc,storage.scratch,n);
        if(out) memcpy((uint8_t*)out+off,storage.scratch,n);
        if(visitor && !visitor(context,off,storage.scratch,n)) return SRNOS_STORAGE_CORRUPT;
        if(compare && memcmp((const uint8_t*)compare+off,storage.scratch,n) && equal) *equal=false;
        off+=n;
    }
    if(!remaining()) return SRNOS_STORAGE_TIMEOUT;
    return (crc ^ SRNOS_CRC32_FINAL_XOR)==meta.crc ? SRNOS_STORAGE_OK : SRNOS_STORAGE_CORRUPT;
}
static bool valid_serial(const void *data,size_t length)
{
    if(!data || length!=SRNOS_STORAGE_SERIAL_LENGTH) return false;
    const char *p=(const char*)data;
    for(size_t i=0;i<length;++i) if(p[i]<'0'||p[i]>'9') return false;
    return true;
}
static void cache_identity(const void *data,uint32_t revision,SRNOS_StorageStatus status)
{
    uint32_t irq=__get_PRIMASK(); __disable_irq();
    storage.identity.status=status; storage.identity.valid=status==SRNOS_STORAGE_OK;
    storage.identity.revision=status==SRNOS_STORAGE_OK?revision:0;
    if(status==SRNOS_STORAGE_OK) memcpy(storage.identity.serial,data,20);
    else memset(storage.identity.serial,0,20);
    storage.identity.serial[20]=0;
    __set_PRIMASK(irq);
}
void SRNOS_StorageDefaultConfig(SRNOS_StorageConfig *config)
{ if(config) { config->enabled=true; config->factory_identity_write=false; } }
bool SRNOS_StoragePrepare(const SRNOS_StorageConfig *config)
{
    int state=(int)osKernelGetState();
    if(storage.prepared || __get_IPSR() || state<0 || state>=(int)osKernelRunning) return false;
    SRNOS_StorageConfig selected; SRNOS_StorageDefaultConfig(&selected);
    if(config) selected=*config;
    if(!SRNOS_EEPROMStorageConfigure(selected.enabled)) return false;
    storage.config=selected; storage.prepared=true;
    storage.identity.status=selected.enabled?SRNOS_STORAGE_NOT_FOUND:SRNOS_STORAGE_DISABLED;
    storage.snapshot.enabled=selected.enabled;
    storage.snapshot.identity_write_enabled=selected.factory_identity_write;
    return true;
}
SRNOS_StorageStatus SRNOS_StorageReadRecord(SRNOS_StorageRecord record,uint32_t owner,
    uint16_t schema,void *out,size_t capacity,size_t *length,uint32_t *revision,uint32_t timeout)
{
    if(length) *length=0;
    if(revision) *revision=0;
    SRNOS_StoragePartition p;
    if(!partition(record,&p)||!out||!capacity||!length||!revision||!schema) return SRNOS_STORAGE_INVALID_ARGUMENT;
    SRNOS_StorageStatus st=acquire(timeout,false); if(st!=SRNOS_STORAGE_OK) return st;
    SRNOS_StorageRecordMeta meta={0};
    st=read_meta(record,p,owner,schema,&meta);
    if(st==SRNOS_STORAGE_OK && meta.length>capacity) st=SRNOS_STORAGE_INVALID_ARGUMENT;
    if(st==SRNOS_STORAGE_OK) st=read_data(p,meta,out,NULL,NULL,NULL,NULL);
    if(st==SRNOS_STORAGE_OK && record==SRNOS_STORAGE_RECORD_IDENTITY && !valid_serial(out,meta.length)) st=SRNOS_STORAGE_CORRUPT;
    if(st==SRNOS_STORAGE_OK) { *length=meta.length; *revision=meta.revision; }
    if(record==SRNOS_STORAGE_RECORD_IDENTITY) cache_identity(out,meta.revision,st);
    return release(st);
}
SRNOS_StorageStatus SRNOS_StorageVisitRecord(SRNOS_StorageRecord record,uint32_t owner,
    uint16_t schema,SRNOS_StorageVisitor visitor,void *context,size_t *length,
    uint32_t *revision,uint32_t timeout)
{
    if(length) *length=0;
    if(revision) *revision=0;
    SRNOS_StoragePartition p;
    if(!partition(record,&p)||!visitor||!length||!revision||!schema) return SRNOS_STORAGE_INVALID_ARGUMENT;
    SRNOS_StorageStatus st=acquire(timeout,false); if(st!=SRNOS_STORAGE_OK) return st;
    SRNOS_StorageRecordMeta meta={0};
    st=read_meta(record,p,owner,schema,&meta);
    if(st==SRNOS_STORAGE_OK) st=read_data(p,meta,NULL,NULL,NULL,visitor,context);
    if(st==SRNOS_STORAGE_OK) { *length=meta.length; *revision=meta.revision; }
    return release(st);
}
SRNOS_StorageStatus SRNOS_StorageWriteRecord(SRNOS_StorageRecord record,uint32_t owner,
    uint16_t schema,const void *data,size_t length,uint32_t *revision,uint32_t timeout)
{
    if(revision) *revision=0;
    SRNOS_StoragePartition p;
    if(!partition(record,&p)||!data||!length||length>(size_t)p.size-128u||!revision||!schema) return SRNOS_STORAGE_INVALID_ARGUMENT;
    if(record==SRNOS_STORAGE_RECORD_IDENTITY && (!valid_serial(data,length)||owner!=0||schema!=1)) return SRNOS_STORAGE_INVALID_ARGUMENT;
    if(!context_ok()) return SRNOS_STORAGE_CONTEXT_ERROR;
    if(!storage.prepared || !storage.config.enabled) return SRNOS_STORAGE_DISABLED;
    if(record==SRNOS_STORAGE_RECORD_IDENTITY && !storage.config.factory_identity_write) return SRNOS_STORAGE_READ_ONLY;
    SRNOS_StorageStatus st=acquire(timeout,true); if(st!=SRNOS_STORAGE_OK) return st;
    SRNOS_StorageRecordMeta old={0}; bool equal=false;
    st=read_meta(record,p,owner,schema,&old);
    if(st==SRNOS_STORAGE_OK) st=read_data(p,old,NULL,old.length==length?data:NULL,&equal,NULL,NULL);
    if(st==SRNOS_STORAGE_OK && old.length==length && equal) {
        *revision=old.revision;
        uint32_t irq=__get_PRIMASK(); __disable_irq(); ++storage.snapshot.skipped_writes; __set_PRIMASK(irq);
        if(record==SRNOS_STORAGE_RECORD_IDENTITY) cache_identity(data,*revision,SRNOS_STORAGE_OK);
        return release(SRNOS_STORAGE_OK);
    }
    if(st!=SRNOS_STORAGE_OK && st!=SRNOS_STORAGE_NOT_FOUND && st!=SRNOS_STORAGE_CORRUPT && st!=SRNOS_STORAGE_INCOMPATIBLE) return release(st);
    uint32_t next=st==SRNOS_STORAGE_OK?old.revision+1u:1u;
    memset(storage.commit,0,64);
    st=transfer(true,(uint16_t)(p.base+p.size-64u),storage.commit,64); /* write driver also verifies invalidation */
    if(st!=SRNOS_STORAGE_OK) goto done;
    memset(storage.header,0,64); memcpy(storage.header,record_magic,8);
    put16(storage.header,8,SRNOS_STORAGE_FORMAT); put16(storage.header,10,(uint16_t)record);
    put32(storage.header,12,(uint32_t)length); put32(storage.header,16,next);
    put32(storage.header,20,owner); put16(storage.header,24,schema); put16(storage.header,26,64);
    uint32_t crc=SRNOS_CRC32Update(SRNOS_CRC32_INIT,storage.header,60);
    crc=SRNOS_CRC32Update(crc,data,length)^SRNOS_CRC32_FINAL_XOR;
    put32(storage.header,60,crc);
    st=transfer(true,p.base,storage.header,64);
    if(st==SRNOS_STORAGE_OK) st=transfer(true,(uint16_t)(p.base+64u),(void*)data,length);
    if(st!=SRNOS_STORAGE_OK) goto done;
    /* Re-read complete header and body before committing; transfer write
     * verification alone is insufficient for a whole-record CRC. */
    st=transfer(false,p.base,storage.scratch,64);
    if(st==SRNOS_STORAGE_OK && memcmp(storage.header,storage.scratch,64)) st=SRNOS_STORAGE_VERIFY_ERROR;
    if(st!=SRNOS_STORAGE_OK) goto done;
    SRNOS_StorageRecordMeta verify={(uint32_t)length,next,crc};
    st=read_data(p,verify,NULL,data,&equal,NULL,NULL);
    if(st==SRNOS_STORAGE_CORRUPT || (st==SRNOS_STORAGE_OK&&!equal)) st=SRNOS_STORAGE_VERIFY_ERROR;
    if(st!=SRNOS_STORAGE_OK) goto done;
    memset(storage.commit,0,64); memcpy(storage.commit,commit_magic,8);
    put16(storage.commit,8,(uint16_t)record); put16(storage.commit,10,SRNOS_STORAGE_FORMAT);
    put32(storage.commit,12,(uint32_t)length); put32(storage.commit,16,next); put32(storage.commit,20,crc);
    put32(storage.commit,60,SRNOS_CRC32(storage.commit,60));
    st=transfer(true,(uint16_t)(p.base+p.size-64u),storage.commit,64);
    if(st!=SRNOS_STORAGE_OK) goto done;
    st=read_meta(record,p,owner,schema,&verify);
    if(st==SRNOS_STORAGE_OK) st=read_data(p,verify,NULL,data,&equal,NULL,NULL);
    if(st==SRNOS_STORAGE_CORRUPT || (st==SRNOS_STORAGE_OK&&!equal)) st=SRNOS_STORAGE_VERIFY_ERROR;
    if(st==SRNOS_STORAGE_OK) *revision=next;
 done:
    if(record==SRNOS_STORAGE_RECORD_IDENTITY) cache_identity(data,*revision,st);
    return release(st);
}
SRNOS_StorageStatus SRNOS_StorageLoadIdentity(uint32_t timeout)
{
    char serial[20]; size_t length; uint32_t revision;
    return SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_IDENTITY,0,1,serial,sizeof(serial),&length,&revision,timeout);
}
SRNOS_StorageStatus SRNOS_StorageWriteIdentity(const char serial[20],uint32_t timeout)
{
    char copy[20]; uint32_t revision;
    if(!serial) return SRNOS_STORAGE_INVALID_ARGUMENT;
    memcpy(copy,serial,20);
    return SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_IDENTITY,0,1,copy,sizeof(copy),&revision,timeout);
}
bool SRNOS_StorageGetIdentity(SRNOS_StorageIdentity *out)
{
    if(!out||!context_ok()) return false;
    uint32_t irq=__get_PRIMASK(); __disable_irq(); *out=storage.identity; __set_PRIMASK(irq); return true;
}
bool SRNOS_StorageGetSnapshot(SRNOS_StorageSnapshot *out)
{
    if(!out||!context_ok()) return false;
    uint32_t irq=__get_PRIMASK(); __disable_irq(); *out=storage.snapshot; __set_PRIMASK(irq); return true;
}
