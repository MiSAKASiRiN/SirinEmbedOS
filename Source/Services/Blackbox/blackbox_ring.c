#include "blackbox_ring.h"
#include "SirinEmbedOS/Core/C/CRC32.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include <limits.h>
#include <string.h>

#define SRNOS_BLACKBOX_SECTOR_BYTES 4096u
#define SRNOS_BLACKBOX_PAGE_BYTES 256u
#define SRNOS_BLACKBOX_SLOTS 15u
#define SRNOS_BLACKBOX_HEADER_COMMIT 32u
#define SRNOS_BLACKBOX_RECORD_COMMIT 28u
#define SRNOS_BLACKBOX_COMMIT UINT32_C(0x54494d43)
#define SRNOS_BLACKBOX_MAX_GENERATION ((UINT64_MAX - SRNOS_BLACKBOX_SLOTS) / SRNOS_BLACKBOX_SLOTS)
static const uint8_t volume_magic[4] = {'S','B','V','1'};
static const uint8_t sector_magic[4] = {'S','B','S','1'};
static const uint8_t record_magic[4] = {'S','B','R','1'};

/* These private wrappers only receive a checked in-page field. Endian's
 * byte-span operations retain unaligned-safe encoding on the MCU. */
static uint16_t get16(const uint8_t *p) {
    uint16_t v=0;(void)SRNOS_Endian_LoadLE16((SRNOS_Memory_ByteSpan){p,2},0,&v);return v;
}
static uint32_t get32(const uint8_t *p) {
    uint32_t v=0;(void)SRNOS_Endian_LoadLE32((SRNOS_Memory_ByteSpan){p,4},0,&v);return v;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v=0;(void)SRNOS_Endian_LoadLE64((SRNOS_Memory_ByteSpan){p,8},0,&v);return v;
}
static void put16(uint8_t *p,uint16_t v) {
    (void)SRNOS_Endian_StoreLE16((SRNOS_Memory_MutableByteSpan){p,2},0,v);
}
static void put32(uint8_t *p,uint32_t v) {
    (void)SRNOS_Endian_StoreLE32((SRNOS_Memory_MutableByteSpan){p,4},0,v);
}
static void put64(uint8_t *p,uint64_t v) {
    (void)SRNOS_Endian_StoreLE64((SRNOS_Memory_MutableByteSpan){p,8},0,v);
}
static bool blank(const uint8_t *p,size_t n) { for(size_t i=0;i<n;i++)if(p[i]!=0xff)return false;return true; }
static uint32_t crc_excluding(const uint8_t *p,unsigned crc_offset,unsigned commit_offset) {
    uint32_t c=SRNOS_CRC32Update(SRNOS_CRC32_INIT,p,crc_offset);
    c=SRNOS_CRC32Update(c,p+commit_offset+4,SRNOS_BLACKBOX_PAGE_BYTES-commit_offset-4);
    return c^SRNOS_CRC32_FINAL_XOR;
}
static SRNOS_BlackboxStatus fail(SRNOS_BlackboxRing *r,SRNOS_BlackboxStatus status) {
    r->fault=status;return status;
}
static bool in_range(const SRNOS_BlackboxRing *r,uint32_t a,size_t n) {
    return a>=r->config.base && n<=r->config.size &&
        (uint64_t)a+n<=(uint64_t)r->config.base+r->config.size;
}
static bool read_bytes(SRNOS_BlackboxRing *r,uint32_t a,void *p,size_t n) {
    if(!in_range(r,a,n)||!r->io.read(r->io.context,a,p,n)) { fail(r,SRNOS_BLACKBOX_IO_ERROR);return false; }
    if(r->io.yield)r->io.yield(r->io.context);
    return true;
}
static bool program_bytes(SRNOS_BlackboxRing *r,uint32_t a,const void *p,size_t n) {
    if(!n || n>SRNOS_BLACKBOX_PAGE_BYTES || (a%SRNOS_BLACKBOX_PAGE_BYTES)+n>SRNOS_BLACKBOX_PAGE_BYTES || !in_range(r,a,n) ||
        !r->io.program(r->io.context,a,p,n)) { fail(r,SRNOS_BLACKBOX_IO_ERROR);return false; }
    if(r->io.yield)r->io.yield(r->io.context);
    return true;
}
static uint32_t sector_address(const SRNOS_BlackboxRing *r,uint64_t generation) {
    return r->config.base+SRNOS_BLACKBOX_SECTOR_BYTES+(uint32_t)(generation%r->data_sectors)*SRNOS_BLACKBOX_SECTOR_BYTES;
}
static uint64_t first_generation(const SRNOS_BlackboxRing *r) {
    return r->generation>=r->data_sectors ? r->generation-r->data_sectors+1u : 0u;
}
static uint64_t next_sequence(const SRNOS_BlackboxRing *r) {
    if(r->has_sector && r->generation==SRNOS_BLACKBOX_MAX_GENERATION && r->next_slot==SRNOS_BLACKBOX_SLOTS)return 0;
    return r->has_sector?r->generation*SRNOS_BLACKBOX_SLOTS+r->next_slot+1u:1u;
}
static void volume_page(SRNOS_BlackboxRing *r) {
    uint8_t *p=r->page;memset(p,0xff,SRNOS_BLACKBOX_PAGE_BYTES);memcpy(p,volume_magic,4);
    put16(p+4,1);put16(p+6,36);put32(p+8,r->config.base);put32(p+12,r->config.size);
    put16(p+16,SRNOS_BLACKBOX_SECTOR_BYTES);put16(p+18,SRNOS_BLACKBOX_PAGE_BYTES);put16(p+20,SRNOS_BLACKBOX_SLOTS);put16(p+22,0);
    put32(p+24,r->data_sectors);put32(p+28,crc_excluding(p,28,SRNOS_BLACKBOX_HEADER_COMMIT));
    put32(p+SRNOS_BLACKBOX_HEADER_COMMIT,SRNOS_BLACKBOX_COMMIT);
}
static void sector_page(SRNOS_BlackboxRing *r,uint64_t generation) {
    uint8_t *p=r->page;memset(p,0xff,SRNOS_BLACKBOX_PAGE_BYTES);memcpy(p,sector_magic,4);
    put16(p+4,1);put16(p+6,36);put64(p+8,generation);put32(p+16,r->config.base);
    put32(p+20,r->data_sectors);put32(p+24,(uint32_t)(generation%r->data_sectors));
    put32(p+28,crc_excluding(p,28,SRNOS_BLACKBOX_HEADER_COMMIT));
}
static bool sector_valid(const SRNOS_BlackboxRing *r,const uint8_t *p,uint32_t index,uint64_t *generation) {
    if(memcmp(p,sector_magic,4)||get16(p+4)!=1||get16(p+6)!=36||get32(p+16)!=r->config.base||
        get32(p+20)!=r->data_sectors||get32(p+24)!=index||get32(p+32)!=SRNOS_BLACKBOX_COMMIT||
        get32(p+28)!=crc_excluding(p,28,32))return false;
    uint64_t g=get64(p+8);if(g>SRNOS_BLACKBOX_MAX_GENERATION||g%r->data_sectors!=index)return false;
    *generation=g;return true;
}
static bool record_valid(const uint8_t *p,uint64_t expected) {
    return !memcmp(p,record_magic,4)&&get64(p+4)==expected&&get16(p+20)!=0&&
        get16(p+22)<=SRNOS_BLACKBOX_PAYLOAD_MAX&&get32(p+28)==SRNOS_BLACKBOX_COMMIT&&
        get32(p+24)==crc_excluding(p,24,28);
}
static void copy_record(const uint8_t *p,SRNOS_BlackboxRecord *out) {
    if(!out)return;
    out->sequence=get64(p+4);out->timestamp_us=get64(p+12);
    out->type=get16(p+20);out->length=get16(p+22);
    memcpy(out->data,p+32,SRNOS_BLACKBOX_PAYLOAD_MAX);
}
/* r->page is immutable expected data across callbacks. Writes and all reads
 * must succeed before callers can count this page as durable. */
static SRNOS_BlackboxStatus commit_page(SRNOS_BlackboxRing *r,uint32_t address,unsigned offset) {
    if(!program_bytes(r,address,r->page,SRNOS_BLACKBOX_PAGE_BYTES)||!read_bytes(r,address,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
    if(memcmp(r->page,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    put32(r->page+offset,SRNOS_BLACKBOX_COMMIT);
    if(!program_bytes(r,address+offset,r->page+offset,4)||!read_bytes(r,address,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
    if(memcmp(r->page,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    return SRNOS_BLACKBOX_OK;
}
static SRNOS_BlackboxStatus find_forward(SRNOS_BlackboxRing *r,uint64_t cursor,
    SRNOS_BlackboxRecord *out,uint64_t *found) {
    *found=0;if(!r->has_sector||cursor==UINT64_MAX)return SRNOS_BLACKBOX_EMPTY;
    uint64_t first=first_generation(r),wanted=cursor+1u,g=(wanted-1u)/SRNOS_BLACKBOX_SLOTS;
    if(g<first){g=first;wanted=g*SRNOS_BLACKBOX_SLOTS+1u;}
    if(g>r->generation)return SRNOS_BLACKBOX_EMPTY;
    for(;;) {
        uint32_t a=sector_address(r,g);uint64_t actual;
        if(!read_bytes(r,a,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
        if(sector_valid(r,r->verify,(uint32_t)(g%r->data_sectors),&actual)&&actual==g) {
            unsigned start=(unsigned)(wanted-g*SRNOS_BLACKBOX_SLOTS-1u);
            for(unsigned slot=start;slot<SRNOS_BLACKBOX_SLOTS;slot++) {
                uint64_t seq=g*SRNOS_BLACKBOX_SLOTS+slot+1u;
                if(!read_bytes(r,a+(slot+1u)*SRNOS_BLACKBOX_PAGE_BYTES,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
                if(record_valid(r->verify,seq)){copy_record(r->verify,out);*found=seq;return SRNOS_BLACKBOX_OK;}
            }
        }
        if(g==r->generation)break;
        ++g;wanted=g*SRNOS_BLACKBOX_SLOTS+1u;
    }
    return SRNOS_BLACKBOX_EMPTY;
}
static SRNOS_BlackboxStatus find_newest(SRNOS_BlackboxRing *r,uint64_t *found) {
    *found=0;if(!r->has_sector)return SRNOS_BLACKBOX_EMPTY;
    uint64_t g=r->generation,first=first_generation(r);
    for(;;) {
        uint32_t a=sector_address(r,g);uint64_t actual;
        if(!read_bytes(r,a,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
        if(sector_valid(r,r->verify,(uint32_t)(g%r->data_sectors),&actual)&&actual==g) {
            for(unsigned slot=SRNOS_BLACKBOX_SLOTS;slot>0;slot--) {
                uint64_t seq=g*SRNOS_BLACKBOX_SLOTS+slot;
                if(!read_bytes(r,a+slot*SRNOS_BLACKBOX_PAGE_BYTES,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
                if(record_valid(r->verify,seq)){*found=seq;return SRNOS_BLACKBOX_OK;}
            }
        }
        if(g==first)break;
        --g;
    }
    return SRNOS_BLACKBOX_EMPTY;
}
SRNOS_BlackboxStatus SRNOS_BlackboxRingMount(SRNOS_BlackboxRing *r,
    const SRNOS_BlackboxRingIO *io,const SRNOS_BlackboxRingConfig *config) {
    if(!r||!io||!config)return SRNOS_BLACKBOX_INVALID;
    /* Permit remount with the previously stored configuration/IO fields. */
    SRNOS_BlackboxRingIO selected_io=*io;
    SRNOS_BlackboxRingConfig selected_config=*config;
    io=&selected_io;config=&selected_config;
    memset(r,0,sizeof(*r));
    if(!io->read||!io->program||!io->erase||config->base%SRNOS_BLACKBOX_SECTOR_BYTES||config->size%SRNOS_BLACKBOX_SECTOR_BYTES||
        config->size<3u*SRNOS_BLACKBOX_SECTOR_BYTES||config->base<SRNOS_BLACKBOX_PARTITION_BASE||
        (uint64_t)config->base+config->size>(uint64_t)SRNOS_BLACKBOX_PARTITION_BASE+SRNOS_BLACKBOX_PARTITION_BYTES)
        return fail(r,SRNOS_BLACKBOX_INVALID);
    r->io=*io;r->config=*config;r->data_sectors=config->size/SRNOS_BLACKBOX_SECTOR_BYTES-1u;
    volume_page(r);
    if(!read_bytes(r,config->base,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
    if(memcmp(r->page,r->verify,SRNOS_BLACKBOX_PAGE_BYTES)) {
        /* A complete but different layout is never an interrupted instance
         * of ours, even if its particular bit pattern is compatible. */
        if(get32(r->verify+SRNOS_BLACKBOX_HEADER_COMMIT)==SRNOS_BLACKBOX_COMMIT &&
            get32(r->verify+28)==crc_excluding(r->verify,28,SRNOS_BLACKBOX_HEADER_COMMIT))
            return fail(r,SRNOS_BLACKBOX_INCOMPATIBLE);
        /* Only interrupted programming of this exact immutable volume page
         * may resume. Bits must be a superset of the final image, and ALL
         * remaining bytes in the whole partition must still be erased. */
        for(unsigned i=0;i<SRNOS_BLACKBOX_PAGE_BYTES;i++)if((r->verify[i]&r->page[i])!=r->page[i])
            return fail(r,memcmp(r->verify,volume_magic,4)?SRNOS_BLACKBOX_INCOMPATIBLE:SRNOS_BLACKBOX_CORRUPT);
        for(uint32_t offset=SRNOS_BLACKBOX_PAGE_BYTES;offset<config->size;offset+=SRNOS_BLACKBOX_PAGE_BYTES) {
            if(!read_bytes(r,config->base+offset,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
            if(!blank(r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return fail(r,SRNOS_BLACKBOX_CORRUPT);
        }
        r->mounted=true;return SRNOS_BLACKBOX_OK;
    }
    r->volume_ready=true;
    bool orphan_found=false;
    uint64_t orphan_generation=0;
    for(uint32_t i=0;i<r->data_sectors;i++) {
        uint64_t g;
        uint32_t address=config->base+(i+1u)*SRNOS_BLACKBOX_SECTOR_BYTES;
        if(!read_bytes(r,address,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
        if(sector_valid(r,r->verify,i,&g)) {
            if(!r->has_sector||g>r->generation){r->has_sector=true;r->generation=g;}
        } else {
            /* A damaged header can hide committed records from a newer
             * generation. Their independent CRC/sequence and physical slot
             * identify a high-water mark we must never silently reuse.
             * Even an FF header requires this scan: erase/header corruption
             * need not have touched its record pages. */
            for(unsigned slot=0;slot<SRNOS_BLACKBOX_SLOTS;slot++) {
                if(!read_bytes(r,address+(slot+1u)*SRNOS_BLACKBOX_PAGE_BYTES,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
                uint64_t seq=get64(r->verify+4);
                if(!seq || (seq-1u)%SRNOS_BLACKBOX_SLOTS!=slot)continue;
                g=(seq-1u)/SRNOS_BLACKBOX_SLOTS;
                if(g>SRNOS_BLACKBOX_MAX_GENERATION || g%r->data_sectors!=i || !record_valid(r->verify,seq))continue;
                if(!orphan_found||g>orphan_generation){orphan_found=true;orphan_generation=g;}
            }
        }
    }
    /* Ordinary interrupted recycling may leave old committed records under
     * an invalid header, but a separate newer valid header still survives.
     * If it does not, preserve media and require explicit maintenance rather
     * than roll the persistent sequence backwards. No IO mutation occurs. */
    if(orphan_found&&(!r->has_sector||orphan_generation>=r->generation))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    if(r->has_sector)for(unsigned slot=0;slot<SRNOS_BLACKBOX_SLOTS;slot++) {
        if(!read_bytes(r,sector_address(r,r->generation)+(slot+1u)*SRNOS_BLACKBOX_PAGE_BYTES,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
        /* A torn page, including a partially programmed commit, is consumed.
         * Never fill earlier erased holes before a later programmed page. */
        if(!blank(r->verify,SRNOS_BLACKBOX_PAGE_BYTES))r->next_slot=(uint8_t)(slot+1u);
    }
    SRNOS_BlackboxStatus status=find_forward(r,0,NULL,&r->oldest_sequence);
    if(status!=SRNOS_BLACKBOX_OK&&status!=SRNOS_BLACKBOX_EMPTY)return status;
    status=find_newest(r,&r->newest_sequence);
    if(status!=SRNOS_BLACKBOX_OK&&status!=SRNOS_BLACKBOX_EMPTY)return status;
    r->mounted=true;return SRNOS_BLACKBOX_OK;
}
static SRNOS_BlackboxStatus initialize_volume(SRNOS_BlackboxRing *r) {
    volume_page(r);
    if(!read_bytes(r,r->config.base,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
    /* Preserve already-programmed commit bits during recovery of first-use
     * power loss. Program final body, validate it, then complete the commit. */
    memcpy(r->page+SRNOS_BLACKBOX_HEADER_COMMIT,r->verify+SRNOS_BLACKBOX_HEADER_COMMIT,4);
    SRNOS_BlackboxStatus status=commit_page(r,r->config.base,SRNOS_BLACKBOX_HEADER_COMMIT);
    if(status==SRNOS_BLACKBOX_OK)r->volume_ready=true;
    return status;
}
static SRNOS_BlackboxStatus new_sector(SRNOS_BlackboxRing *r) {
    if(r->has_sector&&r->generation==SRNOS_BLACKBOX_MAX_GENERATION)return SRNOS_BLACKBOX_FULL;
    uint64_t g=r->has_sector?r->generation+1u:0;
    uint32_t address=sector_address(r,g);
    /* At least two circular sectors: the current highest-generation sector
     * is NEVER the erase target, even when its only record is interrupted. */
    if(r->has_sector&&address==sector_address(r,r->generation))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    if(!in_range(r,address,SRNOS_BLACKBOX_SECTOR_BYTES)||!r->io.erase(r->io.context,address))return fail(r,SRNOS_BLACKBOX_IO_ERROR);
    if(r->io.yield)r->io.yield(r->io.context);
    for(unsigned offset=0;offset<SRNOS_BLACKBOX_SECTOR_BYTES;offset+=SRNOS_BLACKBOX_PAGE_BYTES) {
        if(!read_bytes(r,address+offset,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
        if(!blank(r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    }
    sector_page(r,g);
    SRNOS_BlackboxStatus status=commit_page(r,address,SRNOS_BLACKBOX_HEADER_COMMIT);
    if(status!=SRNOS_BLACKBOX_OK)return status;
    r->has_sector=true;r->generation=g;r->next_slot=0;
    return SRNOS_BLACKBOX_OK;
}
SRNOS_BlackboxStatus SRNOS_BlackboxRingAppend(SRNOS_BlackboxRing *r,uint16_t type,
    uint64_t timestamp_us,const void *data,size_t length,uint64_t *sequence) {
    if(sequence)*sequence=0;
    if(!r||!sequence||!type||length>SRNOS_BLACKBOX_PAYLOAD_MAX||(!data&&length))return SRNOS_BLACKBOX_INVALID;
    if(r->fault!=SRNOS_BLACKBOX_OK)return r->fault;
    if(!r->mounted)return SRNOS_BLACKBOX_NOT_READY;
    SRNOS_BlackboxStatus status;
    if(!r->volume_ready&&(status=initialize_volume(r))!=SRNOS_BLACKBOX_OK)return status;
    if((!r->has_sector||r->next_slot==SRNOS_BLACKBOX_SLOTS)&&(status=new_sector(r))!=SRNOS_BLACKBOX_OK)return status;
    uint64_t seq=r->generation*SRNOS_BLACKBOX_SLOTS+r->next_slot+1u;
    uint32_t a=sector_address(r,r->generation)+(r->next_slot+1u)*SRNOS_BLACKBOX_PAGE_BYTES;
    if(!read_bytes(r,a,r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return r->fault;
    if(!blank(r->verify,SRNOS_BLACKBOX_PAGE_BYTES))return fail(r,SRNOS_BLACKBOX_CORRUPT);
    uint8_t *p=r->page;memset(p,0xff,SRNOS_BLACKBOX_PAGE_BYTES);memcpy(p,record_magic,4);
    put64(p+4,seq);put64(p+12,timestamp_us);put16(p+20,type);put16(p+22,(uint16_t)length);
    if(length)memcpy(p+32,data,length);
    put32(p+24,crc_excluding(p,24,SRNOS_BLACKBOX_RECORD_COMMIT));
    if((status=commit_page(r,a,SRNOS_BLACKBOX_RECORD_COMMIT))!=SRNOS_BLACKBOX_OK)return status;
    ++r->next_slot;r->newest_sequence=seq;
    if(!r->oldest_sequence)r->oldest_sequence=seq;
    else if((r->oldest_sequence-1u)/SRNOS_BLACKBOX_SLOTS<first_generation(r)) {
        /* Only rollover can invalidate the cached oldest surviving record.
         * Do not re-scan sparse/corrupted history for every appended page. */
        status=find_forward(r,0,NULL,&r->oldest_sequence);
        if(status!=SRNOS_BLACKBOX_OK)return status==SRNOS_BLACKBOX_EMPTY?fail(r,SRNOS_BLACKBOX_CORRUPT):status;
    }
    *sequence=seq;return SRNOS_BLACKBOX_OK;
}
SRNOS_BlackboxStatus SRNOS_BlackboxRingReadAfter(SRNOS_BlackboxRing *r,uint64_t cursor,SRNOS_BlackboxRecord *record) {
    if(!r||!record)return SRNOS_BLACKBOX_INVALID;
    if(r->fault!=SRNOS_BLACKBOX_OK)return r->fault;
    if(!r->mounted)return SRNOS_BLACKBOX_NOT_READY;
    uint64_t found;return find_forward(r,cursor,record,&found);
}
void SRNOS_BlackboxRingGetSnapshot(const SRNOS_BlackboxRing *r,SRNOS_BlackboxRingSnapshot *s) {
    if(!r||!s)return;
    *s=(SRNOS_BlackboxRingSnapshot){r->oldest_sequence,r->newest_sequence,next_sequence(r),
        r->data_sectors*SRNOS_BLACKBOX_SLOTS,r->data_sectors,r->mounted,r->volume_ready,r->fault};
}
