#pragma once
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
#include "Codec/link_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#define SRNOS_SIRIN_LINK_COMM_STACK_BYTES 3072u
#define SRNOS_SIRIN_LINK_COMMAND_STACK_BYTES 2048u
#define SRNOS_SIRIN_LINK_WORK_DEPTH 4u
#define SRNOS_SIRIN_LINK_COMPLETION_DEPTH 4u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *user;
    bool (*init)(void *user);
    bool (*start_rx)(void *user, TaskHandle_t notify_task, uint32_t notify_bits);
    size_t (*rx_read)(void *user, uint8_t *out, size_t capacity);
    uint32_t (*rx_overflow_bytes)(void *user);
    SRNOS_SirinLinkCoreTxResult (*tx_submit)(void *user,
                                      const uint8_t *data,
                                      size_t length);
    bool (*tx_take_complete)(void *user, bool *success);
    bool (*is_connected)(void *user);
    uint32_t (*session_generation)(void *user); /* optional; detects quick DTR reopen */
} SRNOS_SirinLinkTransport;

typedef struct {
    SRNOS_SirinLinkTransport transport;
    SRNOS_SirinLinkCoreNowMsFn now_ms;
    SRNOS_SirinLinkCoreCyclesFn cycles;
    void *clock_user;
    uint8_t *tx_storage;
    size_t tx_storage_size;
    uint8_t *rx_drain_buffer;
    size_t rx_drain_buffer_size;
    SRNOS_SirinLinkProjectSetupFn project_setup;
    SRNOS_SirinLinkSnapshotFn snapshot;
    void *project_user;
    uint32_t task_wait_ms;
    uint32_t snapshot_period_ms;
} SRNOS_SirinLinkConfig;

SRNOS_SirinLinkResult SRNOS_SirinLinkConfigure(
    const SRNOS_SirinLinkConfig *config);
bool SRNOS_SirinLinkInitResources(void);
uint32_t SRNOS_SirinLinkHeapBudget(bool enabled);
/* Prepare/freeze registrations before any service worker runs. */
SRNOS_SirinLinkResult SRNOS_SirinLinkPrepareLink(void);
void SRNOS_SirinLinkTask(void *argument);
void SRNOS_SirinLinkWorkerTask(void *argument);

#if defined(SRNOS_SIRIN_LINK_TESTING)
SRNOS_SirinLinkResult SRNOS_SirinLinkTestStart(void);
void SRNOS_SirinLinkTestPollOnce(void);
uint32_t SRNOS_SirinLinkTestNextWaitMs(void);
bool SRNOS_SirinLinkTestPendingCompletion(void);
bool SRNOS_SirinLinkTestRxBudgetExhausted(void);
bool SRNOS_SirinLinkTestWorkerOnce(void);
#endif
#ifdef __cplusplus
}
#endif
