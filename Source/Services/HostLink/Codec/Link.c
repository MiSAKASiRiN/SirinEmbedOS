#include "link_internal.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include <string.h>

enum {
    SRNOS_HOST_LINK_SLOT_FREE = 0u,
    SRNOS_HOST_LINK_SLOT_BUILDING = 1u,
    SRNOS_HOST_LINK_SLOT_QUEUED = 2u,
    SRNOS_HOST_LINK_SLOT_IN_FLIGHT = 3u,
    SRNOS_HOST_LINK_SLOT_DELIVERY_PENDING = 4u,
};

static const uint8_t k_rr_order[] = {
    SRNOS_HOST_LINK_QOS_RELIABLE,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_RELIABLE,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_RELIABLE,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_RELIABLE,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_TELEMETRY,
    SRNOS_HOST_LINK_QOS_LOG,
};

static uint32_t link_now_ms(const SRNOS_HostLinkContext *link)
{
    return (link->config.now_ms != NULL) ?
        link->config.now_ms(link->config.user) : 0u;
}

static uint32_t link_cycles(const SRNOS_HostLinkContext *link)
{
    return (link->config.cycles != NULL) ?
        link->config.cycles(link->config.user) : 0u;
}

static void link_lock(const SRNOS_HostLinkContext *link)
{
    if (link->config.lock != NULL) {
        link->config.lock(link->config.user);
    }
}

static void link_unlock(const SRNOS_HostLinkContext *link)
{
    if (link->config.unlock != NULL) {
        link->config.unlock(link->config.user);
    }
}

static void record_cycles(SRNOS_HostLinkContext *link, uint32_t start)
{
    if (link->config.cycles == NULL) {
        return;
    }
    SRNOS_HostLinkCoreRecordProcessingCycles(link, link_cycles(link) - start);
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t request_fingerprint(const SRNOS_ProtocolView *view)
{
    uint32_t hash = SRNOS_ProtocolCRC32(view->payload, view->payload_len);
    hash ^= ((uint32_t)view->header.service << 24u);
    hash ^= ((uint32_t)view->header.message << 16u);
    hash ^= ((uint32_t)view->header.flags << 8u);
    hash ^= (uint32_t)view->header.kind;
    return hash;
}

static void sync_depths(SRNOS_HostLinkContext *link)
{
    uint32_t depths[SRNOS_HOST_LINK_QOS_COUNT] = {0u};
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        const SRNOS_HostLinkCoreTxSlot *slot = &link->slots[i];
        if (((slot->state == SRNOS_HOST_LINK_SLOT_QUEUED) ||
             (slot->state == SRNOS_HOST_LINK_SLOT_IN_FLIGHT)) &&
            (slot->qos < SRNOS_HOST_LINK_QOS_COUNT)) {
            depths[slot->qos]++;
        }
    }
    for (size_t qos = 0u; qos < SRNOS_HOST_LINK_QOS_COUNT; ++qos) {
        link->stats.qos[qos].queue_depth = depths[qos];
        if (depths[qos] > link->stats.qos[qos].queue_high_water) {
            link->stats.qos[qos].queue_high_water = depths[qos];
        }
    }
}

static int find_free_slot(const SRNOS_HostLinkContext *link)
{
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        if (link->slots[i].state == SRNOS_HOST_LINK_SLOT_FREE) {
            return (int)i;
        }
    }
    return -1;
}

static int find_coalesce_slot(const SRNOS_HostLinkContext *link,
                              SRNOS_HostLinkQos qos,
                              uint16_t key,
                              uint8_t service,
                              uint8_t message)
{
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        const SRNOS_HostLinkCoreTxSlot *slot = &link->slots[i];
        if ((slot->state == SRNOS_HOST_LINK_SLOT_QUEUED) && (slot->policy == SRNOS_HOST_LINK_LATEST) &&
            (slot->qos == (uint8_t)qos) && (slot->key == key) &&
            (slot->service == service) && (slot->message == message)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_oldest_slot(const SRNOS_HostLinkContext *link, SRNOS_HostLinkQos qos, bool evict)
{
    int found = -1;
    uint32_t oldest_age = 0u;
    const uint32_t now = link->enqueue_order;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        const SRNOS_HostLinkCoreTxSlot *slot = &link->slots[i];
        if ((slot->state != SRNOS_HOST_LINK_SLOT_QUEUED) || (slot->qos != (uint8_t)qos) ||
            (evict && slot->policy == SRNOS_HOST_LINK_FIFO)) {
            continue;
        }
        const uint32_t age = now - slot->order;
        if ((found < 0) || (age > oldest_age)) {
            found = (int)i;
            oldest_age = age;
        }
    }
    return found;
}

static int reserve_slot(SRNOS_HostLinkContext *link,
                        SRNOS_HostLinkQos qos,
                        uint16_t key,
                        uint8_t service,
                        uint8_t message,
                        bool *replaced, SRNOS_HostLinkPolicy policy)
{
    int slot_index = -1;
    *replaced = false;
    link_lock(link);
    if (policy == SRNOS_HOST_LINK_FIFO) {
        unsigned count = 0;
        for (size_t i=0; i<SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i)
            count += link->slots[i].state != SRNOS_HOST_LINK_SLOT_FREE && link->slots[i].policy == SRNOS_HOST_LINK_FIFO;
        if (count >= SRNOS_HOST_LINK_FIFO_SLOT_LIMIT) { link_unlock(link); return -1; }
    }
    if (policy == SRNOS_HOST_LINK_LATEST && ((qos == SRNOS_HOST_LINK_QOS_CONTROL) ||
        (qos == SRNOS_HOST_LINK_QOS_TELEMETRY))) {
        slot_index = find_coalesce_slot(link, qos, key, service, message);
        if (slot_index >= 0) {
            link->stats.qos[qos].coalesced++;
            *replaced = true;
        }
    }
    if (slot_index < 0) {
        slot_index = find_free_slot(link);
    }
    if ((slot_index < 0) && policy == SRNOS_HOST_LINK_LATEST &&
        ((qos == SRNOS_HOST_LINK_QOS_CONTROL) ||
         (qos == SRNOS_HOST_LINK_QOS_TELEMETRY))) {
        slot_index = find_oldest_slot(link, qos, true);
        if (slot_index >= 0) {
            link->stats.qos[qos].drop_old++;
            *replaced = true;
        }
    }
    if ((slot_index < 0) && policy == SRNOS_HOST_LINK_LATEST &&
        ((qos == SRNOS_HOST_LINK_QOS_CONTROL) ||
         (qos == SRNOS_HOST_LINK_QOS_RELIABLE))) {
        slot_index = find_oldest_slot(link, SRNOS_HOST_LINK_QOS_LOG, true);
        if (slot_index >= 0) {
            link->stats.qos[SRNOS_HOST_LINK_QOS_LOG].drop_old++;
            *replaced = true;
        } else {
            slot_index = find_oldest_slot(link, SRNOS_HOST_LINK_QOS_TELEMETRY, true);
            if (slot_index >= 0) {
                link->stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].drop_old++;
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
    link->slots[slot_index].state = SRNOS_HOST_LINK_SLOT_BUILDING;
    link->slots[slot_index].policy = (uint8_t)policy;
    link->slots[slot_index].session = link->session;
    link->slots[slot_index].cancelled = 0;
    link->slots[slot_index].on_delivery = NULL;
    link_unlock(link);
    return slot_index;
}

static bool finish_slot(SRNOS_HostLinkContext *link,
                        int slot_index,
                        SRNOS_HostLinkQos qos,
                        uint16_t key,
                        uint8_t kind,
                        uint8_t service,
                        uint8_t message,
                        size_t len,
                        bool replaced)
{
    link_lock(link);
    SRNOS_HostLinkCoreTxSlot *slot = &link->slots[slot_index];
    if(slot->session != link->session) {
        slot->state=SRNOS_HOST_LINK_SLOT_FREE; slot->on_delivery=NULL;
        sync_depths(link); link_unlock(link); return false;
    }
    slot->len = (uint16_t)len;
    slot->key = key;
    slot->qos = (uint8_t)qos;
    slot->kind = kind;
    slot->service = service;
    slot->message = message;
    slot->enqueued_ms = link_now_ms(link);
    slot->order = ++link->enqueue_order;
    slot->state = SRNOS_HOST_LINK_SLOT_QUEUED;
    link->stats.qos[qos].submitted++;
    (void)replaced;
    sync_depths(link);
    link_unlock(link);
    return true;
}

static void cancel_slot(SRNOS_HostLinkContext *link, int slot_index)
{
    link_lock(link);
    link->slots[slot_index].state = SRNOS_HOST_LINK_SLOT_FREE;
    sync_depths(link);
    link_unlock(link);
}

static SRNOS_HostLinkResult queue_protocol(SRNOS_HostLinkContext *link,
                                        SRNOS_HostLinkQos qos,
                                        uint16_t key,
                                        uint8_t kind,
                                        uint8_t flags,
                                        uint8_t service,
                                        uint8_t message,
                                        uint32_t sequence,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        const uint8_t **frame_out,
                                        size_t *frame_len_out,
                                        SRNOS_HostLinkPolicy policy,
                                        SRNOS_HostLinkDeliveryFn done, void *done_user, uint32_t tag)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        (qos >= SRNOS_HOST_LINK_QOS_COUNT) ||
        ((payload == NULL) && (payload_len != 0u))) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }

    bool replaced = false;
    const int slot_index = reserve_slot(link, qos, key, service, message,
                                        &replaced, policy);
    if (slot_index < 0) {
        return SRNOS_HOST_LINK_ERR_FULL;
    }

    const uint32_t start = link_cycles(link);
    SRNOS_HostLinkCoreTxSlot *slot = &link->slots[slot_index];
    size_t frame_len = 0u;
    const SRNOS_ProtocolStatus status =
        SRNOS_ProtocolPack(kind, flags, service, message, sequence,
                           payload, payload_len, slot->data,
                           SRNOS_PROTOCOL_FRAME_SLOT_SIZE, &frame_len);
    record_cycles(link, start);
    if (status != SRNOS_PROTOCOL_OK) {
        cancel_slot(link, slot_index);
        return SRNOS_HOST_LINK_ERR_BUFFER;
    }

    if (frame_out != NULL) {
        *frame_out = slot->data;
    }
    if (frame_len_out != NULL) {
        *frame_len_out = frame_len;
    }
    slot->on_delivery = done; slot->delivery_user = done_user; slot->tag = tag;
    if(!finish_slot(link, slot_index, qos, key, kind, service, message,
                frame_len, replaced)) return SRNOS_HOST_LINK_ERR_BUSY;
    return SRNOS_HOST_LINK_OK;
}

static SRNOS_HostLinkResult queue_encoded_replay(SRNOS_HostLinkContext *link,
                                              const uint8_t *frame,
                                              size_t frame_len)
{
    bool replaced = false;
    const int slot_index = reserve_slot(link, SRNOS_HOST_LINK_QOS_RELIABLE, 0u,
                                        0u, 0u, &replaced, SRNOS_HOST_LINK_LATEST);
    if (slot_index < 0) {
        return SRNOS_HOST_LINK_ERR_FULL;
    }
    if ((frame == NULL) || (frame_len > SRNOS_PROTOCOL_FRAME_SLOT_SIZE)) {
        cancel_slot(link, slot_index);
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    if (!SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(link->slots[slot_index].data,
                               SRNOS_PROTOCOL_FRAME_SLOT_SIZE),
        SRNOS_Memory_Bytes(frame, frame_len))) { cancel_slot(link,slot_index); return SRNOS_HOST_LINK_ERR_BUFFER; }
    if(!finish_slot(link, slot_index, SRNOS_HOST_LINK_QOS_RELIABLE, 0u,
                SRNOS_PROTOCOL_KIND_RESPONSE, 0u, 0u, frame_len, replaced)) return SRNOS_HOST_LINK_ERR_BUSY;
    return SRNOS_HOST_LINK_OK;
}

static SRNOS_HostLinkCoreRequestCache *find_cache(SRNOS_HostLinkContext *link,
                                             const SRNOS_ProtocolView *view,
                                             uint32_t fingerprint)
{
    const uint32_t now = link_now_ms(link);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_REQUEST_CACHE_COUNT; ++i) {
        SRNOS_HostLinkCoreRequestCache *entry = &link->request_cache[i];
        if ((entry->used != 0u) && time_reached(now, entry->expires_ms)) {
            entry->used = 0u;
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

static SRNOS_HostLinkCoreRequestCache *allocate_cache(SRNOS_HostLinkContext *link)
{
    const uint32_t now = link_now_ms(link);
    size_t oldest = 0u;
    uint32_t oldest_age = 0u;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_REQUEST_CACHE_COUNT; ++i) {
        SRNOS_HostLinkCoreRequestCache *entry = &link->request_cache[i];
        if ((entry->used == 0u) || time_reached(now, entry->expires_ms)) {
            return entry;
        }
        const uint32_t age = now - (entry->expires_ms - SRNOS_HOST_LINK_REQUEST_CACHE_TTL_MS);
        if (age >= oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }
    return &link->request_cache[oldest];
}

static SRNOS_HostLinkActiveRequest *find_active(SRNOS_HostLinkContext *link,
    uint8_t service, uint8_t message, uint32_t sequence)
{
    for (unsigned i=0;i<SRNOS_HOST_LINK_ACTIVE_REQUEST_COUNT;i++) {
        SRNOS_HostLinkActiveRequest *entry=&link->active_requests[i];
        if(entry->used && entry->service==service && entry->message==message && entry->sequence==sequence)
            return entry;
    }
    return NULL;
}
bool SRNOS_HostLinkCoreCanDefer(SRNOS_HostLinkContext *link,uint8_t service,uint8_t message,uint32_t sequence)
{
    const SRNOS_HostLinkActiveRequest *request=link?link->dispatching_request:NULL;
    return request && request->used && !request->deferred_submitted && request->service==service &&
        request->message==message && request->sequence==sequence;
}
void SRNOS_HostLinkCoreMarkDeferred(SRNOS_HostLinkContext *link)
{ if(link && link->dispatching_request)link->dispatching_request->deferred_submitted=1; }
static SRNOS_HostLinkActiveRequest *reserve_active(SRNOS_HostLinkContext *link,
    const SRNOS_ProtocolView *request,uint32_t fingerprint)
{
    for (unsigned i=0;i<SRNOS_HOST_LINK_ACTIVE_REQUEST_COUNT;i++) {
        SRNOS_HostLinkActiveRequest *entry=&link->active_requests[i];
        if(!entry->used) {
            *entry=(SRNOS_HostLinkActiveRequest){.used=1,.service=request->header.service,
                .message=request->header.message,.sequence=request->header.sequence,.fingerprint=fingerprint};
            return entry;
        }
    }
    return NULL;
}

static SRNOS_HostLinkCoreRequestCache *cache_response(SRNOS_HostLinkContext *link,
                           uint8_t service,
                           uint8_t message,
                           uint32_t sequence,
                           uint32_t fingerprint,
                           const uint8_t *frame,
                           size_t frame_len)
{
    SRNOS_HostLinkCoreRequestCache *entry = NULL;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_REQUEST_CACHE_COUNT; ++i) {
        SRNOS_HostLinkCoreRequestCache *candidate = &link->request_cache[i];
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
    entry->expires_ms = link_now_ms(link) + SRNOS_HOST_LINK_REQUEST_CACHE_TTL_MS;
    entry->frame_len = (uint16_t)frame_len;
    if ((frame != NULL) && (frame_len <= sizeof(entry->frame))) {
        (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(entry->frame,
                                                     sizeof(entry->frame)),
                             SRNOS_Memory_Bytes(frame, frame_len));
    }
    return entry;
}

static SRNOS_HostLinkResult send_response_internal(SRNOS_HostLinkContext *link,
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
    if ((body_len + 2u) > SRNOS_PROTOCOL_MAX_PAYLOAD) {
        return SRNOS_HOST_LINK_ERR_BUFFER;
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
    if (cache) {
        /* Preserve a completed synchronous result even when the TX queue is full.
         * HostComm separately retains deferred completions until queue acceptance. */
        SRNOS_HostLinkCoreRequestCache *entry=cache_response(link,service,message,sequence,
            fingerprint,NULL,0);
        size_t length=0;
        if(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_RESPONSE,flags,service,message,sequence,
            link->response_payload,body_len+2u,entry->frame,sizeof(entry->frame),&length)!=SRNOS_PROTOCOL_OK) {
            entry->used=0; return SRNOS_HOST_LINK_ERR_BUFFER;
        }
        entry->frame_len=(uint16_t)length;
        return queue_encoded_replay(link,entry->frame,length);
    }
    return queue_protocol(link,SRNOS_HOST_LINK_QOS_RELIABLE,0u,
        SRNOS_PROTOCOL_KIND_RESPONSE,flags,service,message,sequence,
        link->response_payload,body_len+2u,NULL,NULL,SRNOS_HOST_LINK_LATEST,NULL,NULL,0);

}

static SRNOS_HostLinkCoreHandlerEntry *find_handler(SRNOS_HostLinkContext *link,
                                               const SRNOS_ProtocolView *view)
{
    const uint8_t kind_bit = SRNOS_HOST_LINK_KIND_MASK(view->header.kind);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_HANDLER_COUNT; ++i) {
        SRNOS_HostLinkCoreHandlerEntry *entry = &link->handlers[i];
        if ((entry->handler != NULL) &&
            (entry->service == view->header.service) &&
            (entry->message == view->header.message) &&
            ((entry->kind_mask & kind_bit) != 0u)) {
            return entry;
        }
    }
    return NULL;
}

static bool service_is_known(const SRNOS_HostLinkContext *link, uint8_t service)
{
    if ((service == SRNOS_HOST_LINK_SERVICE_SYSTEM) ||
        (service == SRNOS_HOST_LINK_SERVICE_CONTROL)) {
        return true;
    }
    for (size_t i = 0u; i < SRNOS_HOST_LINK_HANDLER_COUNT; ++i) {
        if ((link->handlers[i].handler != NULL) &&
            (link->handlers[i].service == service)) {
            return true;
        }
    }
    return false;
}

static SRNOS_HostLinkQos rx_qos_for_view(const SRNOS_ProtocolView *view)
{
    if ((view->header.kind == SRNOS_PROTOCOL_KIND_EVENT) &&
        (view->header.service == SRNOS_HOST_LINK_SERVICE_CONTROL)) {
        return SRNOS_HOST_LINK_QOS_CONTROL;
    }
    if ((view->header.kind == SRNOS_PROTOCOL_KIND_EVENT) &&
        ((view->header.service == SRNOS_HOST_LINK_SERVICE_TELEMETRY) ||
         (view->header.service == SRNOS_HOST_LINK_SERVICE_TEST))) {
        return SRNOS_HOST_LINK_QOS_TELEMETRY;
    }
    return SRNOS_HOST_LINK_QOS_RELIABLE;
}

static uint16_t build_qos_payload(const SRNOS_HostLinkContext *link,
                                  uint8_t *out,
                                  size_t cap,
                                  size_t *out_len)
{
    const size_t global_words = 24u;
    const size_t class_words = 14u;
    const size_t needed = 4u + (global_words * 4u) +
                          (SRNOS_HOST_LINK_QOS_COUNT * class_words * 4u);
    if ((out == NULL) || (out_len == NULL) || (cap < needed)) {
        return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    }
    size_t pos = 0u;
#define SRNOS_HOST_LINK_PUT32(value) do {                                                     \
    (void)SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(out, cap), pos, (value));\
    pos += 4u;                                                               \
} while (0)
    out[pos++] = 1u;
    out[pos++] = SRNOS_HOST_LINK_QOS_COUNT;
    (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(out, cap), pos,
                              (uint16_t)needed);
    pos += 2u;
    SRNOS_HostLinkStats snapshot;
    SRNOS_HostLinkCoreGetStats(link, &snapshot);
    const SRNOS_HostLinkStats *s = &snapshot;
    SRNOS_HOST_LINK_PUT32(s->rx_frames); SRNOS_HOST_LINK_PUT32(s->rx_bytes); SRNOS_HOST_LINK_PUT32(s->tx_frames); SRNOS_HOST_LINK_PUT32(s->tx_bytes);
    SRNOS_HOST_LINK_PUT32(s->cobs_errors); SRNOS_HOST_LINK_PUT32(s->crc_errors); SRNOS_HOST_LINK_PUT32(s->length_errors);
    SRNOS_HOST_LINK_PUT32(s->version_errors); SRNOS_HOST_LINK_PUT32(s->flags_errors); SRNOS_HOST_LINK_PUT32(s->kind_errors);
    SRNOS_HOST_LINK_PUT32(s->unknown_service); SRNOS_HOST_LINK_PUT32(s->unknown_message); SRNOS_HOST_LINK_PUT32(s->rx_overflow);
    SRNOS_HOST_LINK_PUT32(s->sequence_gap); SRNOS_HOST_LINK_PUT32(s->sequence_duplicate); SRNOS_HOST_LINK_PUT32(s->sequence_out_of_order);
    SRNOS_HOST_LINK_PUT32(s->request_replay); SRNOS_HOST_LINK_PUT32(s->request_conflict); SRNOS_HOST_LINK_PUT32(s->transport_busy);
    SRNOS_HOST_LINK_PUT32(s->transport_errors); SRNOS_HOST_LINK_PUT32(s->transport_disconnects);
    SRNOS_HOST_LINK_PUT32(s->processing_cycles_total); SRNOS_HOST_LINK_PUT32(s->processing_cycles_max);
    SRNOS_HOST_LINK_PUT32(s->transport_connected);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_QOS_COUNT; ++i) {
        const SRNOS_HostLinkQosStats *q = &s->qos[i];
        SRNOS_HOST_LINK_PUT32(q->rx_frames); SRNOS_HOST_LINK_PUT32(q->rx_bytes); SRNOS_HOST_LINK_PUT32(q->tx_frames); SRNOS_HOST_LINK_PUT32(q->tx_bytes);
        SRNOS_HOST_LINK_PUT32(q->submitted); SRNOS_HOST_LINK_PUT32(q->sent); SRNOS_HOST_LINK_PUT32(q->rejected); SRNOS_HOST_LINK_PUT32(q->drop_new);
        SRNOS_HOST_LINK_PUT32(q->drop_old); SRNOS_HOST_LINK_PUT32(q->coalesced); SRNOS_HOST_LINK_PUT32(q->queue_depth);
        SRNOS_HOST_LINK_PUT32(q->queue_high_water); SRNOS_HOST_LINK_PUT32(q->last_wait_ms); SRNOS_HOST_LINK_PUT32(q->max_wait_ms);
    }
#undef SRNOS_HOST_LINK_PUT32
    *out_len = pos;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static uint16_t handle_system(SRNOS_HostLinkContext *link,
                              const SRNOS_ProtocolView *view,
                                              uint8_t *response,
                                              size_t response_cap,
                                              size_t *response_len,
                                              uint8_t *response_flags)
{
    *response_len = 0u;
    *response_flags = 0u;
    switch (view->header.message) {
    case SRNOS_HOST_LINK_SYSTEM_HELLO:
        if (view->payload_len != 0u) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        if (response_cap < 12u) {
            return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
        }
        response[0] = SRNOS_PROTOCOL_VERSION;
        response[1] = SRNOS_HOST_LINK_QOS_COUNT;
        SRNOS_Memory_MutableByteSpan response_span =
            SRNOS_Memory_MutableBytes(response, response_cap);
        (void)SRNOS_Endian_StoreLE16(response_span, 2u,
                                  SRNOS_PROTOCOL_MAX_PAYLOAD);
        (void)SRNOS_Endian_StoreLE32(response_span, 4u, 0x0000003Fu);
        response[8] = SRNOS_HOST_LINK_TX_SLOT_COUNT;
        response[9] = SRNOS_HOST_LINK_REQUEST_CACHE_COUNT;
        (void)SRNOS_Endian_StoreLE16(response_span, 10u, 0x000Fu);
        *response_len = 12u;
        return SRNOS_HOST_LINK_STATUS_OK;
    case SRNOS_HOST_LINK_SYSTEM_ECHO:
        if (view->payload_len > response_cap) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        if (view->payload_len != 0u) {
            (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(response, response_cap),
                                 SRNOS_Memory_Bytes(view->payload,
                                                 view->payload_len));
        }
        *response_len = view->payload_len;
        return SRNOS_HOST_LINK_STATUS_OK;
    case SRNOS_HOST_LINK_SYSTEM_QOS_GET:
        if (view->payload_len != 0u) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        return build_qos_payload(link, response, response_cap, response_len);
    case SRNOS_HOST_LINK_SYSTEM_QOS_RESET:
        if (view->payload_len != 0u) {
            return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
        }
        SRNOS_HostLinkCoreResetStats(link);
        return SRNOS_HOST_LINK_STATUS_OK;
    default:
        link->stats.unknown_message++;
        return SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
    }
}

static SRNOS_HostLinkCoreControlMailbox *find_control(SRNOS_HostLinkContext *link, uint16_t channel)
{
    for (size_t i = 0u; i < SRNOS_HOST_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        if ((link->control[i].used != 0u) &&
            (link->control[i].channel == channel)) {
            return &link->control[i];
        }
    }
    return NULL;
}

static uint16_t handle_control_event(SRNOS_HostLinkContext *link,
                                     const SRNOS_ProtocolView *view)
{
    if ((view->header.message != SRNOS_HOST_LINK_CONTROL_STATE) ||
        (view->payload_len < 10u)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const SRNOS_Memory_ByteSpan payload =
        SRNOS_Memory_Bytes(view->payload, view->payload_len);
    uint16_t channel = 0u;
    uint32_t generation = 0u;
    uint32_t deadline_us = 0u;
    if (!SRNOS_Endian_LoadLE16(payload, 0u, &channel) ||
        !SRNOS_Endian_LoadLE32(payload, 2u, &generation) ||
        !SRNOS_Endian_LoadLE32(payload, 6u, &deadline_us)) {
        return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    }
    const size_t data_len = view->payload_len - 10u;
    SRNOS_HostLinkCoreControlMailbox *mailbox = find_control(link, channel);
    if ((mailbox == NULL) || (data_len > mailbox->capacity)) {
        return (mailbox == NULL) ? SRNOS_HOST_LINK_STATUS_NOT_FOUND :
                                   SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
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
        return SRNOS_HOST_LINK_STATUS_OK;
    }
    if (mailbox->pending != 0u) {
        link->stats.qos[SRNOS_HOST_LINK_QOS_CONTROL].coalesced++;
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
    return SRNOS_HOST_LINK_STATUS_OK;
}

static void track_event_sequence(SRNOS_HostLinkContext *link,
                                 const SRNOS_ProtocolView *view)
{
    SRNOS_HostLinkCoreEventTrack *free_entry = NULL;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_EVENT_TRACK_COUNT; ++i) {
        SRNOS_HostLinkCoreEventTrack *entry = &link->event_track[i];
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

static void record_parse_error(SRNOS_HostLinkContext *link, SRNOS_ProtocolStatus status)
{
    switch (status) {
    case SRNOS_PROTOCOL_ERR_CRC: link->stats.crc_errors++; break;
    case SRNOS_PROTOCOL_ERR_VERSION: link->stats.version_errors++; break;
    case SRNOS_PROTOCOL_ERR_FLAGS: link->stats.flags_errors++; break;
    case SRNOS_PROTOCOL_ERR_KIND: link->stats.kind_errors++; break;
    case SRNOS_PROTOCOL_ERR_LEN:
    case SRNOS_PROTOCOL_ERR_RAW_LEN: link->stats.length_errors++; break;
    default: link->stats.cobs_errors++; break;
    }
}

static SRNOS_COBSStatus on_raw_frame(void *user,
                                      const uint8_t *raw,
                                      size_t raw_len)
{
    SRNOS_HostLinkContext *link = (SRNOS_HostLinkContext *)user;
    SRNOS_ProtocolView view;
    const SRNOS_ProtocolStatus parse_status =
        SRNOS_ProtocolParse(raw, raw_len, &view);
    if (parse_status != SRNOS_PROTOCOL_OK) {
        record_parse_error(link, parse_status);
        return SRNOS_COBS_ERR_DECODE;
    }

    link->stats.rx_frames++;
    const SRNOS_HostLinkQos rx_qos = rx_qos_for_view(&view);
    link->stats.qos[rx_qos].rx_frames++;
    link->stats.qos[rx_qos].rx_bytes += (uint32_t)raw_len;
    if (view.header.kind == SRNOS_PROTOCOL_KIND_EVENT) {
        track_event_sequence(link, &view);
        if (view.header.service == SRNOS_HOST_LINK_SERVICE_CONTROL) {
            const uint16_t control_status = handle_control_event(link, &view);
            if (control_status == SRNOS_HOST_LINK_STATUS_NOT_FOUND) {
                link->stats.unknown_service++;
            } else if (control_status != SRNOS_HOST_LINK_STATUS_OK) {
                link->stats.unknown_message++;
            }
            return SRNOS_COBS_OK;
        }
    }

    uint32_t fingerprint = 0u;
    SRNOS_HostLinkActiveRequest *active=NULL;
    if (view.header.kind == SRNOS_PROTOCOL_KIND_REQUEST) {
        fingerprint = request_fingerprint(&view);
        active=find_active(link,view.header.service,view.header.message,view.header.sequence);
        if(active) {
            const bool conflict=active->fingerprint!=fingerprint;
            if(conflict) ++link->stats.request_conflict; else ++link->stats.request_replay;
            (void)send_response_internal(link,view.header.service,view.header.message,view.header.sequence,
                conflict?SRNOS_HOST_LINK_STATUS_CONFLICT:SRNOS_HOST_LINK_STATUS_ACCEPTED,
                0,NULL,0,fingerprint,false);
            return SRNOS_COBS_OK;
        }
        SRNOS_HostLinkCoreRequestCache *cached = find_cache(link, &view, fingerprint);
        if (cached != NULL) {
            if (cached->fingerprint != fingerprint) {
                link->stats.request_conflict++;
                (void)send_response_internal(link,view.header.service,view.header.message,view.header.sequence,
                    SRNOS_HOST_LINK_STATUS_CONFLICT,0,NULL,0,fingerprint,false);
            } else {
                link->stats.request_replay++;
                (void)queue_encoded_replay(link,cached->frame,cached->frame_len);
            }
            return SRNOS_COBS_OK;
        }
        active=reserve_active(link,&view,fingerprint);
        if(!active) {
            (void)send_response_internal(link,view.header.service,view.header.message,view.header.sequence,
                SRNOS_HOST_LINK_STATUS_BUSY,0,NULL,0,fingerprint,false);
            return SRNOS_COBS_OK;
        }
    }

    size_t response_len = 0u;
    uint8_t response_flags = 0u;
    uint16_t status = SRNOS_HOST_LINK_STATUS_UNSUPPORTED_SERVICE;
    if (view.header.service == SRNOS_HOST_LINK_SERVICE_SYSTEM) {
        if (view.header.kind != SRNOS_PROTOCOL_KIND_REQUEST) {
            status = SRNOS_HOST_LINK_STATUS_BAD_KIND;
        } else {
            status = handle_system(link, &view, &link->response_payload[2],
                                   sizeof(link->response_payload) - 2u,
                                   &response_len, &response_flags);
        }
    } else {
        SRNOS_HostLinkCoreHandlerEntry *handler = find_handler(link, &view);
        if (handler == NULL) {
            if (service_is_known(link, view.header.service)) {
                link->stats.unknown_message++;
                status = SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE;
            } else {
                link->stats.unknown_service++;
                status = SRNOS_HOST_LINK_STATUS_UNSUPPORTED_SERVICE;
            }
        } else {
            link->dispatching_request=active;
            status = handler->handler(handler->user, &view,
                                      &link->response_payload[2],
                                      sizeof(link->response_payload) - 2u,
                                      &response_len, &response_flags);
            link->dispatching_request=NULL;
            if(active && active->deferred_submitted)status=SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL;
        }
    }

    if (view.header.kind == SRNOS_PROTOCOL_KIND_REQUEST) {
        if (status != SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL) {
            if(response_len>sizeof(link->response_payload)-2u || (response_flags & (uint8_t)~SRNOS_PROTOCOL_VALID_FLAGS)) {
                status=SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;response_len=0;response_flags=0;
            }
            (void)send_response_internal(link, view.header.service,
                                         view.header.message,
                                         view.header.sequence, status,
                                         response_flags,
                                         &link->response_payload[2], response_len,
                                         fingerprint, true);
            active->used=0;
        }
    }
    return SRNOS_COBS_OK;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreInit(SRNOS_HostLinkContext *link,
                                const SRNOS_HostLinkCoreConfig *config)
{
    if ((link == NULL) || (config == NULL) ||
        (config->tx_submit == NULL) ||
        (config->tx_storage == NULL) ||
        (config->tx_storage_size <
         (SRNOS_HOST_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE)) ||
        ((config->lock == NULL) != (config->unlock == NULL))) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    memset(link, 0, sizeof(*link));
    link->config = *config;
    link->in_flight = -1;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        link->slots[i].data = &config->tx_storage[
            i * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    }
    if (SRNOS_COBSStreamInit(&link->rx_stream, link->rx_raw,
                             sizeof(link->rx_raw), on_raw_frame, link) !=
        SRNOS_COBS_OK) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    link->initialized = 1u;
    return SRNOS_HOST_LINK_OK;
}

void SRNOS_HostLinkCoreResetRx(SRNOS_HostLinkContext *link)
{
    if (link != NULL) {
        SRNOS_COBSStreamReset(&link->rx_stream);
    }
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreFeedRx(SRNOS_HostLinkContext *link,
                                   const uint8_t *data,
                                   size_t len)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        ((data == NULL) && (len != 0u))) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    const uint32_t start = link_cycles(link);
    link->stats.rx_bytes += (uint32_t)len;
    const uint32_t old_decode = link->rx_stream.stat_decode_error;
    const uint32_t old_long = link->rx_stream.stat_too_long;
    const SRNOS_COBSStatus status = SRNOS_COBSStreamFeed(&link->rx_stream,
                                                         data, len);
    link->stats.cobs_errors += link->rx_stream.stat_decode_error - old_decode;
    link->stats.rx_overflow += link->rx_stream.stat_too_long - old_long;
    record_cycles(link, start);
    return (status == SRNOS_COBS_OK) ? SRNOS_HOST_LINK_OK : SRNOS_HOST_LINK_ERR_PARAM;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreRegisterHandler(SRNOS_HostLinkContext *link,
                                            uint8_t service,
                                            uint8_t message,
                                            uint8_t kind_mask,
                                            SRNOS_HostLinkQos qos,
                                            SRNOS_HostLinkHandlerFn handler,
                                            void *user)
{
    if ((link == NULL) || (handler == NULL) || (kind_mask == 0u) ||
        (qos >= SRNOS_HOST_LINK_QOS_COUNT)) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    if (link->registrations_frozen) return SRNOS_HOST_LINK_ERR_BUSY;
    if ((kind_mask & (uint8_t)~7u) != 0u) return SRNOS_HOST_LINK_ERR_PARAM;
    for (size_t i=0;i<SRNOS_HOST_LINK_HANDLER_COUNT;i++) {
        const SRNOS_HostLinkCoreHandlerEntry *entry=&link->handlers[i];
        if(entry->handler && entry->service==service && entry->message==message &&
            (entry->kind_mask & kind_mask)) return SRNOS_HOST_LINK_ERR_PARAM;
    }
    for (size_t i = 0u; i < SRNOS_HOST_LINK_HANDLER_COUNT; ++i) {
        SRNOS_HostLinkCoreHandlerEntry *entry = &link->handlers[i];
        if (entry->handler == NULL) {
            entry->service = service;
            entry->message = message;
            entry->kind_mask = kind_mask;
            entry->qos = (uint8_t)qos;
            entry->handler = handler;
            entry->user = user;
            return SRNOS_HOST_LINK_OK;
        }
    }
    return SRNOS_HOST_LINK_ERR_FULL;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreRegisterTxTopic(SRNOS_HostLinkContext *link,
                                             uint8_t service,
                                             uint8_t message,
                                             SRNOS_HostLinkQos qos)
{
    if ((link == NULL) || (qos >= SRNOS_HOST_LINK_QOS_LOG)) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    if (link->registrations_frozen) return SRNOS_HOST_LINK_ERR_BUSY;
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_TOPIC_COUNT; ++i) {
        SRNOS_HostLinkCoreTxTopic *topic = &link->tx_topics[i];
        if ((topic->used != 0u) && (topic->service == service) &&
            (topic->message == message)) {
            return SRNOS_HOST_LINK_ERR_PARAM;
        }
        if (topic->used == 0u) {
            topic->used = 1u;
            topic->service = service;
            topic->message = message;
            topic->qos = (uint8_t)qos;
            return SRNOS_HOST_LINK_OK;
        }
    }
    return SRNOS_HOST_LINK_ERR_FULL;
}

SRNOS_HostLinkResult SRNOS_HostLinkCorePublishEx(SRNOS_HostLinkContext *link,
                                   uint8_t service,
                                   uint8_t message,
                                   uint16_t key,
                                   uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_len, SRNOS_HostLinkPolicy policy,
                                   SRNOS_HostLinkDeliveryFn done, void *user, uint32_t tag)
{
    if ((policy != SRNOS_HOST_LINK_LATEST && policy != SRNOS_HOST_LINK_FIFO) ||
        (done && policy != SRNOS_HOST_LINK_FIFO)) return SRNOS_HOST_LINK_ERR_PARAM;
    if (link == NULL) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_TOPIC_COUNT; ++i) {
        const SRNOS_HostLinkCoreTxTopic *topic = &link->tx_topics[i];
        if ((topic->used != 0u) && (topic->service == service) &&
            (topic->message == message)) {
            return queue_protocol(link, (SRNOS_HostLinkQos)topic->qos, key,
                                  SRNOS_PROTOCOL_KIND_EVENT, 0u,
                                  service, message, sequence,
                                  payload, payload_len, NULL, NULL, policy, done, user, tag);
        }
    }
    return SRNOS_HOST_LINK_ERR_NOT_FOUND;
}

SRNOS_HostLinkResult SRNOS_HostLinkCorePublish(SRNOS_HostLinkContext *link, uint8_t service, uint8_t message,
    uint16_t key, uint32_t sequence, const uint8_t *payload, size_t length)
{
    return SRNOS_HostLinkCorePublishEx(link,service,message,key,sequence,payload,length,
        SRNOS_HOST_LINK_LATEST,NULL,NULL,0);
}
static void retire_slot(SRNOS_HostLinkCoreTxSlot *slot,SRNOS_HostLinkDelivery result)
{
    slot->delivery_result=(uint8_t)result;
    slot->state=slot->on_delivery?SRNOS_HOST_LINK_SLOT_DELIVERY_PENDING:SRNOS_HOST_LINK_SLOT_FREE;
}
void SRNOS_HostLinkCoreDrainDeliveries(SRNOS_HostLinkContext *link)
{
    if(!link)return;
    for(unsigned i=0;i<SRNOS_HOST_LINK_TX_SLOT_COUNT;i++) {
        SRNOS_HostLinkDeliveryFn callback=NULL;void *user=NULL;uint32_t tag=0;
        SRNOS_HostLinkDelivery result=SRNOS_HOST_LINK_CANCELLED;
        link_lock(link);
        SRNOS_HostLinkCoreTxSlot *slot=&link->slots[i];
        if(slot->state==SRNOS_HOST_LINK_SLOT_DELIVERY_PENDING) {
            callback=slot->on_delivery;user=slot->delivery_user;tag=slot->tag;
            result=(SRNOS_HostLinkDelivery)slot->delivery_result;
            slot->on_delivery=NULL;slot->state=SRNOS_HOST_LINK_SLOT_FREE;
        }
        link_unlock(link);
        if(callback)callback(user,tag,result);
    }
}
size_t SRNOS_HostLinkCoreDiscardStream(SRNOS_HostLinkContext *link, uint8_t service, uint8_t message, uint16_t key)
{
    size_t count=0; link_lock(link);
    for (unsigned i=0;i<SRNOS_HOST_LINK_TX_SLOT_COUNT;i++) {
        SRNOS_HostLinkCoreTxSlot *s=&link->slots[i];
        if(s->service!=service || s->message!=message || s->key!=key) continue;
        if(s->state==SRNOS_HOST_LINK_SLOT_QUEUED) { retire_slot(s,SRNOS_HOST_LINK_CANCELLED); ++count; }
        else if(s->state==SRNOS_HOST_LINK_SLOT_IN_FLIGHT) s->cancelled=1;
    }
    sync_depths(link); link_unlock(link); return count;
}
void SRNOS_HostLinkCoreResetSession(SRNOS_HostLinkContext *link)
{
    if(!link)return;
    link_lock(link);++link->session;link_unlock(link);
    for(unsigned i=0;i<SRNOS_HOST_LINK_TX_SLOT_COUNT;i++) {
        link_lock(link);
        SRNOS_HostLinkCoreTxSlot *slot=&link->slots[i];
        if(slot->state==SRNOS_HOST_LINK_SLOT_QUEUED)retire_slot(slot,SRNOS_HOST_LINK_CANCELLED);
        else if(slot->state==SRNOS_HOST_LINK_SLOT_IN_FLIGHT)slot->cancelled=1;
        link_unlock(link);
    }
    /* Only HostComm accesses request/RX bookkeeping. Do not clear kilobytes
     * of replay storage while interrupts are masked. */
    memset(link->request_cache,0,sizeof(link->request_cache));
    memset(link->active_requests,0,sizeof(link->active_requests));
    memset(link->event_track,0,sizeof(link->event_track));
    for(unsigned i=0;i<SRNOS_HOST_LINK_CONTROL_CHANNEL_COUNT;i++) {
        link_lock(link);link->control[i].pending=0;link_unlock(link);
    }
    SRNOS_HostLinkCoreResetRx(link);
    link_lock(link);sync_depths(link);link_unlock(link);
}

size_t SRNOS_HostLinkCoreDiscardTopic(SRNOS_HostLinkContext *link,
                              uint8_t service,
                              uint8_t message)
{
    if (link == NULL) {
        return 0u;
    }
    size_t discarded = 0u;
    link_lock(link);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        SRNOS_HostLinkCoreTxSlot *slot = &link->slots[i];
        if ((slot->state == SRNOS_HOST_LINK_SLOT_QUEUED) &&
            (slot->kind == SRNOS_PROTOCOL_KIND_EVENT) &&
            (slot->service == service) && (slot->message == message)) {
            if (slot->qos < SRNOS_HOST_LINK_QOS_COUNT) {
                link->stats.qos[slot->qos].drop_old++;
            }
            retire_slot(slot,SRNOS_HOST_LINK_CANCELLED);
            discarded++;
        }
    }
    sync_depths(link);
    link_unlock(link);
    return discarded;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreSubmitLog(SRNOS_HostLinkContext *link,
                                      const uint8_t *line,
                                      size_t len)
{
    if ((link == NULL) || (link->initialized == 0u) ||
        (line == NULL) || (len == 0u) ||
        (len > SRNOS_PROTOCOL_FRAME_SLOT_SIZE)) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    bool replaced = false;
    const int slot_index = reserve_slot(link, SRNOS_HOST_LINK_QOS_LOG, 0u,
                                        0u, 0u, &replaced, SRNOS_HOST_LINK_LATEST);
    if (slot_index < 0) {
        return SRNOS_HOST_LINK_ERR_FULL;
    }
    if (!SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(link->slots[slot_index].data,
                               SRNOS_PROTOCOL_FRAME_SLOT_SIZE),
        SRNOS_Memory_Bytes(line, len))) { cancel_slot(link,slot_index); return SRNOS_HOST_LINK_ERR_BUFFER; }
    if(!finish_slot(link, slot_index, SRNOS_HOST_LINK_QOS_LOG, 0u, 0u, 0u, 0u,
                len, replaced)) return SRNOS_HOST_LINK_ERR_BUSY;
    return SRNOS_HOST_LINK_OK;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreSendResponse(SRNOS_HostLinkContext *link,
                                         uint8_t service,
                                         uint8_t message,
                                         uint32_t sequence,
                                         uint16_t status,
                                         uint8_t flags,
                                         const uint8_t *body,
                                         size_t body_len)
{
    if (link == NULL) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    SRNOS_HostLinkActiveRequest *active=find_active(link,service,message,sequence);
    const SRNOS_HostLinkResult result=send_response_internal(link,service,message,sequence,status,flags,
        body,body_len,active?active->fingerprint:0,active!=NULL);
    if(result==SRNOS_HOST_LINK_OK && active)active->used=0;
    return result;
}

static int select_tx_slot(SRNOS_HostLinkContext *link)
{
    const uint32_t now = link_now_ms(link);
    int q0 = find_oldest_slot(link, SRNOS_HOST_LINK_QOS_CONTROL, false);
    const int q1 = find_oldest_slot(link, SRNOS_HOST_LINK_QOS_RELIABLE, false);
    if ((q1 >= 0) && ((now - link->slots[q1].enqueued_ms) >= SRNOS_HOST_LINK_Q1_FORCE_MS) &&
        (link->q0_burst != 0u)) {
        return q1;
    }
    if (q0 >= 0) {
        return q0;
    }
    for (size_t n = 0u; n < sizeof(k_rr_order); ++n) {
        const uint8_t qos = k_rr_order[link->rr_cursor];
        link->rr_cursor = (uint8_t)((link->rr_cursor + 1u) % sizeof(k_rr_order));
        const int selected = find_oldest_slot(link, (SRNOS_HostLinkQos)qos, false);
        if (selected >= 0) {
            return selected;
        }
    }
    return -1;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreServiceTx(SRNOS_HostLinkContext *link)
{
    if ((link == NULL) || (link->initialized == 0u)) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    if (link->in_flight >= 0) {
        return SRNOS_HOST_LINK_ERR_BUSY;
    }

    link_lock(link);
    const int slot_index = select_tx_slot(link);
    if (slot_index < 0) {
        link_unlock(link);
        return SRNOS_HOST_LINK_OK;
    }
    SRNOS_HostLinkCoreTxSlot *slot = &link->slots[slot_index];
    slot->state = SRNOS_HOST_LINK_SLOT_IN_FLIGHT;
    link->in_flight = (int8_t)slot_index;
    const uint32_t wait_ms = link_now_ms(link) - slot->enqueued_ms;
    link->stats.qos[slot->qos].last_wait_ms = wait_ms;
    if (wait_ms > link->stats.qos[slot->qos].max_wait_ms) {
        link->stats.qos[slot->qos].max_wait_ms = wait_ms;
    }
    sync_depths(link);
    link_unlock(link);

    const SRNOS_HostLinkCoreTxResult tx_status =
        link->config.tx_submit(link->config.user, slot->data, slot->len);
    if (tx_status == SRNOS_HOST_LINK_TX_ACCEPTED) {
        link->stats.transport_connected = 1u;
        if (slot->qos == SRNOS_HOST_LINK_QOS_CONTROL) {
            link->q0_burst++;
        } else {
            link->q0_burst = 0u;
        }
        return SRNOS_HOST_LINK_OK;
    }

    link_lock(link);
    slot->state = SRNOS_HOST_LINK_SLOT_QUEUED;
    link->in_flight = -1;
    sync_depths(link);
    link_unlock(link);
    if (tx_status == SRNOS_HOST_LINK_TX_BUSY) {
        link->stats.transport_busy++;
        return SRNOS_HOST_LINK_ERR_BUSY;
    }
    if (tx_status == SRNOS_HOST_LINK_TX_DISCONNECTED) {
        link->stats.transport_connected = 0u;
        link->stats.transport_disconnects++;
    } else {
        link->stats.transport_errors++;
    }
    return SRNOS_HOST_LINK_ERR_TRANSPORT;
}

void SRNOS_HostLinkCoreOnTxComplete(SRNOS_HostLinkContext *link, bool success)
{
    if ((link == NULL) || (link->in_flight < 0)) {
        return;
    }
    link_lock(link);
    SRNOS_HostLinkCoreTxSlot *slot = &link->slots[(size_t)link->in_flight];
    if (slot->cancelled) { retire_slot(slot,SRNOS_HOST_LINK_CANCELLED); }
    else if (success) {
        link->stats.tx_frames++;
        link->stats.tx_bytes += slot->len;
        link->stats.qos[slot->qos].tx_frames++;
        link->stats.qos[slot->qos].tx_bytes += slot->len;
        link->stats.qos[slot->qos].sent++;
        retire_slot(slot,SRNOS_HOST_LINK_DELIVERED);
    } else if (slot->qos == SRNOS_HOST_LINK_QOS_RELIABLE) {
        link->stats.transport_errors++;
        slot->state = SRNOS_HOST_LINK_SLOT_QUEUED;
        slot->enqueued_ms = link_now_ms(link);
    } else {
        link->stats.transport_errors++;
        link->stats.qos[slot->qos].drop_new++;
        retire_slot(slot,SRNOS_HOST_LINK_FAILED);
    }
    link->in_flight = -1;
    sync_depths(link);
    link_unlock(link);
}

bool SRNOS_HostLinkCoreHasPendingTx(const SRNOS_HostLinkContext *link)
{
    if (link == NULL) {
        return false;
    }
    if (link->in_flight >= 0) {
        return true;
    }
    for (size_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        if (link->slots[i].state == SRNOS_HOST_LINK_SLOT_QUEUED) {
            return true;
        }
    }
    return false;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreControlRegister(SRNOS_HostLinkContext *link,
                                            uint16_t channel,
                                            uint8_t *storage,
                                            size_t capacity)
{
    if ((link == NULL) || (storage == NULL) || (capacity == 0u) ||
        (capacity > (SRNOS_PROTOCOL_MAX_PAYLOAD - 10u))) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    if (link->registrations_frozen) return SRNOS_HOST_LINK_ERR_BUSY;
    link_lock(link);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_CONTROL_CHANNEL_COUNT; ++i) {
        SRNOS_HostLinkCoreControlMailbox *mailbox = &link->control[i];
        if ((mailbox->used != 0u) && (mailbox->channel == channel)) {
            link_unlock(link);
            return SRNOS_HOST_LINK_ERR_PARAM;
        }
        if (mailbox->used == 0u) {
            memset(mailbox, 0, sizeof(*mailbox));
            mailbox->used = 1u;
            mailbox->channel = channel;
            mailbox->storage = storage;
            mailbox->capacity = capacity;
            link_unlock(link);
            return SRNOS_HOST_LINK_OK;
        }
    }
    link_unlock(link);
    return SRNOS_HOST_LINK_ERR_FULL;
}

SRNOS_HostLinkResult SRNOS_HostLinkCoreControlTake(SRNOS_HostLinkContext *link,
                                        uint16_t channel,
                                        uint8_t *out,
                                        size_t out_cap,
                                        size_t *out_len,
                                        uint32_t *generation,
                                        uint32_t *deadline_us,
                                        uint32_t *received_ms)
{
    if ((link == NULL) || (out == NULL) || (out_len == NULL)) {
        return SRNOS_HOST_LINK_ERR_PARAM;
    }
    SRNOS_HostLinkCoreControlMailbox *mailbox = find_control(link, channel);
    if (mailbox == NULL) {
        return SRNOS_HOST_LINK_ERR_NOT_FOUND;
    }
    link_lock(link);
    if (mailbox->pending == 0u) {
        link_unlock(link);
        return SRNOS_HOST_LINK_ERR_NOT_FOUND;
    }
    if (out_cap < mailbox->length) {
        link_unlock(link);
        return SRNOS_HOST_LINK_ERR_BUFFER;
    }
    (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(out, out_cap),
                         SRNOS_Memory_Bytes(mailbox->storage, mailbox->length));
    *out_len = mailbox->length;
    if (generation != NULL) { *generation = mailbox->generation; }
    if (deadline_us != NULL) { *deadline_us = mailbox->deadline_us; }
    if (received_ms != NULL) { *received_ms = mailbox->received_ms; }
    mailbox->pending = 0u;
    link_unlock(link);
    return SRNOS_HOST_LINK_OK;
}

void SRNOS_HostLinkCoreGetStats(const SRNOS_HostLinkContext *link, SRNOS_HostLinkStats *out)
{
    if ((link != NULL) && (out != NULL)) {
        link_lock(link);
        *out = link->stats;
        link_unlock(link);
    }
}

void SRNOS_HostLinkCoreResetStats(SRNOS_HostLinkContext *link)
{
    if (link == NULL) {
        return;
    }
    link_lock(link);
    const uint32_t transport_connected = link->stats.transport_connected;
    memset(&link->stats, 0, sizeof(link->stats));
    link->stats.transport_connected = transport_connected;
    sync_depths(link);
    for (size_t i = 0u; i < SRNOS_HOST_LINK_QOS_COUNT; ++i) {
        link->stats.qos[i].queue_high_water =
            link->stats.qos[i].queue_depth;
    }
    link_unlock(link);
}

void SRNOS_HostLinkCoreRecordProcessingCycles(SRNOS_HostLinkContext *link, uint32_t cycles)
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

void SRNOS_HostLinkCoreRecordRxOverflow(SRNOS_HostLinkContext *link, uint32_t bytes)
{
    if (link != NULL) {
        link->stats.rx_overflow += bytes;
    }
}

void SRNOS_HostLinkCoreSetTransportConnected(SRNOS_HostLinkContext *link, bool connected)
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
