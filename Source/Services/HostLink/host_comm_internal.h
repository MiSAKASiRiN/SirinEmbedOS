#pragma once
#include "SirinEmbedOS/Services/HostLink/HostLink.h"
#include "Codec/link_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *user;
    bool (*init)(void *user);
    bool (*start_rx)(void *user, TaskHandle_t notify_task, uint32_t notify_bits);
    size_t (*rx_read)(void *user, uint8_t *out, size_t capacity);
    uint32_t (*rx_overflow_bytes)(void *user);
    SRNOS_HostLinkCoreTxResult (*tx_submit)(void *user,
                                      const uint8_t *data,
                                      size_t length);
    bool (*tx_take_complete)(void *user, bool *success);
    bool (*is_connected)(void *user);
    uint32_t (*session_generation)(void *user); /* optional; detects quick DTR reopen */
} SRNOS_HostLinkTransport;

typedef struct {
    SRNOS_HostLinkTransport transport;
    SRNOS_HostLinkCoreNowMsFn now_ms;
    SRNOS_HostLinkCoreCyclesFn cycles;
    void *clock_user;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    uint8_t *rx_drain_buffer;
    size_t rx_drain_buffer_size;
    SRNOS_HostLinkProjectSetupFn project_setup;
    SRNOS_HostLinkSnapshotFn snapshot;
    void *project_user;
    uint32_t task_wait_ms;
    uint32_t snapshot_period_ms;
} SRNOS_HostLinkConfig;

SRNOS_HostLinkResult SRNOS_HostLinkConfigure(
    const SRNOS_HostLinkConfig *config);
bool SRNOS_HostLinkInitResources(void);
/* Prepare/freeze registrations before any service worker runs. */
SRNOS_HostLinkResult SRNOS_HostLinkPrepareLink(void);
void SRNOS_HostLinkTask(void *argument);
void SRNOS_HostLinkWorkerTask(void *argument);

#if defined(SRNOS_HOST_LINK_TESTING)
SRNOS_HostLinkResult SRNOS_HostLinkTestStart(void);
void SRNOS_HostLinkTestPollOnce(void);
uint32_t SRNOS_HostLinkTestNextWaitMs(void);
bool SRNOS_HostLinkTestPendingCompletion(void);
bool SRNOS_HostLinkTestRxBudgetExhausted(void);
bool SRNOS_HostLinkTestWorkerOnce(void);
#endif
#ifdef __cplusplus
}
#endif
