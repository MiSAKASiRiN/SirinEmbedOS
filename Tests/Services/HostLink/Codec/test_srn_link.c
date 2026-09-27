#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_cobs_prot.h"
#include "SirinEmbedOS/Services/HostLink/Codec/lib_srn_link.h"

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
    uint8_t tx[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t tx_len;
    uint32_t submits;
    srn_link_tx_result_t tx_result;
    SRN_LINK_ALIGN32 uint8_t tx_storage[
        SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE];
} fixture_t;

static uint32_t test_now(void *user)
{
    return ((fixture_t *)user)->now_ms;
}

static srn_link_t *reset_on_cycles;
static uint32_t test_cycles(void *user)
{
    fixture_t *fixture = (fixture_t *)user;
    if(reset_on_cycles) { srn_link_t *link=reset_on_cycles; reset_on_cycles=NULL; srn_link_reset_session(link); }
    fixture->cycles += 17u;
    return fixture->cycles;
}

static srn_link_tx_result_t test_tx(void *user,
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

static void init_link(srn_link_t *link, fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    const srn_link_config_t config = {
        .now_ms = test_now,
        .cycles = test_cycles,
        .tx_submit = test_tx,
        .lock = NULL,
        .unlock = NULL,
        .tx_storage = fixture->tx_storage,
        .tx_storage_size = sizeof(fixture->tx_storage),
        .user = fixture,
    };
    assert(srn_link_init(link, &config) == SRN_LINK_OK);
}

static srn_cobs_prot_view_t decode_frame(const uint8_t *frame,
                                         size_t frame_len,
                                         uint8_t *raw,
                                         size_t raw_cap)
{
    assert(frame_len >= 3u);
    assert(frame[0] == 0u);
    assert(frame[frame_len - 1u] == 0u);
    size_t raw_len = 0u;
    assert(srn_cobs_decode(&frame[1], frame_len - 2u,
                           raw, raw_cap, &raw_len) == SRN_COBS_OK);
    srn_cobs_prot_view_t view;
    assert(srn_cobs_prot_parse(raw, raw_len, &view) == SRN_COBS_PROT_OK);
    return view;
}

static size_t pack_request(uint8_t message,
                           uint32_t sequence,
                           const uint8_t *payload,
                           size_t payload_len,
                           uint8_t *frame)
{
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_SYSTEM, message, sequence,
                              payload, payload_len, frame,
                              SRN_COBS_PROT_FRAME_SLOT_SIZE,
                              &frame_len) == SRN_COBS_PROT_OK);
    return frame_len;
}

static void complete_one(srn_link_t *link)
{
    assert(srn_link_service_tx(link) == SRN_LINK_OK);
    srn_link_on_tx_complete(link, true);
}

static void test_crc_and_max_frame(void)
{
    static const uint8_t check[] = "123456789";
    assert(srn_cobs_prot_crc32(check, sizeof(check) - 1u) == 0xCBF43926u);

    static const uint8_t hello_golden[] = {
        0x00u, 0x03u, 0xA5u, 0x02u, 0x01u, 0x01u, 0x02u, 0x01u,
        0x01u, 0x09u, 0x04u, 0x03u, 0x02u, 0x01u, 0x08u, 0xCCu,
        0x8Eu, 0xADu, 0x00u,
    };
    uint8_t hello[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t hello_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_SYSTEM,
                              SRN_LINK_SYSTEM_HELLO, 0x01020304u,
                              NULL, 0u, hello, sizeof(hello),
                              &hello_len) == SRN_COBS_PROT_OK);
    assert(hello_len == sizeof(hello_golden));
    assert(memcmp(hello, hello_golden, sizeof(hello_golden)) == 0);

    uint8_t payload[SRN_COBS_PROT_MAX_PAYLOAD];
    memset(payload, 0x55, sizeof(payload));
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_EVENT, 0u,
                              SRN_LINK_SERVICE_TELEMETRY, 1u, 7u,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRN_COBS_PROT_OK);
    assert(frame_len <= SRN_COBS_PROT_MAX_FRAME_LEN);
    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    srn_cobs_prot_view_t view = decode_frame(frame, frame_len, raw, sizeof(raw));
    assert(view.payload_len == sizeof(payload));
    assert(memcmp(view.payload, payload, sizeof(payload)) == 0);
}

static void test_split_stream_and_replay(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);

    uint8_t request[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t request_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_SYSTEM,
                              SRN_LINK_SYSTEM_HELLO, 0x10203040u,
                              NULL, 0u, request, sizeof(request),
                              &request_len) == SRN_COBS_PROT_OK);

    static const uint8_t ascii[] = "log-before-frame\r\n";
    assert(srn_link_feed_rx(&link, ascii, sizeof(ascii) - 1u) == SRN_LINK_OK);
    for (size_t i = 0u; i < request_len; ++i) {
        assert(srn_link_feed_rx(&link, &request[i], 1u) == SRN_LINK_OK);
    }
    assert(link.stats.rx_frames == 1u);
    assert(srn_link_service_tx(&link) == SRN_LINK_OK);
    uint8_t first_response[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    const size_t first_len = fixture.tx_len;
    memcpy(first_response, fixture.tx, first_len);
    srn_link_on_tx_complete(&link, true);

    assert(srn_link_feed_rx(&link, request, request_len) == SRN_LINK_OK);
    assert(link.stats.request_replay == 1u);
    assert(srn_link_service_tx(&link) == SRN_LINK_OK);
    assert(fixture.tx_len == first_len);
    assert(memcmp(fixture.tx, first_response, first_len) == 0);
    srn_link_on_tx_complete(&link, true);

    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    const srn_cobs_prot_view_t response =
        decode_frame(first_response, first_len, raw, sizeof(raw));
    assert(response.header.kind == SRN_COBS_PROT_KIND_RESPONSE);
    assert(response.header.sequence == 0x10203040u);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
}

static void test_qos_coalesce_and_control_mailbox(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    assert(srn_link_register_tx_topic(&link,
                                      SRN_LINK_SERVICE_TELEMETRY,
                                      0x20u,
                                      SRN_LINK_QOS_TELEMETRY) == SRN_LINK_OK);
    assert(srn_link_register_tx_topic(&link,
                                      SRN_LINK_SERVICE_TELEMETRY,
                                      0x21u,
                                      SRN_LINK_QOS_TELEMETRY) == SRN_LINK_OK);
    const uint8_t one[] = {1u};
    const uint8_t two[] = {2u};
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TELEMETRY, 0x20u,
                            9u, 1u, one, sizeof(one)) == SRN_LINK_OK);
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TELEMETRY, 0x20u,
                            9u, 2u, two, sizeof(two)) == SRN_LINK_OK);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].queue_depth == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].coalesced == 1u);
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TELEMETRY, 0x21u,
                            9u, 3u, one, sizeof(one)) == SRN_LINK_OK);
    assert(srn_link_send_response(&link, SRN_LINK_SERVICE_TELEMETRY, 0x20u,
                                  4u, SRN_LINK_STATUS_OK, 0u, NULL, 0u) ==
           SRN_LINK_OK);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].queue_depth == 2u);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].coalesced == 1u);
    assert(srn_link_discard_topic(&link, SRN_LINK_SERVICE_TELEMETRY,
                                  0x20u) == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].queue_depth == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].drop_old == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_RELIABLE].queue_depth == 1u);
    assert(srn_link_discard_topic(&link, SRN_LINK_SERVICE_TELEMETRY,
                                  0x20u) == 0u);
    const uint32_t cycles_before = link.stats.processing_cycles_total;
    srn_link_record_processing_cycles(&link, 123u);
    assert(link.stats.processing_cycles_total == cycles_before + 123u);
    assert(link.stats.processing_cycles_max >= 123u);

    uint8_t mailbox_storage[32];
    assert(srn_link_control_register(&link, 3u, mailbox_storage,
                                     sizeof(mailbox_storage)) == SRN_LINK_OK);
    uint8_t control_payload[13];
    test_store_le16(&control_payload[0], 3u);
    test_store_le32(&control_payload[2], 8u);
    test_store_le32(&control_payload[6], 5000u);
    control_payload[10] = 0xAAu;
    control_payload[11] = 0xBBu;
    control_payload[12] = 0xCCu;
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_EVENT, 0u,
                              SRN_LINK_SERVICE_CONTROL,
                              SRN_LINK_CONTROL_STATE, 1u,
                              control_payload, sizeof(control_payload),
                              frame, sizeof(frame), &frame_len) == SRN_COBS_PROT_OK);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    uint8_t out[8];
    size_t out_len = 0u;
    uint32_t generation = 0u;
    assert(srn_link_control_take(&link, 3u, out, sizeof(out), &out_len,
                                 &generation, NULL, NULL) == SRN_LINK_OK);
    assert(out_len == 3u);
    assert(generation == 8u);
    assert(memcmp(out, &control_payload[10], 3u) == 0);
}

static void test_corruption(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_SYSTEM,
                              SRN_LINK_SYSTEM_ECHO, 1u,
                              (const uint8_t *)"abc", 3u,
                              frame, sizeof(frame), &frame_len) == SRN_COBS_PROT_OK);
    frame[frame_len / 2u] ^= 0x20u;
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    assert((link.stats.crc_errors + link.stats.cobs_errors) != 0u);
    assert(link.stats.rx_frames == 0u);
}

static void test_all_split_positions_and_framing_errors(void)
{
    uint8_t request[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    const size_t request_len = pack_request(SRN_LINK_SYSTEM_HELLO, 77u,
                                            NULL, 0u, request);
    for (size_t split = 0u; split <= request_len; ++split) {
        srn_link_t link;
        fixture_t fixture;
        init_link(&link, &fixture);
        assert(srn_link_feed_rx(&link, request, split) == SRN_LINK_OK);
        assert(srn_link_feed_rx(&link, &request[split], request_len - split) ==
               SRN_LINK_OK);
        assert(link.stats.rx_frames == 1u);
    }

    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    static const uint8_t delimiters[] = {0u, 0u, 0u, 0u};
    assert(srn_link_feed_rx(&link, delimiters, sizeof(delimiters)) == SRN_LINK_OK);
    assert(link.stats.rx_frames == 0u);

    assert(srn_link_feed_rx(&link, request, request_len - 1u) == SRN_LINK_OK);
    assert(link.stats.rx_frames == 0u);
    srn_link_reset_rx(&link);

    static const uint8_t bad_cobs[] = {0u, 3u, 0xA5u, 0u};
    assert(srn_link_feed_rx(&link, bad_cobs, sizeof(bad_cobs)) == SRN_LINK_OK);
    assert(link.stats.cobs_errors == 1u);

    uint8_t too_long[SRN_COBS_PROT_MAX_RAW_LEN + 4u];
    memset(too_long, 0x01, sizeof(too_long));
    too_long[0] = 0u;
    too_long[sizeof(too_long) - 1u] = 0u;
    assert(srn_link_feed_rx(&link, too_long, sizeof(too_long)) == SRN_LINK_OK);
    assert(link.stats.rx_overflow != 0u);
}

static void feed_raw_mutation(srn_link_t *link,
                              const uint8_t *valid_frame,
                              size_t valid_len,
                              size_t raw_offset,
                              uint8_t value)
{
    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    size_t raw_len = 0u;
    assert(srn_cobs_decode(&valid_frame[1], valid_len - 2u,
                           raw, sizeof(raw), &raw_len) == SRN_COBS_OK);
    raw[raw_offset] = value;
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_pack(raw, raw_len, frame, sizeof(frame), &frame_len) ==
           SRN_COBS_OK);
    assert(srn_link_feed_rx(link, frame, frame_len) == SRN_LINK_OK);
}

static void test_protocol_error_classification(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    uint8_t request[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    const size_t request_len = pack_request(SRN_LINK_SYSTEM_HELLO, 5u,
                                            NULL, 0u, request);
    feed_raw_mutation(&link, request, request_len, 1u, 3u);
    feed_raw_mutation(&link, request, request_len, 6u, 1u);
    assert(link.stats.version_errors == 1u);
    assert(link.stats.length_errors == 1u);
}

static void test_request_conflict(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    const uint8_t first[] = {1u};
    const uint8_t second[] = {2u};
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = pack_request(SRN_LINK_SYSTEM_ECHO, 123u,
                                    first, sizeof(first), frame);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    complete_one(&link);
    frame_len = pack_request(SRN_LINK_SYSTEM_ECHO, 123u,
                             second, sizeof(second), frame);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    assert(link.stats.request_conflict == 1u);
    assert(srn_link_service_tx(&link) == SRN_LINK_OK);
    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    const srn_cobs_prot_view_t response =
        decode_frame(fixture.tx, fixture.tx_len, raw, sizeof(raw));
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_CONFLICT);
    srn_link_on_tx_complete(&link, true);
}

static void test_qos_capacity_priority_and_fairness(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    assert(srn_link_register_tx_topic(&link, SRN_LINK_SERVICE_TEST, 0x10u,
                                      SRN_LINK_QOS_CONTROL) == SRN_LINK_OK);
    assert(srn_link_register_tx_topic(&link, SRN_LINK_SERVICE_TEST, 0x11u,
                                      SRN_LINK_QOS_TELEMETRY) == SRN_LINK_OK);
    const uint8_t payload = 1u;

    for (uint16_t key = 0u; key < SRN_LINK_TX_SLOT_COUNT; ++key) {
        assert(srn_link_publish(&link, SRN_LINK_SERVICE_TEST, 0x11u,
                                key, key, &payload, 1u) == SRN_LINK_OK);
    }
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TEST, 0x11u,
                            99u, 99u, &payload, 1u) == SRN_LINK_OK);
    assert(link.stats.qos[SRN_LINK_QOS_TELEMETRY].drop_old == 1u);

    init_link(&link, &fixture);
    for (uint32_t i = 0u; i < SRN_LINK_TX_SLOT_COUNT; ++i) {
        assert(srn_link_send_response(&link, 1u, 1u, i,
                                      SRN_LINK_STATUS_OK, 0u,
                                      NULL, 0u) == SRN_LINK_OK);
    }
    assert(srn_link_send_response(&link, 1u, 1u, 99u,
                                  SRN_LINK_STATUS_OK, 0u,
                                  NULL, 0u) == SRN_LINK_ERR_FULL);
    assert(link.stats.qos[SRN_LINK_QOS_RELIABLE].rejected == 1u);

    init_link(&link, &fixture);
    assert(srn_link_register_tx_topic(&link, SRN_LINK_SERVICE_TEST, 0x10u,
                                      SRN_LINK_QOS_CONTROL) == SRN_LINK_OK);
    assert(srn_link_register_tx_topic(&link, SRN_LINK_SERVICE_TEST, 0x11u,
                                      SRN_LINK_QOS_TELEMETRY) == SRN_LINK_OK);
    assert(srn_link_submit_log(&link, (const uint8_t *)"line\n", 5u) == SRN_LINK_OK);
    assert(srn_link_send_response(&link, 1u, 1u, 1u,
                                  SRN_LINK_STATUS_OK, 0u, NULL, 0u) == SRN_LINK_OK);
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TEST, 0x11u,
                            1u, 1u, &payload, 1u) == SRN_LINK_OK);
    assert(srn_link_publish(&link, SRN_LINK_SERVICE_TEST, 0x10u,
                            1u, 1u, &payload, 1u) == SRN_LINK_OK);
    assert(srn_link_service_tx(&link) == SRN_LINK_OK);
    assert(link.slots[(size_t)link.in_flight].qos == SRN_LINK_QOS_CONTROL);
    srn_link_on_tx_complete(&link, true);
    bool log_seen = false;
    for (size_t i = 0u; i < 3u; ++i) {
        assert(srn_link_service_tx(&link) == SRN_LINK_OK);
        if (link.slots[(size_t)link.in_flight].qos == SRN_LINK_QOS_LOG) {
            log_seen = true;
        }
        srn_link_on_tx_complete(&link, true);
    }
    assert(log_seen);
}

static void test_reset_wrap_and_transport_stats(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    srn_link_set_transport_connected(&link, true);
    assert(srn_link_submit_log(&link, (const uint8_t *)"x\n", 2u) == SRN_LINK_OK);
    link.stats.crc_errors = 9u;
    srn_link_reset_stats(&link);
    assert(link.stats.crc_errors == 0u);
    assert(link.stats.transport_connected == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_LOG].queue_depth == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_LOG].queue_high_water == 1u);
    complete_one(&link);
    assert(link.stats.qos[SRN_LINK_QOS_LOG].tx_frames == 1u);
    assert(link.stats.qos[SRN_LINK_QOS_LOG].tx_bytes == 2u);

    fixture.tx_result = SRN_LINK_TX_BUSY;
    assert(srn_link_submit_log(&link, (const uint8_t *)"y\n", 2u) == SRN_LINK_OK);
    assert(srn_link_service_tx(&link) == SRN_LINK_ERR_BUSY);
    assert(link.stats.transport_busy == 1u);
    fixture.tx_result = SRN_LINK_TX_DISCONNECTED;
    assert(srn_link_service_tx(&link) == SRN_LINK_ERR_TRANSPORT);
    assert(link.stats.transport_disconnects == 1u);
    assert(link.stats.transport_connected == 0u);

    init_link(&link, &fixture);
    uint8_t mailbox_storage[8];
    assert(srn_link_control_register(&link, 1u, mailbox_storage,
                                     sizeof(mailbox_storage)) == SRN_LINK_OK);
    uint8_t payload[11] = {0u};
    test_store_le16(payload, 1u);
    test_store_le32(&payload[2], UINT32_MAX);
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_EVENT, 0u,
                              SRN_LINK_SERVICE_CONTROL,
                              SRN_LINK_CONTROL_STATE, UINT32_MAX,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRN_COBS_PROT_OK);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    test_store_le32(&payload[2], 0u);
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_EVENT, 0u,
                              SRN_LINK_SERVICE_CONTROL,
                              SRN_LINK_CONTROL_STATE, 0u,
                              payload, sizeof(payload), frame,
                              sizeof(frame), &frame_len) == SRN_COBS_PROT_OK);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    assert(link.stats.sequence_out_of_order == 0u);
    assert(link.stats.sequence_gap == 0u);
}

static void test_qos_snapshot_wire_layout(void)
{
    srn_link_t link;
    fixture_t fixture;
    init_link(&link, &fixture);
    srn_link_set_transport_connected(&link, true);
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    const size_t frame_len = pack_request(SRN_LINK_SYSTEM_QOS_GET, 88u,
                                          NULL, 0u, frame);
    assert(srn_link_feed_rx(&link, frame, frame_len) == SRN_LINK_OK);
    assert(srn_link_service_tx(&link) == SRN_LINK_OK);
    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    const srn_cobs_prot_view_t response =
        decode_frame(fixture.tx, fixture.tx_len, raw, sizeof(raw));
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
    assert(response.payload[2] == 1u);
    assert(response.payload[3] == SRN_LINK_QOS_COUNT);
    assert(test_load_le16(&response.payload[4]) == 324u);
    assert(test_load_le32(&response.payload[98]) == 1u);
    srn_link_on_tx_complete(&link, true);
}

static uint32_t delivered_count, cancelled_count;
static void delivery(void *user,uint32_t tag,srn_link_delivery_t result)
{
    (void)user;
    if(result==SRN_LINK_DELIVERED) delivered_count+=tag;
    if(result==SRN_LINK_CANCELLED) cancelled_count+=tag;
}
static srn_cobs_status_t count_frame(void *user,const uint8_t *raw,size_t size)
{ (void)raw; (void)size; ++*(unsigned *)user; return SRN_COBS_OK; }
static void test_fifo_lifetime_and_copy(void)
{
    srn_link_t link; fixture_t f; init_link(&link,&f);
    assert(srn_link_register_tx_topic(&link,4,2,SRN_LINK_QOS_TELEMETRY)==SRN_LINK_OK);
    uint8_t payload[72]; memset(payload,0x5a,sizeof(payload));
    reset_on_cycles=&link;
    assert(srn_link_publish_ex(&link,4,2,0x100,0,payload,sizeof(payload),SRN_LINK_FIFO,NULL,NULL,0)==SRN_LINK_ERR_BUSY);
    assert(!srn_link_has_pending_tx(&link));
    for(unsigned i=0;i<8;i++) assert(srn_link_publish_ex(&link,4,2,0x100,i,payload,sizeof(payload),
        SRN_LINK_FIFO,delivery,NULL,1)==SRN_LINK_OK);
    assert(srn_link_publish_ex(&link,4,2,0x100,8,payload,sizeof(payload),SRN_LINK_FIFO,NULL,NULL,0)==SRN_LINK_ERR_FULL);
    for(unsigned i=0;i<4;i++) assert(srn_link_send_response(&link,0,1,100+i,0,0,NULL,0)==SRN_LINK_OK);
    assert(srn_link_send_response(&link,0,1,105,0,0,NULL,0)==SRN_LINK_ERR_FULL);
    unsigned next=0;
    for(unsigned i=0;i<12;i++) {
        assert(srn_link_service_tx(&link)==SRN_LINK_OK);
        uint8_t raw[496]; srn_cobs_prot_view_t v=decode_frame(f.tx,f.tx_len,raw,sizeof(raw));
        if(v.header.kind==2) { assert(v.header.sequence==next++); assert(memcmp(v.payload,payload,72)==0); }
        srn_link_on_tx_complete(&link,true);
    }
    assert(next==8 && delivered_count==8);
    assert(srn_link_publish_ex(&link,4,2,0x100,9,payload,72,SRN_LINK_FIFO,delivery,NULL,6)==SRN_LINK_OK);
    assert(srn_link_service_tx(&link)==SRN_LINK_OK);
    int flight=link.in_flight; uint8_t saved[512]; memcpy(saved,link.slots[flight].data,512);
    srn_link_reset_session(&link);
    assert(link.in_flight==flight && memcmp(saved,link.slots[flight].data,512)==0);
    assert(srn_link_service_tx(&link)==SRN_LINK_ERR_BUSY);
    srn_link_on_tx_complete(&link,true); assert(cancelled_count==6);
    memset(f.tx_storage,0xa7,sizeof(f.tx_storage));
    const uint8_t line[]="A long log must be copied, including the newline\n";
    assert(srn_link_submit_log(&link,line,sizeof(line)-1)==SRN_LINK_OK);
    assert(srn_link_service_tx(&link)==SRN_LINK_OK);
    assert(f.tx_len==sizeof(line)-1 && memcmp(f.tx,line,sizeof(line)-1)==0);
    srn_link_on_tx_complete(&link,true);
    uint8_t request[512]; size_t n=pack_request(SRN_LINK_SYSTEM_HELLO,900,NULL,0,request);
    const uint8_t delimiters[8]={0};
    assert(srn_link_feed_rx(&link,delimiters,8)==SRN_LINK_OK);
    assert(srn_link_feed_rx(&link,request,n)==SRN_LINK_OK);
    assert(srn_link_service_tx(&link)==SRN_LINK_OK);
    memcpy(saved,f.tx,f.tx_len); size_t saved_len=f.tx_len; srn_link_on_tx_complete(&link,true);
    memset(f.tx_storage,0x6b,sizeof(f.tx_storage)); /* force another owner's previous contents */
    assert(srn_link_feed_rx(&link,request,n)==SRN_LINK_OK);
    assert(srn_link_service_tx(&link)==SRN_LINK_OK);
    assert(f.tx_len==saved_len && memcmp(f.tx,saved,saved_len)==0);
    srn_cobs_ctx_t c; uint8_t encoded[4],raw[8]; unsigned frames=0;
    assert(srn_cobs_init(&c,encoded,4,raw,8,count_frame,&frames)==SRN_COBS_OK);
    const uint8_t oversized[]={2,1,2,1,2,1,1,0};
    assert(srn_cobs_feed_bytes(&c,oversized,sizeof(oversized))==SRN_COBS_OK);
    assert(frames==0 && c.stat_frames_drop_too_long==1);
    const uint8_t valid[]={2,0x42,0}; srn_cobs_feed_bytes(&c,valid,sizeof(valid)); assert(frames==1);
    puts("FIFO order/capacity/reserved replies/lifetime; recycled response/log copy; oversized suffix rejection: PASS");
}

int main(void)
{
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
    puts("srn_link_v2 host tests: PASS");
    return 0;
}
