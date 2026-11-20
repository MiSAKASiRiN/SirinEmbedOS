#include "SirinEmbedOS/Services/RemoteControl/WBus/WBus.h"
#include "../remote_internal.h"
#include <string.h>

bool SRNOS_WBusSerialConfigValid(const SRNOS_SerialConfig *config)
{
    return config && config->mode == SRNOS_SERIAL_DUPLEX && config->rx_enabled &&
        config->baud_rate == 100000 && config->data_bits == 8 && config->stop_bits == 2 &&
        config->parity == SRNOS_SERIAL_PARITY_EVEN;
}

SRNOS_SerialStatus SRNOS_WBusInit(SRNOS_WBus *self, SRNOS_SerialChannel *channel)
{
    if (!self || !channel) return SRNOS_SERIAL_ARGUMENT;
    if (self->channel) return SRNOS_SERIAL_STATE;
    const SRNOS_SerialConfig *cfg = SRNOS_SerialGetConfig(channel);
    if (!SRNOS_WBusSerialConfigValid(cfg)) return SRNOS_SERIAL_ARGUMENT;
    SRNOS_SerialStatus result = SRNOS_SerialClaim(channel, self);
    if (result != SRNOS_SERIAL_OK) return result;
    memset(self, 0, sizeof(*self));
    self->channel = channel;
    return SRNOS_SERIAL_OK;
}

static void reset_partial(SRNOS_WBus *self)
{
    self->framer.discarded_bytes += self->framer.length;
    self->framer.length = 0;
    ++self->diag.stream_resets;
}

static bool refresh_stats(SRNOS_WBus *self)
{
    SRNOS_SerialStats stats;
    if (SRNOS_SerialGetStats(self->channel, &stats) != SRNOS_SERIAL_OK)
        return false;
    const bool continuous = stats.rx_overflow == self->diag.serial.rx_overflow &&
        stats.hardware_errors == self->diag.serial.hardware_errors &&
        stats.recoveries == self->diag.serial.recoveries;
    self->diag.serial = stats;
    return continuous;
}

size_t SRNOS_WBusPoll(SRNOS_WBus *self, SRNOS_WBusDec *latest, uint32_t now_ms)
{
    if (!self || !self->channel || !latest) return 0;
    if (!refresh_stats(self)) {
        reset_partial(self);
        (void)SRNOS_SerialDiscardRx(self->channel);
        return 0;
    }
    if (self->framer.length && now_ms - self->last_byte_ms > 30u)
        reset_partial(self);
    uint8_t bytes[128];
    size_t count = 0;
    SRNOS_WBusDec decoded;
    for (unsigned batch = 0; batch < 8; ++batch) {
        size_t received = 0;
        SRNOS_SerialStatus status = SRNOS_SerialRead(self->channel, bytes,
            sizeof(bytes), &received, SRNOS_NO_WAIT);
        if (!refresh_stats(self) ||
            (status != SRNOS_SERIAL_OK && status != SRNOS_SERIAL_TIMEOUT)) {
            reset_partial(self);
            (void)SRNOS_SerialDiscardRx(self->channel);
            return 0;
        }
        if (status != SRNOS_SERIAL_OK || !received) break;
        self->last_byte_ms = now_ms;
        count += SRNOS_WBusFramerFeed(&self->framer, bytes, received, &decoded);
    }
    if (count) {
        *latest = decoded;
        self->diag.last_frame_ms = now_ms;
        self->diag.seen_frame = true;
    }
    return count;
}
