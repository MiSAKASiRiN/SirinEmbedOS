#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_link.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include <string.h>

enum {
    SLOT_FREE = 0u,
    SLOT_BUILDING = 1u,
    SLOT_QUEUED = 2u,
    SLOT_IN_FLIGHT = 3u,
};

static const uint8_t k_rr_order[] = {
    SRN_LINK_QOS_RELIABLE,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_RELIABLE,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_RELIABLE,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_RELIABLE,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_TELEMETRY,
    SRN_LINK_QOS_LOG,
};

static uint32_t link_now_ms(const srn_link_t *link)
{
    return (link->config.now_ms != NULL) ?
        link->config.now_ms(link->config.user) : 0u;
}

static uint32_t link_cycles(const srn_link_t *link)
{
    return (link->config.cycles != NULL) ?
        link->config.cycles(link->config.user) : 0u;
}

static void link_lock(const srn_link_t *link)
{
    if (link->config.lock != NULL) {
        link->config.lock(link->config.user);
    }
}

static void link_unlock(const srn_link_t *link)
{
    if (link->config.unlock != NULL) {
        link->config.unlock(link->config.user);
    }
}

static void record_cycles(srn_link_t *link, uint32_t start)
{
    if (link->config.cycles == NULL) {
        return;
    }
    srn_link_record_processing_cycles(link, link_cycles(link) - start);
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t request_fingerprint(const srn_cobs_prot_view_t *view)
{
    uint32_t hash = srn_cobs_prot_crc32(view->payload, view->payload_len);
    hash ^= ((uint32_t)view->header.service << 24u);
    hash ^= ((uint32_t)view->header.message << 16u);
    hash ^= ((uint32_t)view->header.flags << 8u);
    hash ^= (uint32_t)view->header.kind;
    return hash;
}

static void sync_depths(srn_link_t *link)
{
    uint32_t depths[SRN_LINK_QOS_COUNT] = {0u};
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        const srn_link_tx_slot_t *slot = &link->slots[i];
        if (((slot->state == SLOT_QUEUED) ||
             (slot->state == SLOT_IN_FLIGHT)) &&
            (slot->qos < SRN_LINK_QOS_COUNT)) {
            depths[slot->qos]++;
        }
    }
    for (size_t qos = 0u; qos < SRN_LINK_QOS_COUNT; ++qos) {
        link->stats.qos[qos].queue_depth = depths[qos];
        if (depths[qos] > link->stats.qos[qos].queue_high_water) {
            link->stats.qos[qos].queue_high_water = depths[qos];
        }
    }
}

static int find_free_slot(const srn_link_t *link)
{
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        if (link->slots[i].state == SLOT_FREE) {
            return (int)i;
        }
    }
    return -1;
}

static int find_coalesce_slot(const srn_link_t *link,
                              srn_link_qos_t qos,
                              uint16_t key,
                              uint8_t service,
                              uint8_t message)
{
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        const srn_link_tx_slot_t *slot = &link->slots[i];
        if ((slot->state == SLOT_QUEUED) &&
            (slot->qos == (uint8_t)qos) && (slot->key == key) &&
            (slot->service == service) && (slot->message == message)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_oldest_slot(const srn_link_t *link, srn_link_qos_t qos)
{
    int found = -1;
    uint32_t oldest_age = 0u;
    const uint32_t now = link_now_ms(link);
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        const srn_link_tx_slot_t *slot = &link->slots[i];
        if ((slot->state != SLOT_QUEUED) || (slot->qos != (uint8_t)qos)) {
            continue;
        }
        const uint32_t age = now - slot->enqueued_ms;
        if ((found < 0) || (age > oldest_age)) {
            found = (int)i;
            oldest_age = age;
        }
    }
    return found;
}

static int reserve_slot(srn_link_t *link,
                        srn_link_qos_t qos,
                        uint16_t key,
                        uint8_t service,
                        uint8_t message,
                        bool *replaced)
{
    int slot_index = -1;
    *replaced = false;
    link_lock(link);
    if ((qos == SRN_LINK_QOS_CONTROL) ||
        (qos == SRN_LINK_QOS_TELEMETRY)) {
        slot_index = find_coalesce_slot(link, qos, key, service, message);
        if (slot_index >= 0) {
            link->stats.qos[qos].coalesced++;
            *replaced = true;
        }
    }
    if (slot_index < 0) {
        slot_index = find_free_slot(link);
    }
    if ((slot_index < 0) &&
        ((qos == SRN_LINK_QOS_CONTROL) ||
         (qos == SRN_LINK_QOS_TELEMETRY))) {
        slot_index = find_oldest_slot(link, qos);
        if (slot_index >= 0) {
            link->stats.qos[qos].drop_old++;
            *replaced = true;
        }
    }
    if ((slot_index < 0) &&
        ((qos == SRN_LINK_QOS_CONTROL) ||
         (qos == SRN_LINK_QOS_RELIABLE))) {
        slot_index = find_oldest_slot(link, SRN_LINK_QOS_LOG);
        if (slot_index >= 0) {
            link->stats.qos[SRN_LINK_QOS_LOG].drop_old++;
            *replaced = true;
        } else {
            slot_index = find_oldest_slot(link, SRN_LINK_QOS_TELEMETRY);
            if (slot_index >= 0) {
                link->stats.qos[SRN_LINK_QOS_TELEMETRY].drop_old++;
                *replaced = true;
            }
        }
    }
    if (slot_index < 0) {
        link->stats.qos[qos].rejected++;
        link->stats.qos[qos].drop_new++;
        link_unlock(link);
        return -1;
    }
    link->slots[slot_index].state = SLOT_BUILDING;
    link_unlock(link);
    return slot_index;
}

static void finish_slot(srn_link_t *link,
                        int slot_index,
                        srn_link_qos_t qos,
                        uint16_t key,
                        uint8_t kind,
                        uint8_t service,
                        uint8_t message,
                        size_t len,
                        bool replaced)
{
    link_lock(link);
    srn_link_tx_slot_t *slot = &link->slots[slot_index];
    slot->len = (uint16_t)len;
    slot->key = key;
    slot->qos = (uint8_t)qos;
    slot->kind = kind;
    slot->service = service;
    slot->message = message;
    slot->enqueued_ms = link_now_ms(link);
    slot->state = SLOT_QUEUED;
    link->stats.qos[qos].submitted++;
    (void)replaced;
    sync_depths(link);
    link_unlock(link);
}

static void cancel_slot(srn_link_t *link, int slot_index)
{
    link_lock(link);
    link->slots[slot_index].state = SLOT_FREE;
    sync_depths(link);
    link_unlock(link);
}

static srn_link_result_t queue_protocol(srn_link_t *link,
                                        srn_link_qos_t qos,
                                        uint16_t key,
                                        uint8_t kind,
                                        uint8_t flags,
                                        uint8_t service,
                                        uint8_t message,
                                        uint32_t sequence,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        const uint8_t **frame_out,
                                        size_t *frame_len_out)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        (qos >= SRN_LINK_QOS_COUNT) ||
        ((payload == NULL) && (payload_len != 0u))) {
        return SRN_LINK_ERR_PARAM;
    }

    bool replaced = false;
    const int slot_index = reserve_slot(link, qos, key, service, message,
                                        &replaced);
    if (slot_index < 0) {
        return SRN_LINK_ERR_FULL;
    }

    const uint32_t start = link_cycles(link);
    srn_link_tx_slot_t *slot = &link->slots[slot_index];
    size_t frame_len = 0u;
    const srn_cobs_prot_status_t status =
        srn_cobs_prot_pack(kind, flags, service, message, sequence,
                           payload, payload_len, slot->data,
                           SRN_COBS_PROT_FRAME_SLOT_SIZE, &frame_len);
    record_cycles(link, start);
    if (status != SRN_COBS_PROT_OK) {
        cancel_slot(link, slot_index);
        return SRN_LINK_ERR_BUFFER;
    }

    if (frame_out != NULL) {
        *frame_out = slot->data;
    }
    if (frame_len_out != NULL) {
        *frame_len_out = frame_len;
    }
    finish_slot(link, slot_index, qos, key, kind, service, message,
                frame_len, replaced);
    return SRN_LINK_OK;
}

static srn_link_result_t queue_encoded_replay(srn_link_t *link,
                                              const uint8_t *frame,
                                              size_t frame_len)
{
    bool replaced = false;
    const int slot_index = reserve_slot(link, SRN_LINK_QOS_RELIABLE, 0u,
                                        0u, 0u, &replaced);
    if (slot_index < 0) {
        return SRN_LINK_ERR_FULL;
    }
    if ((frame == NULL) || (frame_len > SRN_COBS_PROT_FRAME_SLOT_SIZE)) {
        cancel_slot(link, slot_index);
        return SRN_LINK_ERR_PARAM;
    }
    (void)SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(link->slots[slot_index].data,
                               sizeof(link->slots[slot_index].data)),
        SRNOS_Memory_Bytes(frame, frame_len));
    finish_slot(link, slot_index, SRN_LINK_QOS_RELIABLE, 0u,
                SRN_COBS_PROT_KIND_RESPONSE, 0u, 0u, frame_len, replaced);
    return SRN_LINK_OK;
}

static srn_link_request_cache_t *find_cache(srn_link_t *link,
                                             const srn_cobs_prot_view_t *view,
                                             uint32_t fingerprint)
{
    const uint32_t now = link_now_ms(link);
    for (size_t i = 0u; i < SRN_LINK_REQUEST_CACHE_COUNT; ++i) {
        srn_link_request_cache_t *entry = &link->request_cache[i];
        if ((entry->used != 0u) && time_reached(now, entry->expires_ms)) {
            entry->used = 0u;
            entry->pending = 0u;
        }
        if ((entry->used != 0u) &&
            (entry->service == view->header.service) &&
            (entry->message == view->header.message) &&
            (entry->sequence == view->header.sequence)) {
            (void)fingerprint;
            return entry;
        }
    }
    return NULL;
}

static srn_link_request_cache_t *allocate_cache(srn_link_t *link)
{
    const uint32_t now = link_now_ms(link);
    size_t oldest = 0u;
    uint32_t oldest_age = 0u;
    for (size_t i = 0u; i < SRN_LINK_REQUEST_CACHE_COUNT; ++i) {
        srn_link_request_cache_t *entry = &link->request_cache[i];
        if ((entry->used == 0u) || time_reached(now, entry->expires_ms)) {
            return entry;
        }
        const uint32_t age = now - (entry->expires_ms - SRN_LINK_REQUEST_CACHE_TTL_MS);
        if (age >= oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }
    return &link->request_cache[oldest];
}

static void cache_pending(srn_link_t *link,
                          const srn_cobs_prot_view_t *view,
                          uint32_t fingerprint)
{
    srn_link_request_cache_t *entry = allocate_cache(link);
    memset(entry, 0, sizeof(*entry));
    entry->used = 1u;
    entry->pending = 1u;
    entry->service = view->header.service;
    entry->message = view->header.message;
    entry->sequence = view->header.sequence;
    entry->fingerprint = fingerprint;
    entry->expires_ms = link_now_ms(link) + SRN_LINK_REQUEST_CACHE_TTL_MS;
}

static void cache_response(srn_link_t *link,
                           uint8_t service,
                           uint8_t message,
                           uint32_t sequence,
                           uint32_t fingerprint,
                           const uint8_t *frame,
                           size_t frame_len)
{
    srn_link_request_cache_t *entry = NULL;
    for (size_t i = 0u; i < SRN_LINK_REQUEST_CACHE_COUNT; ++i) {
        srn_link_request_cache_t *candidate = &link->request_cache[i];
        if ((candidate->used != 0u) &&
            (candidate->service == service) &&
            (candidate->message == message) &&
            (candidate->sequence == sequence)) {
            entry = candidate;
            break;
        }
    }
    if (entry == NULL) {
        entry = allocate_cache(link);
    }
    memset(entry, 0, sizeof(*entry));
    entry->used = 1u;
    entry->service = service;
    entry->message = message;
    entry->sequence = sequence;
    entry->fingerprint = fingerprint;
    entry->expires_ms = link_now_ms(link) + SRN_LINK_REQUEST_CACHE_TTL_MS;
    entry->frame_len = (uint16_t)frame_len;
    if ((frame != NULL) && (frame_len <= sizeof(entry->frame))) {
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(entry->frame,
                                                     sizeof(entry->frame)),
                             SRNOS_Memory_Bytes(frame, frame_len));
    }
}

static srn_link_result_t send_response_internal(srn_link_t *link,
                                                uint8_t service,
                                                uint8_t message,
                                                uint32_t sequence,
                                                uint16_t status,
                                                uint8_t flags,
                                                const uint8_t *body,
                                                size_t body_len,
                                                uint32_t fingerprint,
                                                bool cache)
{
    if ((body_len + 2u) > SRN_COBS_PROT_MAX_PAYLOAD) {
        return SRN_LINK_ERR_BUFFER;
    }
    (void)SRNOS_Endian_StoreLE16(
        SRNOS_Memory_MutableBytes(link->response_payload,
                               sizeof(link->response_payload)),
        0u, status);
    if ((body != NULL) && (body_len != 0u)) {
        (void)SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(&link->response_payload[2],
                                   sizeof(link->response_payload) - 2u),
            SRNOS_Memory_Bytes(body, body_len));
    }
    const uint8_t *frame = NULL;
    size_t frame_len = 0u;
    const srn_link_result_t result = queue_protocol(
        link, SRN_LINK_QOS_RELIABLE, 0u,
        SRN_COBS_PROT_KIND_RESPONSE, flags, service, message, sequence,
        link->response_payload, body_len + 2u, &frame, &frame_len);
    if ((result == SRN_LINK_OK) && cache) {
        cache_response(link, service, message, sequence, fingerprint,
                       frame, frame_len);
    }
    return result;
}

static srn_link_handler_entry_t *find_handler(srn_link_t *link,
                                               const srn_cobs_prot_view_t *view)
{
    const uint8_t kind_bit = SRN_LINK_KIND_MASK(view->header.kind);
    for (size_t i = 0u; i < SRN_LINK_HANDLER_COUNT; ++i) {
        srn_link_handler_entry_t *entry = &link->handlers[i];
        if ((entry->handler != NULL) &&
            (entry->service == view->header.service) &&
            (entry->message == view->header.message) &&
            ((entry->kind_mask & kind_bit) != 0u)) {
            return entry;
        }
    }
    return NULL;
}

static bool service_is_known(const srn_link_t *link, uint8_t service)
{
    if ((service == SRN_LINK_SERVICE_SYSTEM) ||
        (service == SRN_LINK_SERVICE_CONTROL)) {
        return true;
    }
    for (size_t i = 0u; i < SRN_LINK_HANDLER_COUNT; ++i) {
        if ((link->handlers[i].handler != NULL) &&
            (link->handlers[i].service == service)) {
            return true;
        }
    }
    return false;
}

static srn_link_qos_t rx_qos_for_view(const srn_cobs_prot_view_t *view)
{
    if ((view->header.kind == SRN_COBS_PROT_KIND_EVENT) &&
        (view->header.service == SRN_LINK_SERVICE_CONTROL)) {
        return SRN_LINK_QOS_CONTROL;
    }
    if ((view->header.kind == SRN_COBS_PROT_KIND_EVENT) &&
        ((view->header.service == SRN_LINK_SERVICE_TELEMETRY) ||
         (view->header.service == SRN_LINK_SERVICE_TEST))) {
        return SRN_LINK_QOS_TELEMETRY;
    }
    return SRN_LINK_QOS_RELIABLE;
}

static uint16_t build_qos_payload(const srn_link_t *link,
                                  uint8_t *out,
                                  size_t cap,
                                  size_t *out_len)
{
    const size_t global_words = 24u;
    const size_t class_words = 14u;
    const size_t needed = 4u + (global_words * 4u) +
                          (SRN_LINK_QOS_COUNT * class_words * 4u);
    if ((out == NULL) || (out_len == NULL) || (cap < needed)) {
        return SRN_LINK_STATUS_INTERNAL_ERROR;
    }
    size_t pos = 0u;
#define PUT32(value) do {                                                     \
    (void)SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(out, cap), pos, (value));\
    pos += 4u;                                                               \
} while (0)
    out[pos++] = 1u;
    out[pos++] = SRN_LINK_QOS_COUNT;
    (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(out, cap), pos,
                              (uint16_t)needed);
    pos += 2u;
    srn_link_stats_t snapshot;
    srn_link_get_stats(link, &snapshot);
    const srn_link_stats_t *s = &snapshot;
    PUT32(s->rx_frames); PUT32(s->rx_bytes); PUT32(s->tx_frames); PUT32(s->tx_bytes);
    PUT32(s->cobs_errors); PUT32(s->crc_errors); PUT32(s->length_errors);
    PUT32(s->version_errors); PUT32(s->flags_errors); PUT32(s->kind_errors);
    PUT32(s->unknown_service); PUT32(s->unknown_message); PUT32(s->rx_overflow);
    PUT32(s->sequence_gap); PUT32(s->sequence_duplicate); PUT32(s->sequence_out_of_order);
    PUT32(s->request_replay); PUT32(s->request_conflict); PUT32(s->transport_busy);
    PUT32(s->transport_errors); PUT32(s->transport_disconnects);
    PUT32(s->processing_cycles_total); PUT32(s->processing_cycles_max);
    PUT32(s->transport_connected);
    for (size_t i = 0u; i < SRN_LINK_QOS_COUNT; ++i) {
        const srn_link_qos_stats_t *q = &s->qos[i];
        PUT32(q->rx_frames); PUT32(q->rx_bytes); PUT32(q->tx_frames); PUT32(q->tx_bytes);
        PUT32(q->submitted); PUT32(q->sent); PUT32(q->rejected); PUT32(q->drop_new);
        PUT32(q->drop_old); PUT32(q->coalesced); PUT32(q->queue_depth);
        PUT32(q->queue_high_water); PUT32(q->last_wait_ms); PUT32(q->max_wait_ms);
    }
#undef PUT32
    *out_len = pos;
    return SRN_LINK_STATUS_OK;
}

static uint16_t handle_system(srn_link_t *link,
                              const srn_cobs_prot_view_t *view,
                                              uint8_t *response,
                                              size_t response_cap,
                                              size_t *response_len,
                                              uint8_t *response_flags)
{
    *response_len = 0u;
    *response_flags = 0u;
    switch (view->header.message) {
    case SRN_LINK_SYSTEM_HELLO:
        if (view->payload_len != 0u) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        if (response_cap < 12u) {
            return SRN_LINK_STATUS_INTERNAL_ERROR;
        }
        response[0] = SRN_COBS_PROT_VERSION;
        response[1] = SRN_LINK_QOS_COUNT;
        SRNOS_Memory_MutableByteSpan response_span =
            SRNOS_Memory_MutableBytes(response, response_cap);
        (void)SRNOS_Endian_StoreLE16(response_span, 2u,
                                  SRN_COBS_PROT_MAX_PAYLOAD);
        (void)SRNOS_Endian_StoreLE32(response_span, 4u, 0x0000003Fu);
        response[8] = SRN_LINK_TX_SLOT_COUNT;
        response[9] = SRN_LINK_REQUEST_CACHE_COUNT;
        (void)SRNOS_Endian_StoreLE16(response_span, 10u, 0x000Fu);
        *response_len = 12u;
        return SRN_LINK_STATUS_OK;
    case SRN_LINK_SYSTEM_ECHO:
        if (view->payload_len > response_cap) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        if (view->payload_len != 0u) {
            (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(response, response_cap),
                                 SRNOS_Memory_Bytes(view->payload,
                                                 view->payload_len));
        }
        *response_len = view->payload_len;
        return SRN_LINK_STATUS_OK;
    case SRN_LINK_SYSTEM_QOS_GET:
        if (view->payload_len != 0u) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        return build_qos_payload(link, response, response_cap, response_len);
    case SRN_LINK_SYSTEM_QOS_RESET:
        if (view->payload_len != 0u) {
            return SRN_LINK_STATUS_BAD_LENGTH;
        }
        srn_link_reset_stats(link);
        return SRN_LINK_STATUS_OK;
    default:
        link->stats.unknown_message++;
        return SRN_LINK_STATUS_UNSUPPORTED_MESSAGE;
    }
}

static srn_link_control_mailbox_t *find_control(srn_link_t *link, uint16_t channel)
{
    for (size_t i = 0u; i < SRN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if ((link->control[i].used != 0u) &&
            (link->control[i].channel == channel)) {
            return &link->control[i];
        }
    }
    return NULL;
}

static uint16_t handle_control_event(srn_link_t *link,
                                     const srn_cobs_prot_view_t *view)
{
    if ((view->header.message != SRN_LINK_CONTROL_STATE) ||
        (view->payload_len < 10u)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(view->payload, view->payload_len);
    uint16_t channel = 0u;
    uint32_t generation = 0u;
    uint32_t deadline_us = 0u;
    if (!SRNOS_Endian_LoadLE16(payload, 0u, &channel) ||
        !SRNOS_Endian_LoadLE32(payload, 2u, &generation) ||
        !SRNOS_Endian_LoadLE32(payload, 6u, &deadline_us)) {
        return SRN_LINK_STATUS_BAD_LENGTH;
    }
    const size_t data_len = view->payload_len - 10u;
    srn_link_control_mailbox_t *mailbox = find_control(link, channel);
    if ((mailbox == NULL) || (data_len > mailbox->capacity)) {
        return (mailbox == NULL) ? SRN_LINK_STATUS_NOT_FOUND :
                                   SRN_LINK_STATUS_BAD_LENGTH;
    }

    link_lock(link);
    if ((mailbox->pending != 0u) &&
        ((int32_t)(generation - mailbox->generation) <= 0)) {
        if (generation == mailbox->generation) {
            link->stats.sequence_duplicate++;
        } else {
            link->stats.sequence_out_of_order++;
        }
        link_unlock(link);
        return SRN_LINK_STATUS_OK;
    }
    if (mailbox->pending != 0u) {
        link->stats.qos[SRN_LINK_QOS_CONTROL].coalesced++;
    }
    if (data_len != 0u) {
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(mailbox->storage,
                                                     mailbox->capacity),
                             SRNOS_Memory_Bytes(&view->payload[10], data_len));
    }
    mailbox->length = data_len;
    mailbox->generation = generation;
    mailbox->deadline_us = deadline_us;
    mailbox->received_ms = link_now_ms(link);
    mailbox->pending = 1u;
    link_unlock(link);
    return SRN_LINK_STATUS_OK;
}

static void track_event_sequence(srn_link_t *link,
                                 const srn_cobs_prot_view_t *view)
{
    srn_link_event_track_t *free_entry = NULL;
    for (size_t i = 0u; i < SRN_LINK_EVENT_TRACK_COUNT; ++i) {
        srn_link_event_track_t *entry = &link->event_track[i];
        if (entry->used == 0u) {
            if (free_entry == NULL) {
                free_entry = entry;
            }
            continue;
        }
        if ((entry->service != view->header.service) ||
            (entry->message != view->header.message)) {
            continue;
        }
        const int32_t delta = (int32_t)(view->header.sequence - entry->last_sequence);
        if (delta == 0) {
            link->stats.sequence_duplicate++;
        } else if (delta < 0) {
            link->stats.sequence_out_of_order++;
        } else {
            if (delta > 1) {
                link->stats.sequence_gap += (uint32_t)(delta - 1);
            }
            entry->last_sequence = view->header.sequence;
        }
        return;
    }
    if (free_entry != NULL) {
        free_entry->used = 1u;
        free_entry->service = view->header.service;
        free_entry->message = view->header.message;
        free_entry->last_sequence = view->header.sequence;
    }
}

static void record_parse_error(srn_link_t *link, srn_cobs_prot_status_t status)
{
    switch (status) {
    case SRN_COBS_PROT_ERR_CRC: link->stats.crc_errors++; break;
    case SRN_COBS_PROT_ERR_VERSION: link->stats.version_errors++; break;
    case SRN_COBS_PROT_ERR_FLAGS: link->stats.flags_errors++; break;
    case SRN_COBS_PROT_ERR_KIND: link->stats.kind_errors++; break;
    case SRN_COBS_PROT_ERR_LEN:
    case SRN_COBS_PROT_ERR_RAW_LEN: link->stats.length_errors++; break;
    default: link->stats.cobs_errors++; break;
    }
}

static srn_cobs_status_t on_raw_frame(void *user,
                                      const uint8_t *raw,
                                      size_t raw_len)
{
    srn_link_t *link = (srn_link_t *)user;
    srn_cobs_prot_view_t view;
    const srn_cobs_prot_status_t parse_status =
        srn_cobs_prot_parse(raw, raw_len, &view);
    if (parse_status != SRN_COBS_PROT_OK) {
        record_parse_error(link, parse_status);
        return SRN_COBS_ERR_DECODE;
    }

    link->stats.rx_frames++;
    const srn_link_qos_t rx_qos = rx_qos_for_view(&view);
    link->stats.qos[rx_qos].rx_frames++;
    link->stats.qos[rx_qos].rx_bytes += (uint32_t)raw_len;
    if (view.header.kind == SRN_COBS_PROT_KIND_EVENT) {
        track_event_sequence(link, &view);
        if (view.header.service == SRN_LINK_SERVICE_CONTROL) {
            const uint16_t control_status = handle_control_event(link, &view);
            if (control_status == SRN_LINK_STATUS_NOT_FOUND) {
                link->stats.unknown_service++;
            } else if (control_status != SRN_LINK_STATUS_OK) {
                link->stats.unknown_message++;
            }
            return SRN_COBS_OK;
        }
    }

    uint32_t fingerprint = 0u;
    if (view.header.kind == SRN_COBS_PROT_KIND_REQUEST) {
        fingerprint = request_fingerprint(&view);
        srn_link_request_cache_t *cached = find_cache(link, &view, fingerprint);
        if (cached != NULL) {
            if (cached->fingerprint != fingerprint) {
                link->stats.request_conflict++;
                (void)send_response_internal(link, view.header.service,
                                             view.header.message,
                                             view.header.sequence,
                                             SRN_LINK_STATUS_CONFLICT,
                                             0u, NULL, 0u, fingerprint, false);
            } else if (cached->pending != 0u) {
                link->stats.request_replay++;
                (void)send_response_internal(link, view.header.service,
                                             view.header.message,
                                             view.header.sequence,
                                             SRN_LINK_STATUS_ACCEPTED,
                                             0u, NULL, 0u, fingerprint, false);
            } else {
                link->stats.request_replay++;
                (void)queue_encoded_replay(link, cached->frame,
                                           cached->frame_len);
            }
            return SRN_COBS_OK;
        }
    }

    size_t response_len = 0u;
    uint8_t response_flags = 0u;
    uint16_t status = SRN_LINK_STATUS_UNSUPPORTED_SERVICE;
    if (view.header.service == SRN_LINK_SERVICE_SYSTEM) {
        if (view.header.kind != SRN_COBS_PROT_KIND_REQUEST) {
            status = SRN_LINK_STATUS_BAD_KIND;
        } else {
            status = handle_system(link, &view, &link->response_payload[2],
                                   sizeof(link->response_payload) - 2u,
                                   &response_len, &response_flags);
        }
    } else {
        srn_link_handler_entry_t *handler = find_handler(link, &view);
        if (handler == NULL) {
            if (service_is_known(link, view.header.service)) {
                link->stats.unknown_message++;
                status = SRN_LINK_STATUS_UNSUPPORTED_MESSAGE;
            } else {
                link->stats.unknown_service++;
                status = SRN_LINK_STATUS_UNSUPPORTED_SERVICE;
            }
        } else {
            status = handler->handler(handler->user, &view,
                                      &link->response_payload[2],
                                      sizeof(link->response_payload) - 2u,
                                      &response_len, &response_flags);
        }
    }

    if (view.header.kind == SRN_COBS_PROT_KIND_REQUEST) {
        if (status == SRN_LINK_STATUS_DEFERRED_INTERNAL) {
            cache_pending(link, &view, fingerprint);
        } else {
            (void)send_response_internal(link, view.header.service,
                                         view.header.message,
                                         view.header.sequence, status,
                                         response_flags,
                                         &link->response_payload[2], response_len,
                                         fingerprint, true);
        }
    }
    return SRN_COBS_OK;
}

srn_link_result_t srn_link_init(srn_link_t *link,
                                const srn_link_config_t *config)
{
    if ((link == NULL) || (config == NULL) ||
        (config->tx_submit == NULL) ||
        (config->tx_storage == NULL) ||
        (config->tx_storage_size <
         (SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE)) ||
        ((config->lock == NULL) != (config->unlock == NULL))) {
        return SRN_LINK_ERR_PARAM;
    }
    memset(link, 0, sizeof(*link));
    link->config = *config;
    link->in_flight = -1;
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        link->slots[i].data = &config->tx_storage[
            i * SRN_COBS_PROT_FRAME_SLOT_SIZE];
    }
    if (srn_cobs_stream_init(&link->rx_stream, link->rx_raw,
                             sizeof(link->rx_raw), on_raw_frame, link) !=
        SRN_COBS_OK) {
        return SRN_LINK_ERR_PARAM;
    }
    link->initialized = 1u;
    return SRN_LINK_OK;
}

void srn_link_reset_rx(srn_link_t *link)
{
    if (link != NULL) {
        srn_cobs_stream_reset(&link->rx_stream);
    }
}

srn_link_result_t srn_link_feed_rx(srn_link_t *link,
                                   const uint8_t *data,
                                   size_t len)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        ((data == NULL) && (len != 0u))) {
        return SRN_LINK_ERR_PARAM;
    }
    const uint32_t start = link_cycles(link);
    link->stats.rx_bytes += (uint32_t)len;
    const uint32_t old_decode = link->rx_stream.stat_decode_error;
    const uint32_t old_long = link->rx_stream.stat_too_long;
    const srn_cobs_status_t status = srn_cobs_stream_feed(&link->rx_stream,
                                                         data, len);
    link->stats.cobs_errors += link->rx_stream.stat_decode_error - old_decode;
    link->stats.rx_overflow += link->rx_stream.stat_too_long - old_long;
    record_cycles(link, start);
    return (status == SRN_COBS_OK) ? SRN_LINK_OK : SRN_LINK_ERR_PARAM;
}

srn_link_result_t srn_link_register_handler(srn_link_t *link,
                                            uint8_t service,
                                            uint8_t message,
                                            uint8_t kind_mask,
                                            srn_link_qos_t qos,
                                            srn_link_handler_fn handler,
                                            void *user)
{
    if ((link == NULL) || (handler == NULL) || (kind_mask == 0u) ||
        (qos >= SRN_LINK_QOS_COUNT)) {
        return SRN_LINK_ERR_PARAM;
    }
    for (size_t i = 0u; i < SRN_LINK_HANDLER_COUNT; ++i) {
        srn_link_handler_entry_t *entry = &link->handlers[i];
        if (entry->handler == NULL) {
            entry->service = service;
            entry->message = message;
            entry->kind_mask = kind_mask;
            entry->qos = (uint8_t)qos;
            entry->handler = handler;
            entry->user = user;
            return SRN_LINK_OK;
        }
    }
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t srn_link_register_tx_topic(srn_link_t *link,
                                             uint8_t service,
                                             uint8_t message,
                                             srn_link_qos_t qos)
{
    if ((link == NULL) || (qos >= SRN_LINK_QOS_LOG)) {
        return SRN_LINK_ERR_PARAM;
    }
    for (size_t i = 0u; i < SRN_LINK_TX_TOPIC_COUNT; ++i) {
        srn_link_tx_topic_t *topic = &link->tx_topics[i];
        if ((topic->used != 0u) && (topic->service == service) &&
            (topic->message == message)) {
            topic->qos = (uint8_t)qos;
            return SRN_LINK_OK;
        }
        if (topic->used == 0u) {
            topic->used = 1u;
            topic->service = service;
            topic->message = message;
            topic->qos = (uint8_t)qos;
            return SRN_LINK_OK;
        }
    }
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t srn_link_publish(srn_link_t *link,
                                   uint8_t service,
                                   uint8_t message,
                                   uint16_t key,
                                   uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_len)
{
    if (link == NULL) {
        return SRN_LINK_ERR_PARAM;
    }
    for (size_t i = 0u; i < SRN_LINK_TX_TOPIC_COUNT; ++i) {
        const srn_link_tx_topic_t *topic = &link->tx_topics[i];
        if ((topic->used != 0u) && (topic->service == service) &&
            (topic->message == message)) {
            return queue_protocol(link, (srn_link_qos_t)topic->qos, key,
                                  SRN_COBS_PROT_KIND_EVENT, 0u,
                                  service, message, sequence,
                                  payload, payload_len, NULL, NULL);
        }
    }
    return SRN_LINK_ERR_NOT_FOUND;
}

size_t srn_link_discard_topic(srn_link_t *link,
                              uint8_t service,
                              uint8_t message)
{
    if (link == NULL) {
        return 0u;
    }
    size_t discarded = 0u;
    link_lock(link);
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        srn_link_tx_slot_t *slot = &link->slots[i];
        if ((slot->state == SLOT_QUEUED) &&
            (slot->kind == SRN_COBS_PROT_KIND_EVENT) &&
            (slot->service == service) && (slot->message == message)) {
            if (slot->qos < SRN_LINK_QOS_COUNT) {
                link->stats.qos[slot->qos].drop_old++;
            }
            slot->state = SLOT_FREE;
            discarded++;
        }
    }
    sync_depths(link);
    link_unlock(link);
    return discarded;
}

srn_link_result_t srn_link_submit_log(srn_link_t *link,
                                      const uint8_t *line,
                                      size_t len)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        (line == NULL) || (len == 0u) ||
        (len > SRN_COBS_PROT_FRAME_SLOT_SIZE)) {
        return SRN_LINK_ERR_PARAM;
    }
    bool replaced = false;
    const int slot_index = reserve_slot(link, SRN_LINK_QOS_LOG, 0u,
                                        0u, 0u, &replaced);
    if (slot_index < 0) {
        return SRN_LINK_ERR_FULL;
    }
    (void)SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(link->slots[slot_index].data,
                               sizeof(link->slots[slot_index].data)),
        SRNOS_Memory_Bytes(line, len));
    finish_slot(link, slot_index, SRN_LINK_QOS_LOG, 0u, 0u, 0u, 0u,
                len, replaced);
    return SRN_LINK_OK;
}

srn_link_result_t srn_link_send_response(srn_link_t *link,
                                         uint8_t service,
                                         uint8_t message,
                                         uint32_t sequence,
                                         uint16_t status,
                                         uint8_t flags,
                                         const uint8_t *body,
                                         size_t body_len)
{
    if (link == NULL) {
        return SRN_LINK_ERR_PARAM;
    }
    uint32_t fingerprint = 0u;
    bool cache = false;
    for (size_t i = 0u; i < SRN_LINK_REQUEST_CACHE_COUNT; ++i) {
        srn_link_request_cache_t *entry = &link->request_cache[i];
        if ((entry->used != 0u) && (entry->pending != 0u) &&
            (entry->service == service) && (entry->message == message) &&
            (entry->sequence == sequence)) {
            fingerprint = entry->fingerprint;
            cache = true;
            break;
        }
    }
    return send_response_internal(link, service, message, sequence, status, flags,
                                  body, body_len, fingerprint, cache);
}

static int select_tx_slot(srn_link_t *link)
{
    const uint32_t now = link_now_ms(link);
    int q0 = find_oldest_slot(link, SRN_LINK_QOS_CONTROL);
    const int q1 = find_oldest_slot(link, SRN_LINK_QOS_RELIABLE);
    if ((q1 >= 0) && ((now - link->slots[q1].enqueued_ms) >= SRN_LINK_Q1_FORCE_MS) &&
        (link->q0_burst != 0u)) {
        return q1;
    }
    if (q0 >= 0) {
        return q0;
    }
    for (size_t n = 0u; n < sizeof(k_rr_order); ++n) {
        const uint8_t qos = k_rr_order[link->rr_cursor];
        link->rr_cursor = (uint8_t)((link->rr_cursor + 1u) % sizeof(k_rr_order));
        const int selected = find_oldest_slot(link, (srn_link_qos_t)qos);
        if (selected >= 0) {
            return selected;
        }
    }
    return -1;
}

srn_link_result_t srn_link_service_tx(srn_link_t *link)
{
    if ((link == NULL) || (link->initialized == 0u)) {
        return SRN_LINK_ERR_PARAM;
    }
    if (link->in_flight >= 0) {
        return SRN_LINK_ERR_BUSY;
    }

    link_lock(link);
    const int slot_index = select_tx_slot(link);
    if (slot_index < 0) {
        link_unlock(link);
        return SRN_LINK_OK;
    }
    srn_link_tx_slot_t *slot = &link->slots[slot_index];
    slot->state = SLOT_IN_FLIGHT;
    link->in_flight = (int8_t)slot_index;
    const uint32_t wait_ms = link_now_ms(link) - slot->enqueued_ms;
    link->stats.qos[slot->qos].last_wait_ms = wait_ms;
    if (wait_ms > link->stats.qos[slot->qos].max_wait_ms) {
        link->stats.qos[slot->qos].max_wait_ms = wait_ms;
    }
    sync_depths(link);
    link_unlock(link);

    const srn_link_tx_result_t tx_status =
        link->config.tx_submit(link->config.user, slot->data, slot->len);
    if (tx_status == SRN_LINK_TX_ACCEPTED) {
        link->stats.transport_connected = 1u;
        if (slot->qos == SRN_LINK_QOS_CONTROL) {
            link->q0_burst++;
        } else {
            link->q0_burst = 0u;
        }
        return SRN_LINK_OK;
    }

    link_lock(link);
    slot->state = SLOT_QUEUED;
    link->in_flight = -1;
    sync_depths(link);
    link_unlock(link);
    if (tx_status == SRN_LINK_TX_BUSY) {
        link->stats.transport_busy++;
        return SRN_LINK_ERR_BUSY;
    }
    if (tx_status == SRN_LINK_TX_DISCONNECTED) {
        link->stats.transport_connected = 0u;
        link->stats.transport_disconnects++;
    } else {
        link->stats.transport_errors++;
    }
    return SRN_LINK_ERR_TRANSPORT;
}

void srn_link_on_tx_complete(srn_link_t *link, bool success)
{
    if ((link == NULL) || (link->in_flight < 0)) {
        return;
    }
    link_lock(link);
    srn_link_tx_slot_t *slot = &link->slots[(size_t)link->in_flight];
    if (success) {
        link->stats.tx_frames++;
        link->stats.tx_bytes += slot->len;
        link->stats.qos[slot->qos].tx_frames++;
        link->stats.qos[slot->qos].tx_bytes += slot->len;
        link->stats.qos[slot->qos].sent++;
        slot->state = SLOT_FREE;
    } else if (slot->qos == SRN_LINK_QOS_RELIABLE) {
        link->stats.transport_errors++;
        slot->state = SLOT_QUEUED;
        slot->enqueued_ms = link_now_ms(link);
    } else {
        link->stats.transport_errors++;
        link->stats.qos[slot->qos].drop_new++;
        slot->state = SLOT_FREE;
    }
    link->in_flight = -1;
    sync_depths(link);
    link_unlock(link);
}

bool srn_link_has_pending_tx(const srn_link_t *link)
{
    if (link == NULL) {
        return false;
    }
    if (link->in_flight >= 0) {
        return true;
    }
    for (size_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        if (link->slots[i].state == SLOT_QUEUED) {
            return true;
        }
    }
    return false;
}

srn_link_result_t srn_link_control_register(srn_link_t *link,
                                            uint16_t channel,
                                            uint8_t *storage,
                                            size_t capacity)
{
    if ((link == NULL) || (storage == NULL) || (capacity == 0u) ||
        (capacity > (SRN_COBS_PROT_MAX_PAYLOAD - 10u))) {
        return SRN_LINK_ERR_PARAM;
    }
    link_lock(link);
    for (size_t i = 0u; i < SRN_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        srn_link_control_mailbox_t *mailbox = &link->control[i];
        if ((mailbox->used != 0u) && (mailbox->channel == channel)) {
            link_unlock(link);
            return SRN_LINK_ERR_PARAM;
        }
        if (mailbox->used == 0u) {
            memset(mailbox, 0, sizeof(*mailbox));
            mailbox->used = 1u;
            mailbox->channel = channel;
            mailbox->storage = storage;
            mailbox->capacity = capacity;
            link_unlock(link);
            return SRN_LINK_OK;
        }
    }
    link_unlock(link);
    return SRN_LINK_ERR_FULL;
}

srn_link_result_t srn_link_control_take(srn_link_t *link,
                                        uint16_t channel,
                                        uint8_t *out,
                                        size_t out_cap,
                                        size_t *out_len,
                                        uint32_t *generation,
                                        uint32_t *deadline_us,
                                        uint32_t *received_ms)
{
    if ((link == NULL) || (out == NULL) || (out_len == NULL)) {
        return SRN_LINK_ERR_PARAM;
    }
    srn_link_control_mailbox_t *mailbox = find_control(link, channel);
    if (mailbox == NULL) {
        return SRN_LINK_ERR_NOT_FOUND;
    }
    link_lock(link);
    if (mailbox->pending == 0u) {
        link_unlock(link);
        return SRN_LINK_ERR_NOT_FOUND;
    }
    if (out_cap < mailbox->length) {
        link_unlock(link);
        return SRN_LINK_ERR_BUFFER;
    }
    (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(out, out_cap),
                         SRNOS_Memory_Bytes(mailbox->storage, mailbox->length));
    *out_len = mailbox->length;
    if (generation != NULL) { *generation = mailbox->generation; }
    if (deadline_us != NULL) { *deadline_us = mailbox->deadline_us; }
    if (received_ms != NULL) { *received_ms = mailbox->received_ms; }
    mailbox->pending = 0u;
    link_unlock(link);
    return SRN_LINK_OK;
}

void srn_link_get_stats(const srn_link_t *link, srn_link_stats_t *out)
{
    if ((link != NULL) && (out != NULL)) {
        link_lock(link);
        *out = link->stats;
        link_unlock(link);
    }
}

void srn_link_reset_stats(srn_link_t *link)
{
    if (link == NULL) {
        return;
    }
    link_lock(link);
    const uint32_t transport_connected = link->stats.transport_connected;
    memset(&link->stats, 0, sizeof(link->stats));
    link->stats.transport_connected = transport_connected;
    sync_depths(link);
    for (size_t i = 0u; i < SRN_LINK_QOS_COUNT; ++i) {
        link->stats.qos[i].queue_high_water =
            link->stats.qos[i].queue_depth;
    }
    link_unlock(link);
}

void srn_link_record_processing_cycles(srn_link_t *link, uint32_t cycles)
{
    if ((link == NULL) || (cycles == 0u)) {
        return;
    }
    link_lock(link);
    link->stats.processing_cycles_total += cycles;
    if (cycles > link->stats.processing_cycles_max) {
        link->stats.processing_cycles_max = cycles;
    }
    link_unlock(link);
}

void srn_link_record_rx_overflow(srn_link_t *link, uint32_t bytes)
{
    if (link != NULL) {
        link->stats.rx_overflow += bytes;
    }
}

void srn_link_set_transport_connected(srn_link_t *link, bool connected)
{
    if (link != NULL) {
        link_lock(link);
        if ((link->stats.transport_connected != 0u) && !connected) {
            link->stats.transport_disconnects++;
        }
        link->stats.transport_connected = connected ? 1u : 0u;
        link_unlock(link);
    }
}
