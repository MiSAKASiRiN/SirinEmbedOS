#include "fifo_internal.h"
#include "spi_adapter_internal.h"
#include "vendor/drv_canfdspi_register.h"
#include <string.h>

static uint32_t SRNOS_MCPFifoWord(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int SRNOS_MCPFifoCommand(uint8_t *tx,uint16_t address,unsigned op,unsigned bytes) {
    tx[0]=(uint8_t)((op<<4)|(address>>8));tx[1]=(uint8_t)address;
    memset(tx+2,0,bytes);return (int)bytes+2;
}
static uint16_t SRNOS_MCPFifoRegister(const SRNOS_MCPFifo *f,uint16_t offset) {
    return (uint16_t)(cREGADDR_CiFIFOCON+f->channel*CiFIFO_OFFSET+offset);
}
static int SRNOS_MCPFifoIncrement(SRNOS_MCPFifo *f,uint8_t *tx) {
    f->pending=true;f->phase=3;
    SRNOS_MCPFifoCommand(tx,SRNOS_MCPFifoRegister(f,1),cINSTRUCTION_WRITE,1);
    /* Microchip requires UINC and TXREQ in the same write. */
    tx[2]=f->transmit?3u:1u;return 3;
}
static int SRNOS_MCPFifoPrepare(void *ctx,unsigned completed,uint8_t *tx,uint8_t **rx) {
    SRNOS_MCPFifo *f=ctx;*rx=f->response;
    if(!completed) { f->phase=0;return SRNOS_MCPFifoCommand(tx,SRNOS_MCPFifoRegister(f,4),cINSTRUCTION_READ,8); }
    if(f->phase==0) {
        f->status=f->response[2];
        if(!(f->status&1u))return 0;
        uint32_t ua=SRNOS_MCPFifoWord(f->response+6);
        if(ua<f->base || ua>=f->base+f->count*f->stride || (ua-f->base)%f->stride) {
            f->quarantined=true;return -1;
        }
        f->ua=(uint16_t)ua;
        f->next_ua=(uint16_t)(ua+f->stride);
        if(f->next_ua==f->base+f->count*f->stride)f->next_ua=f->base;
        f->phase=1;
        if(f->transmit) {
            unsigned bytes=(8u+f->length+3u)&~3u;
            int n=SRNOS_MCPFifoCommand(tx,(uint16_t)(0x400u+ua),cINSTRUCTION_WRITE,bytes);
            memcpy(tx+2,f->object,bytes);return n;
        }
        return SRNOS_MCPFifoCommand(tx,(uint16_t)(0x400u+ua),cINSTRUCTION_READ,16);
    }
    if(f->phase==1 && !f->transmit) {
        static const uint8_t lengths[16]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
        memcpy(f->object,f->response+2,16);
        /* Classic DLC 9..15 still carries eight bytes. FDF is control bit 7. */
        f->length=f->fd && (f->object[4]&0x80u)?lengths[f->object[4]&15u]:8u;
        if(f->length>8) {
            f->phase=2;
            return SRNOS_MCPFifoCommand(tx,(uint16_t)(0x410u+f->ua),cINSTRUCTION_READ,f->length-8u);
        }
    } else if(f->phase==2) memcpy(f->object+16,f->response+2,f->length-8u);
    else if(f->phase==3) { f->pending=false;f->ready=true;return 0; }
    return SRNOS_MCPFifoIncrement(f,tx);
}
/* One bounded UA read, optionally one confirmed-safe UINC retry. Never reset
 * a FIFO on an ambiguous result: doing so would hide loss or repeat frames. */
static int SRNOS_MCPFifoReconcilePrepare(void *ctx,unsigned completed,uint8_t *tx,uint8_t **rx) {
    SRNOS_MCPFifo *f=ctx;*rx=f->response;
    if(!completed)return SRNOS_MCPFifoCommand(tx,SRNOS_MCPFifoRegister(f,8),cINSTRUCTION_READ,4);
    if(completed==1) {
        uint32_t ua=SRNOS_MCPFifoWord(f->response+2);
        if(ua==f->next_ua) { f->pending=false;f->ready=true;return 0; }
        if(ua!=f->ua) { f->quarantined=true;return -1; }
        return SRNOS_MCPFifoIncrement(f,tx); /* UA proved the previous write did not advance. */
    }
    f->pending=false;f->ready=true;return 0;
}
void SRNOS_MCPFifoInit(SRNOS_MCPFifo *f,uint8_t index,bool transmit,bool fd) {
    memset(f,0,sizeof(*f));f->index=index;f->channel=transmit?2:1;
    f->transmit=transmit;f->fd=fd;f->stride=fd?72:16;
    f->count=fd?(transmit?12:16):32;
    f->base=transmit?(fd?16u*72u:32u*16u):0;
}
bool SRNOS_MCPFifoReconcile(SRNOS_MCPFifo *f) {
    if(f->quarantined)return false;
    if(!f->pending)return true;
    if(SRNOS_MCP2518FDSequence(f->index,SRNOS_MCPFifoReconcilePrepare,f)!=SRNOS_SPI_OK) {
        ++f->errors;return false;
    }
    ++f->reconciled;return !f->pending;
}
int SRNOS_MCPFifoReceive(SRNOS_MCPFifo *f,uint8_t object[72]) {
    if(!f || f->transmit || !object || f->quarantined)return -1;
    if(f->pending) { if(!SRNOS_MCPFifoReconcile(f))return -1; }
    else {
        f->ready=false;
        if(SRNOS_MCP2518FDSequence(f->index,SRNOS_MCPFifoPrepare,f)!=SRNOS_SPI_OK) {
            ++f->errors;if(f->pending)++f->uncertain;return -1;
        }
    }
    if(!f->ready)return 0;
    memcpy(object,f->object,72);f->ready=false;return 1;
}
bool SRNOS_MCPFifoTransmit(SRNOS_MCPFifo *f,const uint8_t header[8],const uint8_t *data,uint8_t length) {
    if(!f || !f->transmit || f->quarantined || f->pending || length>(f->fd?64:8) || !header || (!data && length))return false;
    f->ready=false;f->length=length;
    memset(f->object,0,sizeof(f->object));memcpy(f->object,header,8);
    if(length)memcpy(f->object+8,data,length);
    if(SRNOS_MCP2518FDSequence(f->index,SRNOS_MCPFifoPrepare,f)!=SRNOS_SPI_OK) {
        ++f->errors;
        if(f->pending) { ++f->uncertain;return true; }
        return false;
    }
    return f->ready;
}
