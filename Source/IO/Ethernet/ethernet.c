#include "ethernet_internal.h"
#include "IO/ethernet_board.h"
#include "Devices/LAN8720/lan8720.h"
#include "Kernel/thread_internal.h"
#include "queue.h"
#include <string.h>

#define SRNOS_ETHERNET_FRAME_TTL_MS 100u
#define SRNOS_ETHERNET_LINK_POLL_MS 250u
#define SRNOS_ETHERNET_RETRY_MS 1000u
typedef struct {
    uint32_t epoch;
    SRNOS_EthernetFrame frame;
} queued_frame_t;
static SRNOS_EthernetConfig configuration;
static SRNOS_EthernetStatus current, published;
static uint32_t accepted;
static StaticQueue_t tx_control, rx_control;
static uint8_t tx_storage[SRNOS_ETHERNET_QUEUE_DEPTH * sizeof(queued_frame_t)];
static uint8_t rx_storage[SRNOS_ETHERNET_QUEUE_DEPTH * sizeof(queued_frame_t)];
static QueueHandle_t tx_queue, rx_queue;
static TaskHandle_t worker;
static bool prepared, initialized, tx_busy, saturated, network_owner;
static uint32_t next_init, next_link, tx_started;
static queued_frame_t packet; /* Worker-owned, never on its small stack. */

static void publish(void)
{
    taskENTER_CRITICAL();
    published = current;
    taskEXIT_CRITICAL();
}
bool SRNOS_EthernetGetStatus(SRNOS_EthernetStatus *status)
{
    if (!status || !SRNOS_KernelTaskContext()) return false;
    taskENTER_CRITICAL();
    *status = published;
    status->tx_accepted = accepted;
    taskEXIT_CRITICAL();
    return true;
}
static SRNOS_EthernetResult send_frame(const uint8_t *frame, uint16_t length, bool check_epoch, uint32_t epoch)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_ETHERNET_CONTEXT;
    if (!frame || length < 14u || length > SRNOS_ETHERNET_FRAME_MAX) return SRNOS_ETHERNET_INVALID;
    SRNOS_EthernetStatus status;
    SRNOS_EthernetGetStatus(&status);
    if (status.state != SRNOS_ETHERNET_LINK_UP) return SRNOS_ETHERNET_NOT_READY;
    if (check_epoch && status.link_epoch != epoch) return SRNOS_ETHERNET_NOT_READY;
    if (memcmp(frame + 6, status.mac, 6)) return SRNOS_ETHERNET_INVALID;
    queued_frame_t item = {.epoch = status.link_epoch};
    item.frame.length = length;
    item.frame.received_ms = SRNOS_TimeMs();
    memcpy(item.frame.data, frame, length);
    if (xQueueSend(tx_queue, &item, 0) != pdPASS) return SRNOS_ETHERNET_FULL;
    taskENTER_CRITICAL();
    ++accepted;
    taskEXIT_CRITICAL();
    xTaskNotifyGive(worker);
    return SRNOS_ETHERNET_OK;
}
SRNOS_EthernetResult SRNOS_EthernetNetworkSend(const uint8_t *frame, uint16_t length, uint32_t epoch)
{ return send_frame(frame, length, true, epoch); }
SRNOS_EthernetResult SRNOS_EthernetNetworkReceive(SRNOS_EthernetFrame *frame)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_ETHERNET_CONTEXT;
    if (!frame) return SRNOS_ETHERNET_INVALID;
    SRNOS_EthernetStatus status;
    SRNOS_EthernetGetStatus(&status);
    if (status.state != SRNOS_ETHERNET_LINK_UP) return SRNOS_ETHERNET_NOT_READY;
    queued_frame_t item;
    for (unsigned i = 0; i < SRNOS_ETHERNET_QUEUE_DEPTH; ++i) {
        if (xQueueReceive(rx_queue, &item, 0) != pdPASS) break;
        if (item.epoch == status.link_epoch &&
            (uint32_t)(SRNOS_TimeMs() - item.frame.received_ms) <= SRNOS_ETHERNET_FRAME_TTL_MS) {
            *frame = item.frame;
            return SRNOS_ETHERNET_OK;
        }
    }
    return SRNOS_ETHERNET_EMPTY;
}
bool SRNOS_EthernetClaimNetwork(void)
{
    if (!prepared || !configuration.enabled || worker || network_owner) return false;
    network_owner = true;
    return true;
}
SRNOS_EthernetResult SRNOS_EthernetSend(const uint8_t *frame, uint16_t length)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_ETHERNET_CONTEXT;
    return network_owner ? SRNOS_ETHERNET_OWNED : send_frame(frame, length, false, 0);
}
SRNOS_EthernetResult SRNOS_EthernetReceive(SRNOS_EthernetFrame *frame)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_ETHERNET_CONTEXT;
    return network_owner ? SRNOS_ETHERNET_OWNED : SRNOS_EthernetNetworkReceive(frame);
}
void SRNOS_EthernetWakeFromISR(void)
{
    if (!worker) return;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(worker, &wake);
    portYIELD_FROM_ISR(wake);
}
bool SRNOS_EthernetPrepare(const SRNOS_EthernetConfig *config)
{
    if (prepared || !SRNOS_EthernetConfigValid(config)) return false;
    configuration = *config;
    current.state = config->enabled ? SRNOS_ETHERNET_STARTING : SRNOS_ETHERNET_DISABLED;
    published = current;
    prepared = true;
    return true;
}
static void recover(uint32_t error)
{
    if (current.state == SRNOS_ETHERNET_LINK_UP) ++current.link_changes;
    current.state = error ? SRNOS_ETHERNET_ERROR : SRNOS_ETHERNET_LINK_DOWN;
    current.last_error = error;
    current.speed_mbps = 0;
    current.full_duplex = false;
    ++current.link_epoch;
    if (error) ++current.recoveries;
    publish(); /* Reject new sends before flushing. Late old-epoch sends expire. */
    SRNOS_EthernetBoardReset();
    initialized = false;
    if (tx_busy) { ++current.tx_dropped; tx_busy = false; }
    while (xQueueReceive(tx_queue, &packet, 0) == pdPASS) ++current.tx_dropped;
    while (xQueueReceive(rx_queue, &packet, 0) == pdPASS) ++current.rx_dropped;
    next_init = SRNOS_TimeMs() + SRNOS_ETHERNET_RETRY_MS;
}
static bool initialize(void)
{
    if (!SRNOS_EthernetBoardInit(current.mac, &current.ref_clock_hz)) return false;
    bool found = false;
    for (uint8_t address = 0; address < 32; ++address) {
        uint32_t id1, id2;
        if (!SRNOS_EthernetBoardReadPHY(address, SRNOS_LAN8720_ID1, &id1) ||
            !SRNOS_EthernetBoardReadPHY(address, SRNOS_LAN8720_ID2, &id2)) return false;
        if (!SRNOS_LAN8720Identify(id1, id2)) continue;
        if (found) return false; /* Fixed board expects exactly one PHY. */
        current.phy_address = address;
        current.phy_id1 = (uint16_t)id1;
        current.phy_id2 = (uint16_t)id2;
        found = true;
    }
    if (!found || !SRNOS_EthernetBoardWritePHY(current.phy_address,
            SRNOS_LAN8720_BCR, SRNOS_LAN8720_RESTART_AUTONEG)) return false;
    initialized = true;
    current.state = SRNOS_ETHERNET_LINK_DOWN;
    next_link = SRNOS_TimeMs();
    return true;
}
static void step(void *context)
{
    (void)context;
    saturated = false;
    uint32_t now = SRNOS_TimeMs();
    if (!initialized) {
        if ((int32_t)(now - next_init) < 0) return;
        current.state = SRNOS_ETHERNET_STARTING;
        publish();
        if (!initialize()) { recover(1); publish(); return; }
        now = SRNOS_TimeMs();
    }
    if ((int32_t)(now - next_link) >= 0) {
        uint32_t bsr, special;
        /* BMSR link status is latched low: read twice. */
        if (!SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_BSR, &bsr) ||
            !SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_BSR, &bsr) ||
            !SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_SPECIAL, &special)) {
            recover(2); publish(); return;
        }
        current.phy_bsr = (uint16_t)bsr;
        current.phy_special = (uint16_t)special;
        uint32_t control, advertised, partner, modes;
        if (!SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_BCR, &control) ||
            !SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_ANAR, &advertised) ||
            !SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_ANLPAR, &partner) ||
            !SRNOS_EthernetBoardReadPHY(current.phy_address, SRNOS_LAN8720_MODES, &modes)) {
            recover(2); publish(); return;
        }
        current.phy_bcr = (uint16_t)control;
        current.phy_anar = (uint16_t)advertised;
        current.phy_anlpar = (uint16_t)partner;
        current.phy_modes = (uint16_t)modes;
        uint16_t speed = 0;
        bool full = false;
        bool up = SRNOS_LAN8720Link(bsr, special, &speed, &full);
        if (current.state == SRNOS_ETHERNET_LINK_UP &&
            (!up || speed != current.speed_mbps || full != current.full_duplex)) {
            recover(0); publish(); return;
        }
        if (up && current.state != SRNOS_ETHERNET_LINK_UP) {
            if (!SRNOS_EthernetBoardLink(speed, full)) { recover(3); publish(); return; }
            current.state = SRNOS_ETHERNET_LINK_UP;
            current.speed_mbps = speed;
            current.full_duplex = full;
            ++current.link_epoch;
            ++current.link_changes;
        }
        next_link = now + SRNOS_ETHERNET_LINK_POLL_MS;
    }
    if (current.state == SRNOS_ETHERNET_LINK_UP) {
        uint32_t error = SRNOS_EthernetBoardError();
        if (error) { recover(error); publish(); return; }
        for (unsigned i = 0; i < 8; ++i) {
            int result = SRNOS_EthernetBoardReceive(&packet.frame);
            if (result == 0) break;
            saturated = i == 7;
            if (result < 0) { ++current.rx_errors; continue; }
            packet.epoch = current.link_epoch;
            packet.frame.received_ms = now;
            ++current.rx_frames;
            if (xQueueSend(rx_queue, &packet, 0) != pdPASS) ++current.rx_dropped;
        }
        if (tx_busy) {
            int result = SRNOS_EthernetBoardTxComplete();
            if (result != 0) {
                tx_busy = false;
                if (result > 0) ++current.tx_completed;
                else ++current.tx_errors;
            } else if ((uint32_t)(now - tx_started) >= SRNOS_ETHERNET_FRAME_TTL_MS) {
                ++current.tx_errors;
                recover(4); publish(); return;
            }
        }
        if (!tx_busy) {
            for (unsigned i = 0; i < SRNOS_ETHERNET_QUEUE_DEPTH; ++i) {
                if (xQueueReceive(tx_queue, &packet, 0) != pdPASS) break;
                if (packet.epoch != current.link_epoch ||
                    (uint32_t)(now - packet.frame.received_ms) > SRNOS_ETHERNET_FRAME_TTL_MS) {
                    ++current.tx_dropped; continue;
                }
                if (!SRNOS_EthernetBoardSend(packet.frame.data, packet.frame.length)) {
                    ++current.tx_errors;
                    ++current.tx_dropped;
                    recover(5); publish(); return;
                }
                tx_busy = true;
                tx_started = now;
                break;
            }
        }
    }
    publish();
}
static bool wait_event(void *context)
{
    (void)context;
    if (saturated) vTaskDelay(1); /* Bound CPU use under continuous RX pressure. */
    else (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tx_busy ? 10u : 250u));
    return true;
}
static void entry(void *context)
{
    (void)context;
    memcpy(current.mac, configuration.mac, 6);
    unsigned bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= current.mac[i];
    if (!bits) SRNOS_EthernetBoardMAC(current.mac);
    const SRNOS_InternalLoop loop = {step, wait_event, NULL, 0, false};
    SRNOS_KernelRunLoop(&loop);
}
bool SRNOS_EthernetStart(void)
{
    if (!prepared) return false;
    if (!configuration.enabled) return true;
    if (worker) return false;
    tx_queue = xQueueCreateStatic(SRNOS_ETHERNET_QUEUE_DEPTH, sizeof(queued_frame_t), tx_storage, &tx_control);
    rx_queue = xQueueCreateStatic(SRNOS_ETHERNET_QUEUE_DEPTH, sizeof(queued_frame_t), rx_storage, &rx_control);
    if (!tx_queue || !rx_queue) return false;
    /* Below chassis control (24) and every real-time IO worker. HAL startup
     * timeout/MDIO waits cannot starve those tasks when a clock/wire is absent. */
    const osThreadAttr_t attributes = {.name = "SRNOS_Ethernet", .stack_size = 4096,
                                      .priority = osPriorityBelowNormal};
    worker = (TaskHandle_t)SRNOS_KernelThreadNew(entry, NULL, &attributes);
    return worker != NULL;
}
