//
// ExternalCAN.h - MCP2518FD backend for the shared CAN router
//
#ifndef SRNOS_EXFDCAN_ROUTER_H
#define SRNOS_EXFDCAN_ROUTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"

#include "SirinEmbedOS/IO/SPI/SPI.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES   (8u)

typedef struct
{
    uint16_t sid;
    uint8_t  dlc;
    uint8_t  data[SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES];
    uint32_t timestamp_ms;
} SRNOS_ExternalCANRxFrame;

typedef SRNOS_DbgExternalCAN SRNOS_ExternalCANStats;

// Optional readout helpers for debugging.
bool SRNOS_ExternalCANGetStats(uint8_t dev, SRNOS_ExternalCANStats *out);
size_t SRNOS_ExternalCANRxRead(uint8_t dev, SRNOS_ExternalCANRxFrame *out, size_t max);

SRNOS_CANChannel SRNOS_ExternalCANChannelFromDev(uint8_t dev);
int SRNOS_ExternalCANDevFromChannel(SRNOS_CANChannel ch);


/* Copies task-context statistics; no driver or hardware handle escapes. */
void SRNOS_ExternalCANGetSPIStatistics(SRNOS_SPIStatistics *out);
#ifdef __cplusplus
}
#endif

#endif // SRNOS_EXFDCAN_ROUTER_H
