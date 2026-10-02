#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
static void model_write(uint8_t *dst,const uint8_t *src,size_t length);
static void model_delay(void);
#define SRNOS_EEPROM_TEST_WRITE model_write
#define SRNOS_EEPROM_TEST_DELAY model_delay
#define main driver_baseline
#include "../../Devices/EEPROM/test_eeprom.c"
#undef main
#include "../../../Source/Services/Storage/storage.c"
static jmp_buf power_loss;
static int cut_after=-1, programmed;
static bool check_competitor;
static uint8_t saved_chip[16384], value[97], other[97], loaded[8064];
typedef struct { size_t count, abort_after; uint32_t crc; } VisitorState;
static bool visit(void *context,size_t offset,const uint8_t *data,size_t length)
{
    VisitorState *state=context;
    assert(!irq && !basepri && length<=64 && length && offset==state->count);
    state->count+=length;
    state->crc=SRNOS_CRC32Update(state->crc,data,length);
    return !state->abort_after || state->count<state->abort_after;
}
static void model_write(uint8_t *dst,const uint8_t *src,size_t length)
{
    for(size_t i=0;i<length;++i) {
        if(cut_after>=0 && programmed++==cut_after) longjmp(power_loss,1);
        dst[i]=src[i];
    }
}
static void model_delay(void)
{
    if(!check_competitor) return;
    check_competitor=false;
    uintptr_t prior=owner; owner=2;
    size_t n;uint32_t rev;
    assert(SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_BMI,123,1,loaded,sizeof(loaded),&n,&rev,0)==SRNOS_STORAGE_BUSY);
    assert(SRNOS_EEPROMReadAsync(0,loaded,1,10)==SRNOS_EEPROM_BUSY);
    assert(SRNOS_EEPROMCancel()==SRNOS_EEPROM_BUSY);
    owner=prior;
    /* Even lease owner cannot sneak raw I/O between record phases. */
    assert(SRNOS_EEPROMReadAsync(0,loaded,1,10)==SRNOS_EEPROM_BUSY);
}
static void boot(bool clear,bool factory,bool enabled)
{
    if(clear) reset();
    else { memset(&s,0,sizeof(s)); memset(&active,0,sizeof(active)); irq=basepri=ipsr=0; owner=1; stalled=nack=quarantine=locked=false; }
    memset(&storage,0,sizeof(storage));
    kernel=0; SRNOS_StorageConfig config={enabled,factory};
    assert(SRNOS_StoragePrepare(&config)); assert(!SRNOS_StoragePrepare(&config));
    kernel=osKernelRunning;
}
static SRNOS_StorageStatus load(uint32_t *revision)
{
    size_t n=0;
    SRNOS_StorageStatus st=SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_BMI,123,1,loaded,sizeof(loaded),&n,revision,0);
    if(st==SRNOS_STORAGE_OK) assert(n==sizeof(value)); else assert(n==0 && *revision==0);
    return st;
}
int main(void)
{
    cut_after=-1; driver_baseline();
    assert(SRNOS_CRC32("123456789",9)==0xCBF43926u);
    uint32_t crc=SRNOS_CRC32Update(SRNOS_CRC32_INIT,"1234",4);
    assert((SRNOS_CRC32Update(crc,"56789",5)^SRNOS_CRC32_FINAL_XOR)==0xCBF43926u);
    assert(SRNOS_CRC32(NULL,1)==0 && SRNOS_CRC32(NULL,0)==0);
    for(size_t i=0;i<sizeof(value);++i) { value[i]=(uint8_t)(i*17+3); other[i]=(uint8_t)(value[i]^0x7d); }
    boot(true,true,true); memset(chip,0xff,sizeof(chip));
    uint32_t revision=0;
    assert(load(&revision)==SRNOS_STORAGE_NOT_FOUND);
    assert(SRNOS_EEPROMWrite(0x2fff,value,2,10)==SRNOS_EEPROM_READ_ONLY);
    assert(SRNOS_EEPROMWrite(0x3000,value,2,10)==SRNOS_EEPROM_OK);
    check_competitor=true;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,value,sizeof(value),&revision,0)==SRNOS_STORAGE_OK && revision==1);
    assert(!check_competitor && load(&revision)==SRNOS_STORAGE_OK && !memcmp(value,loaded,sizeof(value)));
    unsigned old_writes=writes;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,value,sizeof(value),&revision,0)==SRNOS_STORAGE_OK && revision==1 && writes==old_writes);
    memcpy(saved_chip,chip,sizeof(chip));
    /* Every byte cut through invalidation, header, multi-page data, and commit.
     * Simulated reset discards all RAM/driver state and parses actual EEPROM. */
    for(int cut=0;cut<=289;++cut) {
        memcpy(chip,saved_chip,sizeof(chip)); boot(false,true,true);
        programmed=0; cut_after=cut;
        if(setjmp(power_loss)==0) {
            assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,other,sizeof(other),&revision,0)==SRNOS_STORAGE_OK);
        }
        cut_after=-1; boot(false,true,true);
        SRNOS_StorageStatus st=load(&revision);
        assert(st==SRNOS_STORAGE_OK || st==SRNOS_STORAGE_CORRUPT);
        if(st==SRNOS_STORAGE_OK) assert(!memcmp(loaded,value,sizeof(value)) || !memcmp(loaded,other,sizeof(other)));
        assert(!memcmp(chip,saved_chip,0x400));
        assert(!memcmp(chip+0x800,saved_chip+0x800,sizeof(chip)-0x800));
    }
    /* Header, payload and commit corruption are all rejected. */
    for(unsigned region=0;region<3;++region) {
        unsigned start=region==0?0x400:region==1?0x440:0x7c0;
        unsigned len=region==1?sizeof(value):64;
        for(unsigned at=0;at<len;++at) {
            memcpy(chip,saved_chip,sizeof(chip));chip[start+at]^=0x80;boot(false,true,true);
            assert(load(&revision)!=SRNOS_STORAGE_OK);
        }
    }
    memcpy(chip,saved_chip,sizeof(chip));boot(false,true,true);
    size_t length;
    assert(SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_BMI,999,1,loaded,sizeof(loaded),&length,&revision,0)==SRNOS_STORAGE_INCOMPATIBLE);
    assert(SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_BMI,123,2,loaded,sizeof(loaded),&length,&revision,0)==SRNOS_STORAGE_INCOMPATIBLE);
    assert(SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_BMI,123,1,loaded,1,&length,&revision,0)==SRNOS_STORAGE_INVALID_ARGUMENT);
    tick=UINT32_MAX-10;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,other,sizeof(other),&revision,0)==SRNOS_STORAGE_OK && revision==2);
    assert(load(&revision)==SRNOS_STORAGE_OK);
    assert(SRNOS_StorageWriteIdentity("00123456789012345678",0)==SRNOS_STORAGE_OK);
    SRNOS_StorageIdentity identity; assert(SRNOS_StorageGetIdentity(&identity)&&identity.valid&&!strcmp(identity.serial,"00123456789012345678"));
    assert(SRNOS_StorageWriteIdentity("a0123456789012345678",0)==SRNOS_STORAGE_INVALID_ARGUMENT);
    boot(false,false,true);
    assert(SRNOS_StorageLoadIdentity(0)==SRNOS_STORAGE_OK);
    assert(SRNOS_StorageGetIdentity(&identity)&&identity.valid&&!strcmp(identity.serial,"00123456789012345678"));
    assert(SRNOS_StorageWriteIdentity("00123456789012345678",0)==SRNOS_STORAGE_READ_ONLY);
    /* Whole operation timeout, not renewed per page, and terminal cancel. */
    boot(false,true,true); uint32_t start_tick=tick;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,value,sizeof(value),&revision,15)==SRNOS_STORAGE_TIMEOUT);
    assert(tick-start_tick<=15 && !SRNOS_EEPROMIsBusy());
    boot(false,true,true); stalled=true; start_tick=tick;
    assert(load(&revision)==SRNOS_STORAGE_TIMEOUT && tick-start_tick<=2000 && cancels>0);
    boot(false,true,true);nack=true;assert(load(&revision)==SRNOS_STORAGE_IO_ERROR);
    boot(false,true,true);protected_write=true;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,value,sizeof(value),&revision,0)==SRNOS_STORAGE_VERIFY_ERROR);
    protected_write=false;
    boot(false,true,true);ipsr=1;assert(load(&revision)==SRNOS_STORAGE_CONTEXT_ERROR);ipsr=0;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_BMI,123,1,value,sizeof(value),&revision,UINT32_MAX)==SRNOS_STORAGE_INVALID_ARGUMENT);
    boot(false,true,true);
    for(size_t i=0;i<sizeof(loaded);++i) loaded[i]=(uint8_t)(i*17u);
    start_tick=tick;
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,loaded,sizeof(loaded),&revision,0)==SRNOS_STORAGE_OK);
    assert(tick-start_tick<SRNOS_STORAGE_WRITE_TIMEOUT_MS);
    memset(loaded,0,sizeof(loaded));
    assert(SRNOS_StorageReadRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,loaded,sizeof(loaded),&length,&revision,0)==SRNOS_STORAGE_OK && length==sizeof(loaded));
    for(size_t i=0;i<sizeof(loaded);++i) assert(loaded[i]==(uint8_t)(i*17u));
    assert(SRNOS_StorageWriteRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,loaded,sizeof(loaded)+1,&revision,0)==SRNOS_STORAGE_INVALID_ARGUMENT);
    VisitorState visitor={0,0,SRNOS_CRC32_INIT};
    assert(SRNOS_StorageVisitRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,visit,&visitor,&length,&revision,0)==SRNOS_STORAGE_OK);
    assert(length==8064 && visitor.count==8064 && (visitor.crc^SRNOS_CRC32_FINAL_XOR)==SRNOS_CRC32(loaded,sizeof(loaded)));
    chip[0x2fbf]^=1; /* Last body byte: visitor saw every block before CRC fails. */
    visitor=(VisitorState){0,0,SRNOS_CRC32_INIT};
    assert(SRNOS_StorageVisitRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,visit,&visitor,&length,&revision,0)==SRNOS_STORAGE_CORRUPT);
    assert(visitor.count==8064 && length==0 && revision==0 && !SRNOS_EEPROMIsBusy());
    chip[0x2fbf]^=1;
    visitor=(VisitorState){0,128,SRNOS_CRC32_INIT};
    assert(SRNOS_StorageVisitRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,visit,&visitor,&length,&revision,0)==SRNOS_STORAGE_CORRUPT);
    assert(visitor.count==128 && length==0 && revision==0 && !SRNOS_EEPROMIsBusy());
    visitor=(VisitorState){0,0,SRNOS_CRC32_INIT};
    assert(SRNOS_StorageVisitRecord(SRNOS_STORAGE_RECORD_PARAMETERS,123,1,visit,&visitor,&length,&revision,0)==SRNOS_STORAGE_OK);
    boot(false,false,false); assert(load(&revision)==SRNOS_STORAGE_DISABLED);
    assert(SRNOS_EEPROMWrite(0,value,sizeof(value),20)==SRNOS_EEPROM_OK);
    puts("Storage: actual FT24C128A state machine, CRC vector, every-byte power cuts, corrupt records, lease/ownership, raw protection, serial, unchanged writes, deadline/cancel and wrap passed");
}
