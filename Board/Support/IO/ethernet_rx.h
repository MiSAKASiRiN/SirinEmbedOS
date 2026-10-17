#ifndef SRNOS_ETHERNET_RX_INTERNAL_H
#define SRNOS_ETHERNET_RX_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
typedef struct SRNOS_EthernetRxBuffer {
    struct SRNOS_EthernetRxBuffer *next;
    uint16_t length;
    bool used;
} SRNOS_EthernetRxBuffer;
/* HAL ReadData clears pRxStart after delivery but leaves pRxEnd unchanged.
 * Only the head determines whether this is a new packet. This function runs
 * exclusively in the Ethernet worker, not in the completion IRQ. */
static inline void SRNOS_EthernetRxLink(void **start, void **end,
                                       SRNOS_EthernetRxBuffer *node, uint16_t length)
{
    node->length = length;
    node->next = 0;
    if (!*start) *start = node;
    else ((SRNOS_EthernetRxBuffer *)*end)->next = node;
    *end = node;
}
#endif
