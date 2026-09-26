#ifndef OMX_TSKPTT_PARAM_CONSOLE_H
#define OMX_TSKPTT_PARAM_CONSOLE_H

#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TSKPTT_PARAM_SERVICE_SCHEMA = 0x01u,
    TSKPTT_PARAM_SERVICE_READ = 0x02u,
    TSKPTT_PARAM_SERVICE_WRITE = 0x03u,
    TSKPTT_PARAM_SERVICE_SAVE = 0x04u,
    TSKPTT_PARAM_SERVICE_LOAD = 0x05u,
} tskptt_param_service_message_t;

typedef bool (*tskptt_param_service_defer_fn)(void *user,
                                              uint8_t message,
                                              uint32_t sequence);

typedef struct {
    tskptt_param_service_defer_fn defer;
    void *user;
} tskptt_param_service_config_t;

srn_link_result_t TSKPTT_ParamService_Register(
    srn_link_t *link,
    const tskptt_param_service_config_t *config);

uint16_t TSKPTT_ParamService_RunStorage(uint8_t message,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif /* OMX_TSKPTT_PARAM_CONSOLE_H */
