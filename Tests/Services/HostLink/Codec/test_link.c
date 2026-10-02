#include "SirinEmbedOS/Services/HostLink/Codec/COBS.h"
#include "SirinEmbedOS/Services/HostLink/Codec/Protocol.h"
#include "link_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t test_load_le16(const uint8_t *data)
{
    uint16_t value = 0u;
    assert(SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(data, 2u), 0u, &value));
    return value;
}

static uint32_t test_load_le32(const uint8_t *data)
{
    uint32_t value = 0u;
    assert(SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(data, 4u), 0u, &value));
    return value;
}

static void test_store_le16(uint8_t *data, uint16_t value)
{
    assert(SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(data, 2u), 0u, value));
}

static void test_store_le32(uint8_t *data, uint32_t value)
{
    assert(SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(data, 4u), 0u, value));
}


typedef struct {
    uint32_t now_ms;
    uint32_t cycles;
    uint8_t tx[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t tx_len;
    uint32_t submits;
    SRNOS_HostLinkCoreTxResult tx_result;
    SRNOS_HOST_LINK_ALIGN32 uint8_t tx_storage[
        SRNOS_HOST_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
} fixture_t;

static unsigned critical_depth;
static void test_lock(void *user) { (void)user;++critical_depth; }
static void test_unlock(void *user) { (void)user;assert(critical_depth);--critical_depth; }
static uint32_t test_now(void *user)
{
    return ((fixture_t *)user)->now_ms;
}

static SRNOS_HostLinkContext *reset_on_cycles;
static uint32_t test_cycles(void *user)
{
    fixture_t *fixture = (fixture_t *)user;
    if(reset_on_cycles) { SRNOS_HostLinkContext *link=reset_on_cycles; reset_on_cycles=NULL; SRNOS_HostLinkCoreResetSession(link); }
    fixture->cycles += 17u;
    return fixture->cycles;
}

static SRNOS_HostLinkCoreTxResult test_tx(void *user,
                                    const uint8_t *data,
                                    size_t len)
{
    fixture_t *fixture = (fixture_t *)user;
    assert(len <= sizeof(fixture->tx));
    memcpy(fixture->tx, data, len);
    fixture->tx_len = len;
    fixture->submits++;
    return fixture->tx_result;
}

static void init_link(SRNOS_HostLinkContext *link, fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    const SRNOS_HostLinkCoreConfig config = {
        .now_ms = test_now,
        .cycles = test_cycles,
        .tx_submit = test_tx,
        .lock = test_lock,
        .unlock = test_unlock,
        .tx_storage = fixture->tx_storage,
        .tx_storage_size = sizeof(fixture->tx_storage),
        .user = fixture,
    };
    assert(SRNOS_HostLinkCoreInit(link, &config) == SRNOS_HOST_LINK_OK);
}

static SRNOS_ProtocolView decode_frame(const uint8_t *frame,
                                         size_t frame_len,
                                         uint8_t *raw,
                                         size_t raw_cap)
{
    assert(frame_len >= 3u);
    assert(frame[0] == 0u);
    assert(frame[frame_len - 1u] == 0u);
    size_t raw_len = 0u;
    assert(SRNOS_COBSDecode(&frame[1], frame_len - 2u,
                           raw, raw_cap, &raw_len) == SRNOS_COBS_OK);
    SRNOS_ProtocolView view;
    assert(SRNOS_ProtocolParse(raw, raw_len, &view) == SRNOS_PROTOCOL_OK);
    return view;
}

static size_t pack_request(uint8_t message,
                           uint32_t sequence,
                           const uint8_t *payload,
                           size_t payload_len,
                           uint8_t *frame)
{
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_SYSTEM, message, sequence,
                              payload, payload_len, frame,
                              SRNOS_PROTOCOL_FRAME_SLOT_SIZE,
                              &frame_len) == SRNOS_PROTOCOL_OK);
    return frame_len;
}

static void complete_one(SRNOS_HostLinkContext *link)
{
    assert(SRNOS_HostLinkCoreServiceTx(link) == SRNOS_HOST_LINK_OK);
    SRNOS_HostLinkCoreOnTxComplete(link, true); SRNOS_HostLinkCoreDrainDeliveries(link);
}

static void test_crc_and_max_frame(void)
{
    static const uint8_t check[] = "123456789";
    assert(SRNOS_ProtocolCRC32(check, sizeof(check) - 1u) == 0xCBF43926u);

    static const uint8_t hello_golden[] = {
        0x00u, 0x03u, 0xA5u, 0x02u, 0x01u, 0x01u, 0x02u, 0x01u,
        0x01u, 0x09u, 0x04u, 0x03u, 0x02u, 0x01u, 0x08u, 0xCCu,
        0x8Eu, 0xADu, 0x00u,
    };
    uint8_t hello[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t hello_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_SYSTEM,
                              SRNOS_HOST_LINK_SYSTEM_HELLO, 0x01020304u,
                              NULL, 0u, hello, sizeof(hello),
                              &hello_len) == SRNOS_PROTOCOL_OK);
    assert(hello_len == sizeof(hello_golden));
    assert(memcmp(hello, hello_golden, sizeof(hello_golden)) == 0);

    uint8_t payload[SRNOS_PROTOCOL_MAX_PAYLOAD];
    memset(payload, 0x55, sizeof(payload));
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_EVENT, 0u,
                              SRNOS_HOST_LINK_SERVICE_TELEMETRY, 1u, 7u,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRNOS_PROTOCOL_OK);
    assert(frame_len <= SRNOS_PROTOCOL_MAX_FRAME_LEN);
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    SRNOS_ProtocolView view = decode_frame(frame, frame_len, raw, sizeof(raw));
    assert(view.payload_len == sizeof(payload));
    assert(memcmp(view.payload, payload, sizeof(payload)) == 0);
}

static void test_split_stream_and_replay(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);

    uint8_t request[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t request_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_SYSTEM,
                              SRNOS_HOST_LINK_SYSTEM_HELLO, 0x10203040u,
                              NULL, 0u, request, sizeof(request),
                              &request_len) == SRNOS_PROTOCOL_OK);

    static const uint8_t ascii[] = "log-before-frame\r\n";
    assert(SRNOS_HostLinkCoreFeedRx(&link, ascii, sizeof(ascii) - 1u) == SRNOS_HOST_LINK_OK);
    for (size_t i = 0u; i < request_len; ++i) {
        assert(SRNOS_HostLinkCoreFeedRx(&link, &request[i], 1u) == SRNOS_HOST_LINK_OK);
    }
    assert(link.stats.rx_frames == 1u);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
    uint8_t first_response[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    const size_t first_len = fixture.tx_len;
    memcpy(first_response, fixture.tx, first_len);
    SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);

    assert(SRNOS_HostLinkCoreFeedRx(&link, request, request_len) == SRNOS_HOST_LINK_OK);
    assert(link.stats.request_replay == 1u);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
    assert(fixture.tx_len == first_len);
    assert(memcmp(fixture.tx, first_response, first_len) == 0);
    SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);

    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    const SRNOS_ProtocolView response =
        decode_frame(first_response, first_len, raw, sizeof(raw));
    assert(response.header.kind == SRNOS_PROTOCOL_KIND_RESPONSE);
    assert(response.header.sequence == 0x10203040u);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
}

static void test_qos_coalesce_and_control_mailbox(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,
                                      SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                                      0x20u,
                                      SRNOS_HOST_LINK_QOS_TELEMETRY) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,
                                      SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                                      0x21u,
                                      SRNOS_HOST_LINK_QOS_TELEMETRY) == SRNOS_HOST_LINK_OK);
    const uint8_t one[] = {1u};
    const uint8_t two[] = {2u};
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x20u,
                            9u, 1u, one, sizeof(one)) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x20u,
                            9u, 2u, two, sizeof(two)) == SRNOS_HOST_LINK_OK);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].queue_depth == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].coalesced == 1u);
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x21u,
                            9u, 3u, one, sizeof(one)) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreSendResponse(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY, 0x20u,
                                  4u, SRNOS_HOST_LINK_STATUS_OK, 0u, NULL, 0u) ==
           SRNOS_HOST_LINK_OK);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].queue_depth == 2u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].coalesced == 1u);
    assert(SRNOS_HostLinkCoreDiscardTopic(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                                  0x20u) == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].queue_depth == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].drop_old == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_RELIABLE].queue_depth == 1u);
    assert(SRNOS_HostLinkCoreDiscardTopic(&link, SRNOS_HOST_LINK_SERVICE_TELEMETRY,
                                  0x20u) == 0u);
    const uint32_t cycles_before = link.stats.processing_cycles_total;
    SRNOS_HostLinkCoreRecordProcessingCycles(&link, 123u);
    assert(link.stats.processing_cycles_total == cycles_before + 123u);
    assert(link.stats.processing_cycles_max >= 123u);

    uint8_t mailbox_storage[32];
    assert(SRNOS_HostLinkCoreControlRegister(&link, 3u, mailbox_storage,
                                     sizeof(mailbox_storage)) == SRNOS_HOST_LINK_OK);
    uint8_t control_payload[13];
    test_store_le16(&control_payload[0], 3u);
    test_store_le32(&control_payload[2], 8u);
    test_store_le32(&control_payload[6], 5000u);
    control_payload[10] = 0xAAu;
    control_payload[11] = 0xBBu;
    control_payload[12] = 0xCCu;
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_EVENT, 0u,
                              SRNOS_HOST_LINK_SERVICE_CONTROL,
                              SRNOS_HOST_LINK_CONTROL_STATE, 1u,
                              control_payload, sizeof(control_payload),
                              frame, sizeof(frame), &frame_len) == SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    uint8_t out[8];
    size_t out_len = 0u;
    uint32_t generation = 0u;
    assert(SRNOS_HostLinkCoreControlTake(&link, 3u, out, sizeof(out), &out_len,
                                 &generation, NULL, NULL) == SRNOS_HOST_LINK_OK);
    assert(out_len == 3u);
    assert(generation == 8u);
    assert(memcmp(out, &control_payload[10], 3u) == 0);
}

static void test_corruption(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_SYSTEM,
                              SRNOS_HOST_LINK_SYSTEM_ECHO, 1u,
                              (const uint8_t *)"abc", 3u,
                              frame, sizeof(frame), &frame_len) == SRNOS_PROTOCOL_OK);
    frame[frame_len / 2u] ^= 0x20u;
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert((link.stats.crc_errors + link.stats.cobs_errors) != 0u);
    assert(link.stats.rx_frames == 0u);
}

static void test_all_split_positions_and_framing_errors(void)
{
    uint8_t request[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    const size_t request_len = pack_request(SRNOS_HOST_LINK_SYSTEM_HELLO, 77u,
                                            NULL, 0u, request);
    for (size_t split = 0u; split <= request_len; ++split) {
        SRNOS_HostLinkContext link;
        fixture_t fixture;
        init_link(&link, &fixture);
        assert(SRNOS_HostLinkCoreFeedRx(&link, request, split) == SRNOS_HOST_LINK_OK);
        assert(SRNOS_HostLinkCoreFeedRx(&link, &request[split], request_len - split) ==
               SRNOS_HOST_LINK_OK);
        assert(link.stats.rx_frames == 1u);
    }

    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    static const uint8_t delimiters[] = {0u, 0u, 0u, 0u};
    assert(SRNOS_HostLinkCoreFeedRx(&link, delimiters, sizeof(delimiters)) == SRNOS_HOST_LINK_OK);
    assert(link.stats.rx_frames == 0u);

    assert(SRNOS_HostLinkCoreFeedRx(&link, request, request_len - 1u) == SRNOS_HOST_LINK_OK);
    assert(link.stats.rx_frames == 0u);
    SRNOS_HostLinkCoreResetRx(&link);

    static const uint8_t bad_cobs[] = {0u, 3u, 0xA5u, 0u};
    assert(SRNOS_HostLinkCoreFeedRx(&link, bad_cobs, sizeof(bad_cobs)) == SRNOS_HOST_LINK_OK);
    assert(link.stats.cobs_errors == 1u);

    uint8_t too_long[SRNOS_PROTOCOL_MAX_RAW_LEN + 4u];
    memset(too_long, 0x01, sizeof(too_long));
    too_long[0] = 0u;
    too_long[sizeof(too_long) - 1u] = 0u;
    assert(SRNOS_HostLinkCoreFeedRx(&link, too_long, sizeof(too_long)) == SRNOS_HOST_LINK_OK);
    assert(link.stats.rx_overflow != 0u);
}

static void feed_raw_mutation(SRNOS_HostLinkContext *link,
                              const uint8_t *valid_frame,
                              size_t valid_len,
                              size_t raw_offset,
                              uint8_t value)
{
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    size_t raw_len = 0u;
    assert(SRNOS_COBSDecode(&valid_frame[1], valid_len - 2u,
                           raw, sizeof(raw), &raw_len) == SRNOS_COBS_OK);
    raw[raw_offset] = value;
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_COBSPack(raw, raw_len, frame, sizeof(frame), &frame_len) ==
           SRNOS_COBS_OK);
    assert(SRNOS_HostLinkCoreFeedRx(link, frame, frame_len) == SRNOS_HOST_LINK_OK);
}

static void test_protocol_error_classification(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    uint8_t request[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    const size_t request_len = pack_request(SRNOS_HOST_LINK_SYSTEM_HELLO, 5u,
                                            NULL, 0u, request);
    feed_raw_mutation(&link, request, request_len, 1u, 3u);
    feed_raw_mutation(&link, request, request_len, 6u, 1u);
    assert(link.stats.version_errors == 1u);
    assert(link.stats.length_errors == 1u);
}

static void test_request_conflict(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    const uint8_t first[] = {1u};
    const uint8_t second[] = {2u};
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = pack_request(SRNOS_HOST_LINK_SYSTEM_ECHO, 123u,
                                    first, sizeof(first), frame);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    complete_one(&link);
    frame_len = pack_request(SRNOS_HOST_LINK_SYSTEM_ECHO, 123u,
                             second, sizeof(second), frame);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert(link.stats.request_conflict == 1u);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    const SRNOS_ProtocolView response =
        decode_frame(fixture.tx, fixture.tx_len, raw, sizeof(raw));
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_CONFLICT);
    SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);
}

static void test_qos_capacity_priority_and_fairness(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x10u,
                                      SRNOS_HOST_LINK_QOS_CONTROL) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x11u,
                                      SRNOS_HOST_LINK_QOS_TELEMETRY) == SRNOS_HOST_LINK_OK);
    const uint8_t payload = 1u;

    for (uint16_t key = 0u; key < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++key) {
        assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x11u,
                                key, key, &payload, 1u) == SRNOS_HOST_LINK_OK);
    }
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x11u,
                            99u, 99u, &payload, 1u) == SRNOS_HOST_LINK_OK);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_TELEMETRY].drop_old == 1u);

    init_link(&link, &fixture);
    for (uint32_t i = 0u; i < SRNOS_HOST_LINK_TX_SLOT_COUNT; ++i) {
        assert(SRNOS_HostLinkCoreSendResponse(&link, 1u, 1u, i,
                                      SRNOS_HOST_LINK_STATUS_OK, 0u,
                                      NULL, 0u) == SRNOS_HOST_LINK_OK);
    }
    assert(SRNOS_HostLinkCoreSendResponse(&link, 1u, 1u, 99u,
                                  SRNOS_HOST_LINK_STATUS_OK, 0u,
                                  NULL, 0u) == SRNOS_HOST_LINK_ERR_FULL);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_RELIABLE].rejected == 1u);

    init_link(&link, &fixture);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x10u,
                                      SRNOS_HOST_LINK_QOS_CONTROL) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x11u,
                                      SRNOS_HOST_LINK_QOS_TELEMETRY) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreSubmitLog(&link, (const uint8_t *)"line\n", 5u) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreSendResponse(&link, 1u, 1u, 1u,
                                  SRNOS_HOST_LINK_STATUS_OK, 0u, NULL, 0u) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x11u,
                            1u, 1u, &payload, 1u) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCorePublish(&link, SRNOS_HOST_LINK_SERVICE_TEST, 0x10u,
                            1u, 1u, &payload, 1u) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
    assert(link.slots[(size_t)link.in_flight].qos == SRNOS_HOST_LINK_QOS_CONTROL);
    SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);
    bool log_seen = false;
    for (size_t i = 0u; i < 3u; ++i) {
        assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
        if (link.slots[(size_t)link.in_flight].qos == SRNOS_HOST_LINK_QOS_LOG) {
            log_seen = true;
        }
        SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);
    }
    assert(log_seen);
}

static void test_reset_wrap_and_transport_stats(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    SRNOS_HostLinkCoreSetTransportConnected(&link, true);
    assert(SRNOS_HostLinkCoreSubmitLog(&link, (const uint8_t *)"x\n", 2u) == SRNOS_HOST_LINK_OK);
    link.stats.crc_errors = 9u;
    SRNOS_HostLinkCoreResetStats(&link);
    assert(link.stats.crc_errors == 0u);
    assert(link.stats.transport_connected == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_LOG].queue_depth == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_LOG].queue_high_water == 1u);
    complete_one(&link);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_LOG].tx_frames == 1u);
    assert(link.stats.qos[SRNOS_HOST_LINK_QOS_LOG].tx_bytes == 2u);

    fixture.tx_result = SRNOS_HOST_LINK_TX_BUSY;
    assert(SRNOS_HostLinkCoreSubmitLog(&link, (const uint8_t *)"y\n", 2u) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_ERR_BUSY);
    assert(link.stats.transport_busy == 1u);
    fixture.tx_result = SRNOS_HOST_LINK_TX_DISCONNECTED;
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_ERR_TRANSPORT);
    assert(link.stats.transport_disconnects == 1u);
    assert(link.stats.transport_connected == 0u);

    init_link(&link, &fixture);
    uint8_t mailbox_storage[8];
    assert(SRNOS_HostLinkCoreControlRegister(&link, 1u, mailbox_storage,
                                     sizeof(mailbox_storage)) == SRNOS_HOST_LINK_OK);
    uint8_t payload[11] = {0u};
    test_store_le16(payload, 1u);
    test_store_le32(&payload[2], UINT32_MAX);
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_EVENT, 0u,
                              SRNOS_HOST_LINK_SERVICE_CONTROL,
                              SRNOS_HOST_LINK_CONTROL_STATE, UINT32_MAX,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    test_store_le32(&payload[2], 0u);
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_EVENT, 0u,
                              SRNOS_HOST_LINK_SERVICE_CONTROL,
                              SRNOS_HOST_LINK_CONTROL_STATE, 0u,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert(link.stats.sequence_out_of_order == 0u);
    assert(link.stats.sequence_gap == 0u);
}

static void test_qos_snapshot_wire_layout(void)
{
    SRNOS_HostLinkContext link;
    fixture_t fixture;
    init_link(&link, &fixture);
    SRNOS_HostLinkCoreSetTransportConnected(&link, true);
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    const size_t frame_len = pack_request(SRNOS_HOST_LINK_SYSTEM_QOS_GET, 88u,
                                          NULL, 0u, frame);
    assert(SRNOS_HostLinkCoreFeedRx(&link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link) == SRNOS_HOST_LINK_OK);
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    const SRNOS_ProtocolView response =
        decode_frame(fixture.tx, fixture.tx_len, raw, sizeof(raw));
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(response.payload[2] == 1u);
    assert(response.payload[3] == SRNOS_HOST_LINK_QOS_COUNT);
    assert(test_load_le16(&response.payload[4]) == 324u);
    assert(test_load_le32(&response.payload[98]) == 1u);
    SRNOS_HostLinkCoreOnTxComplete(&link, true); SRNOS_HostLinkCoreDrainDeliveries(&link);
}

static uint32_t delivered_count, cancelled_count;
static void delivery(void *user,uint32_t tag,SRNOS_HostLinkDelivery result)
{
    (void)user;assert(critical_depth==0);
    if(result==SRNOS_HOST_LINK_DELIVERED) delivered_count+=tag;
    if(result==SRNOS_HOST_LINK_CANCELLED) cancelled_count+=tag;
}
static SRNOS_COBSStatus count_frame(void *user,const uint8_t *raw,size_t size)
{ (void)raw; (void)size; ++*(unsigned *)user; return SRNOS_COBS_OK; }
static void test_fifo_lifetime_and_copy(void)
{
    SRNOS_HostLinkContext link; fixture_t f; init_link(&link,&f);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,4,2,SRNOS_HOST_LINK_QOS_TELEMETRY)==SRNOS_HOST_LINK_OK);
    uint8_t payload[72]; memset(payload,0x5a,sizeof(payload));
    reset_on_cycles=&link;
    assert(SRNOS_HostLinkCorePublishEx(&link,4,2,0x100,0,payload,sizeof(payload),SRNOS_HOST_LINK_FIFO,NULL,NULL,0)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(!SRNOS_HostLinkCoreHasPendingTx(&link));
    for(unsigned i=0;i<8;i++) assert(SRNOS_HostLinkCorePublishEx(&link,4,2,0x100,i,payload,sizeof(payload),
        SRNOS_HOST_LINK_FIFO,delivery,NULL,1)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCorePublishEx(&link,4,2,0x100,8,payload,sizeof(payload),SRNOS_HOST_LINK_FIFO,NULL,NULL,0)==SRNOS_HOST_LINK_ERR_FULL);
    for(unsigned i=0;i<4;i++) assert(SRNOS_HostLinkCoreSendResponse(&link,0,1,100+i,0,0,NULL,0)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreSendResponse(&link,0,1,105,0,0,NULL,0)==SRNOS_HOST_LINK_ERR_FULL);
    unsigned next=0;
    for(unsigned i=0;i<12;i++) {
        assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_OK);
        uint8_t raw[496]; SRNOS_ProtocolView v=decode_frame(f.tx,f.tx_len,raw,sizeof(raw));
        if(v.header.kind==2) { assert(v.header.sequence==next++); assert(memcmp(v.payload,payload,72)==0); }
        SRNOS_HostLinkCoreOnTxComplete(&link,true); SRNOS_HostLinkCoreDrainDeliveries(&link);
    }
    assert(next==8 && delivered_count==8);
    assert(SRNOS_HostLinkCorePublishEx(&link,4,2,0x100,9,payload,72,SRNOS_HOST_LINK_FIFO,delivery,NULL,6)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_OK);
    int flight=link.in_flight; uint8_t saved[512]; memcpy(saved,link.slots[flight].data,512);
    SRNOS_HostLinkCoreResetSession(&link);
    assert(link.in_flight==flight && memcmp(saved,link.slots[flight].data,512)==0);
    assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_ERR_BUSY);
    SRNOS_HostLinkCoreOnTxComplete(&link,true); SRNOS_HostLinkCoreDrainDeliveries(&link); assert(cancelled_count==6);
    memset(f.tx_storage,0xa7,sizeof(f.tx_storage));
    const uint8_t line[]="A long log must be copied, including the newline\n";
    assert(SRNOS_HostLinkCoreSubmitLog(&link,line,sizeof(line)-1)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_OK);
    assert(f.tx_len==sizeof(line)-1 && memcmp(f.tx,line,sizeof(line)-1)==0);
    SRNOS_HostLinkCoreOnTxComplete(&link,true); SRNOS_HostLinkCoreDrainDeliveries(&link);
    uint8_t request[512]; size_t n=pack_request(SRNOS_HOST_LINK_SYSTEM_HELLO,900,NULL,0,request);
    const uint8_t delimiters[8]={0};
    assert(SRNOS_HostLinkCoreFeedRx(&link,delimiters,8)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreFeedRx(&link,request,n)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_OK);
    memcpy(saved,f.tx,f.tx_len); size_t saved_len=f.tx_len; SRNOS_HostLinkCoreOnTxComplete(&link,true); SRNOS_HostLinkCoreDrainDeliveries(&link);
    memset(f.tx_storage,0x6b,sizeof(f.tx_storage)); /* force another owner's previous contents */
    assert(SRNOS_HostLinkCoreFeedRx(&link,request,n)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(&link)==SRNOS_HOST_LINK_OK);
    assert(f.tx_len==saved_len && memcmp(f.tx,saved,saved_len)==0);
    SRNOS_COBSCtx c; uint8_t encoded[4],raw[8]; unsigned frames=0;
    assert(SRNOS_COBSInit(&c,encoded,4,raw,8,count_frame,&frames)==SRNOS_COBS_OK);
    const uint8_t oversized[]={2,1,2,1,2,1,1,0};
    assert(SRNOS_COBSFeedBytes(&c,oversized,sizeof(oversized))==SRNOS_COBS_OK);
    assert(frames==0 && c.stat_frames_drop_too_long==1);
    const uint8_t valid[]={2,0x42,0}; SRNOS_COBSFeedBytes(&c,valid,sizeof(valid)); assert(frames==1);
    puts("FIFO order/capacity/reserved replies/lifetime; recycled response/log copy; oversized suffix rejection: PASS");
}

static unsigned deferred_calls, immediate_calls;
static uint16_t wait_handler(void *u,const SRNOS_ProtocolView *request,uint8_t *out,
    size_t cap,size_t *len,uint8_t *flags)
{
    (void)u;(void)request;(void)out;(void)cap;*len=0;*flags=0;
    assert(critical_depth==0);++deferred_calls;return SRNOS_HOST_LINK_STATUS_DEFERRED_INTERNAL;
}
static uint16_t immediate_handler(void *u,const SRNOS_ProtocolView *request,uint8_t *out,
    size_t cap,size_t *len,uint8_t *flags)
{
    (void)u;(void)request;(void)out;(void)cap;*len=0;*flags=0;
    assert(critical_depth==0);++immediate_calls;return SRNOS_HOST_LINK_STATUS_OK;
}
static void feed_test_request(SRNOS_HostLinkContext *link,uint32_t sequence,uint8_t payload)
{
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];size_t length=0;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST,0,SRNOS_HOST_LINK_SERVICE_TEST,0x41,
        sequence,&payload,1,frame,sizeof(frame),&length)==SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(link,frame,length)==SRNOS_HOST_LINK_OK);
}
static uint16_t response_status(SRNOS_HostLinkContext *link,fixture_t *fixture)
{
    assert(SRNOS_HostLinkCoreServiceTx(link)==SRNOS_HOST_LINK_OK);
    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    SRNOS_ProtocolView view=decode_frame(fixture->tx,fixture->tx_len,raw,sizeof(raw));
    uint16_t status=test_load_le16(view.payload);
    SRNOS_HostLinkCoreOnTxComplete(link,true);SRNOS_HostLinkCoreDrainDeliveries(link);
    return status;
}
static void test_active_requests_and_congested_response(void)
{
    SRNOS_HostLinkContext link;fixture_t fixture;init_link(&link,&fixture);
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,SRNOS_HOST_LINK_SERVICE_TEST,0x41,1,
        SRNOS_HOST_LINK_QOS_RELIABLE,wait_handler,NULL)==SRNOS_HOST_LINK_OK);
    for(unsigned i=0;i<SRNOS_HOST_LINK_ACTIVE_REQUEST_COUNT-1u;i++)feed_test_request(&link,100+i,1);
    assert(deferred_calls==9);
    fixture.now_ms=5000; /* pending requests outlive the completed-cache TTL */
    for(unsigned i=0;i<8;i++) {
        uint8_t frame[512];size_t length=pack_request(SRNOS_HOST_LINK_SYSTEM_HELLO,900+i,NULL,0,frame);
        assert(SRNOS_HostLinkCoreFeedRx(&link,frame,length)==SRNOS_HOST_LINK_OK);
        assert(response_status(&link,&fixture)==SRNOS_HOST_LINK_STATUS_OK);
    } /* completed-cache churn cannot evict any active command */
    feed_test_request(&link,109,1);assert(deferred_calls==10);
    feed_test_request(&link,100,1);assert(response_status(&link,&fixture)==SRNOS_HOST_LINK_STATUS_ACCEPTED);
    feed_test_request(&link,100,2);assert(response_status(&link,&fixture)==SRNOS_HOST_LINK_STATUS_CONFLICT);
    feed_test_request(&link,200,1);assert(response_status(&link,&fixture)==SRNOS_HOST_LINK_STATUS_BUSY);
    assert(deferred_calls==10);
    assert(SRNOS_HostLinkCoreSendResponse(&link,SRNOS_HOST_LINK_SERVICE_TEST,0x41,100,0,0,NULL,0)==SRNOS_HOST_LINK_OK);
    assert(response_status(&link,&fixture)==0);
    feed_test_request(&link,100,1);assert(response_status(&link,&fixture)==0);assert(deferred_calls==10);
    feed_test_request(&link,200,1);assert(deferred_calls==11);
    SRNOS_HostLinkCoreResetSession(&link);
    feed_test_request(&link,100,1);assert(deferred_calls==12);

    init_link(&link,&fixture);
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,SRNOS_HOST_LINK_SERVICE_TEST,0x41,1,
        SRNOS_HOST_LINK_QOS_RELIABLE,immediate_handler,NULL)==SRNOS_HOST_LINK_OK);
    for(unsigned i=0;i<SRNOS_HOST_LINK_TX_SLOT_COUNT;i++)
        assert(SRNOS_HostLinkCoreSendResponse(&link,0,1,i,0,0,NULL,0)==SRNOS_HOST_LINK_OK);
    feed_test_request(&link,400,1);assert(immediate_calls==1);
    for(unsigned i=0;i<SRNOS_HOST_LINK_TX_SLOT_COUNT;i++)complete_one(&link);
    feed_test_request(&link,400,1);assert(immediate_calls==1);
    assert(response_status(&link,&fixture)==0);
    puts("active 10, no pending TTL eviction, duplicate/conflict, completed replay under congestion: PASS");
}
static void test_registration_and_delivery_boundary(void)
{
    SRNOS_HostLinkContext link;fixture_t f;init_link(&link,&f);
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,0x7e,0x41,1,SRNOS_HOST_LINK_QOS_RELIABLE,wait_handler,NULL)==0);
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,0x7e,0x41,5,SRNOS_HOST_LINK_QOS_RELIABLE,wait_handler,NULL)==SRNOS_HOST_LINK_ERR_PARAM);
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,0x7e,0x41,4,SRNOS_HOST_LINK_QOS_RELIABLE,wait_handler,NULL)==0);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,4,2,SRNOS_HOST_LINK_QOS_TELEMETRY)==0);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,4,2,SRNOS_HOST_LINK_QOS_TELEMETRY)==SRNOS_HOST_LINK_ERR_PARAM);
    link.registrations_frozen=1;
    assert(SRNOS_HostLinkCoreRegisterHandler(&link,0x7e,0x42,1,SRNOS_HOST_LINK_QOS_RELIABLE,wait_handler,NULL)==SRNOS_HOST_LINK_ERR_BUSY);
    assert(SRNOS_HostLinkCoreRegisterTxTopic(&link,4,3,SRNOS_HOST_LINK_QOS_TELEMETRY)==SRNOS_HOST_LINK_ERR_BUSY);
    const uint8_t payload=1;unsigned before=cancelled_count;
    assert(SRNOS_HostLinkCorePublishEx(&link,4,2,0x100,1,&payload,1,SRNOS_HOST_LINK_FIFO,delivery,NULL,3)==0);
    SRNOS_HostLinkCoreResetSession(&link);assert(cancelled_count==before);
    SRNOS_HostLinkCoreDrainDeliveries(&link);assert(cancelled_count==before+3);
    SRNOS_HostLinkCoreDrainDeliveries(&link);assert(cancelled_count==before+3);
    assert(critical_depth==0);
    puts("registration collisions/freeze and exactly-once deferred lock-free delivery: PASS");
}

int main(void)
{
    test_registration_and_delivery_boundary();
    test_active_requests_and_congested_response();
    delivered_count=cancelled_count=0;
    test_fifo_lifetime_and_copy();
    puts("test_crc_and_max_frame"); fflush(stdout);
    test_crc_and_max_frame();
    puts("test_split_stream_and_replay"); fflush(stdout);
    test_split_stream_and_replay();
    puts("test_qos_coalesce_and_control_mailbox"); fflush(stdout);
    test_qos_coalesce_and_control_mailbox();
    puts("test_corruption"); fflush(stdout);
    test_corruption();
    puts("test_all_split_positions_and_framing_errors"); fflush(stdout);
    test_all_split_positions_and_framing_errors();
    puts("test_protocol_error_classification"); fflush(stdout);
    test_protocol_error_classification();
    puts("test_request_conflict"); fflush(stdout);
    test_request_conflict();
    puts("test_qos_capacity_priority_and_fairness"); fflush(stdout);
    test_qos_capacity_priority_and_fairness();
    puts("test_reset_wrap_and_transport_stats"); fflush(stdout);
    test_reset_wrap_and_transport_stats();
    puts("test_qos_snapshot_wire_layout"); fflush(stdout);
    test_qos_snapshot_wire_layout();
    puts("HostLink V2 host tests: PASS");
    return 0;
}
