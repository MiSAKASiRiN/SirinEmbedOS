#ifndef SRNOS_ETHERNET_BOARD_H
#define SRNOS_ETHERNET_BOARD_H
#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
void SRNOS_EthernetBoardMAC(uint8_t mac[6]);
bool SRNOS_EthernetBoardInit(uint8_t mac[6], uint32_t *clock_hz);
void SRNOS_EthernetBoardReset(void);
bool SRNOS_EthernetBoardReadPHY(uint8_t address, uint8_t reg, uint32_t *value);
bool SRNOS_EthernetBoardWritePHY(uint8_t address, uint8_t reg, uint32_t value);
bool SRNOS_EthernetBoardLink(uint16_t speed, bool full);
/* Worker-only. RX: 1 frame, 0 empty, -1 invalid/error. TX: 1 completed,
 * 0 DMA still owns, -1 descriptor error. Submit must not overlap. */
int SRNOS_EthernetBoardReceive(SRNOS_EthernetFrame *frame);
bool SRNOS_EthernetBoardSend(const uint8_t *frame, uint16_t length);
int SRNOS_EthernetBoardTxComplete(void);
uint32_t SRNOS_EthernetBoardError(void);
#endif
