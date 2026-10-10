#ifndef SRNOS_MCP2518FD_FIFO_INTERNAL_H
#define SRNOS_MCP2518FD_FIFO_INTERNAL_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single CAN4 owner. Static storage outside ITCM; not DMA storage. The SPI
 * dispatcher alone stages these bytes in the existing noncached DMA area. */
typedef struct {
    uint8_t index, channel, stride, count, status, length;
    bool transmit, fd, pending, ready, quarantined;
    uint16_t base, ua, next_ua;
    unsigned phase;
    uint8_t object[72], response[74];
    uint32_t errors, uncertain, reconciled;
} SRNOS_MCPFifo;
void SRNOS_MCPFifoInit(SRNOS_MCPFifo *fifo,uint8_t index,bool transmit,bool fd);
/* RX: 1 delivered, 0 empty, -1 I/O error/pending. Delivers exactly once after
 * UINC is confirmed, including a previously uncertain commit. */
int SRNOS_MCPFifoReceive(SRNOS_MCPFifo *fifo,uint8_t object[72]);
/* TX: true transfers ownership (possibly to retained in-flight state). A
 * subsequent frame cannot overwrite it until UA reconciliation completes. */
bool SRNOS_MCPFifoTransmit(SRNOS_MCPFifo *fifo,const uint8_t header[8],const uint8_t *data,uint8_t length);
bool SRNOS_MCPFifoReconcile(SRNOS_MCPFifo *fifo);
#ifdef __cplusplus
}
#endif
#endif
