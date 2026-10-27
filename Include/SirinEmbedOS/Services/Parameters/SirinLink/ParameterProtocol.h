#ifndef SRNOS_PARAMETER_PROTOCOL_H
#define SRNOS_PARAMETER_PROTOCOL_H

#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRNOS_PARAMETER_PROTOCOL_SCHEMA = 0x01u,
    SRNOS_PARAMETER_PROTOCOL_READ = 0x02u,
    SRNOS_PARAMETER_PROTOCOL_WRITE = 0x03u,
    SRNOS_PARAMETER_PROTOCOL_SAVE = 0x04u,
    SRNOS_PARAMETER_PROTOCOL_LOAD = 0x05u,
    SRNOS_PARAMETER_PROTOCOL_STATE = 0x06u,
    SRNOS_PARAMETER_PROTOCOL_IDENTITY = 0x07u,
    SRNOS_PARAMETER_PROTOCOL_STABLE_IDS = 0x08u,
} SRNOS_ParameterProtocolMessage;

typedef bool (*SRNOS_ParameterProtocolDeferFn)(void *user,
                                              uint8_t message,
                                              uint32_t sequence);

typedef struct {
    SRNOS_ParameterProtocolDeferFn defer;
    void *user;
    bool disable_storage;
} SRNOS_ParameterProtocolConfig;

SRNOS_SirinLinkResult SRNOS_ParameterProtocolRegister(
    SRNOS_SirinLinkRegistration *link,
    const SRNOS_ParameterProtocolConfig *config);

uint16_t SRNOS_ParameterProtocolRunStorage(uint8_t message,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_PARAMETER_PROTOCOL_H */
