#pragma once
#include "SirinEmbedOS/Core/C/Endian.h"
#include <string.h>
static inline uint16_t SRNOS_VideoLinkU16(const uint8_t *p) {
    uint16_t v=0; SRNOS_Endian_LoadLE16((SRNOS_Memory_ByteSpan){p,2},0,&v); return v;
}
static inline void SRNOS_VideoLinkPut16(uint8_t *p,uint16_t v) {
    SRNOS_Endian_StoreLE16((SRNOS_Memory_MutableByteSpan){p,2},0,v);
}
static inline void SRNOS_VideoLinkPut32(uint8_t *p,uint32_t v) {
    SRNOS_Endian_StoreLE32((SRNOS_Memory_MutableByteSpan){p,4},0,v);
}
static inline uint16_t SRNOS_VideoLinkCRC(const uint8_t *p,size_t n,uint16_t value,uint16_t poly) {
    while(n--) { value^=*p++; for(unsigned b=0;b<8;b++) value=(uint16_t)((value>>1)^((value&1)?poly:0)); }
    return value;
}
static inline size_t SRNOS_VideoLinkEncode(uint16_t cmd,const uint8_t *payload,size_t size,uint8_t seq,uint8_t *out) {
    if(!payload||!out||size>300)return 0;
    out[0]=0xa5; SRNOS_VideoLinkPut16(out+1,(uint16_t)size);out[3]=seq;
    out[4]=(uint8_t)SRNOS_VideoLinkCRC(out,4,0xff,0x8c);
    SRNOS_VideoLinkPut16(out+5,cmd);memcpy(out+7,payload,size);
    SRNOS_VideoLinkPut16(out+7+size,SRNOS_VideoLinkCRC(out,size+7,0xffff,0x8408));
    return size+9;
}
