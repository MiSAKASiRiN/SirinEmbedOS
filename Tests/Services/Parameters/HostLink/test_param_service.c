#include "SirinEmbedOS/Services/Parameters/HostLink/tskptt_param_console.h"

#include "SirinEmbedOS/Services/Parameters/lib_params.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t test_load_le16(const uint8_t *data)
{
    uint16_t value = 0u;
    assert(SRNOS_Endian_LoadLE16(SRNOS_Memory_Bytes(data, 2u), 0u, &value));
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
    uint8_t tx[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t tx_len;
    uint32_t now;
    uint8_t deferred_message;
    uint32_t deferred_sequence;
    SRN_LINK_ALIGN32 uint8_t tx_storage[
        SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE];
} fixture_t;

static uint32_t now_ms(void *user) { return ((fixture_t *)user)->now; }
static srn_link_tx_result_t tx(void *user, const uint8_t *data, size_t len)
{
    fixture_t *fixture = (fixture_t *)user;
    memcpy(fixture->tx, data, len);
    fixture->tx_len = len;
    return SRN_LINK_TX_ACCEPTED;
}
static bool defer(void *user, uint8_t message, uint32_t sequence)
{
    fixture_t *fixture = (fixture_t *)user;
    fixture->deferred_message = message;
    fixture->deferred_sequence = sequence;
    return true;
}

static srn_cobs_prot_view_t decode(const fixture_t *fixture, uint8_t *raw)
{
    size_t raw_len = 0u;
    assert(srn_cobs_decode(&fixture->tx[1], fixture->tx_len - 2u,
                           raw, SRN_COBS_PROT_MAX_RAW_LEN, &raw_len) == SRN_COBS_OK);
    srn_cobs_prot_view_t view;
    assert(srn_cobs_prot_parse(raw, raw_len, &view) == SRN_COBS_PROT_OK);
    return view;
}

static srn_cobs_prot_view_t transact(srn_link_t *link,
                                     fixture_t *fixture,
                                     uint8_t message,
                                     uint32_t sequence,
                                     const uint8_t *payload,
                                     size_t payload_len,
                                     uint8_t *raw)
{
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_CONFIG, message, sequence,
                              payload, payload_len, frame, sizeof(frame),
                              &frame_len) == SRN_COBS_PROT_OK);
    assert(srn_link_feed_rx(link, frame, frame_len) == SRN_LINK_OK);
    assert(srn_link_service_tx(link) == SRN_LINK_OK);
    srn_cobs_prot_view_t response = decode(fixture, raw);
    srn_link_on_tx_complete(link, true);
    return response;
}

static void run_deferred(srn_link_t *link,
                         fixture_t *fixture,
                         uint8_t message,
                         uint32_t sequence,
                         uint8_t *raw)
{
    uint8_t frame[SRN_COBS_PROT_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(srn_cobs_prot_pack(SRN_COBS_PROT_KIND_REQUEST, 0u,
                              SRN_LINK_SERVICE_CONFIG, message, sequence,
                              NULL, 0u, frame, sizeof(frame),
                              &frame_len) == SRN_COBS_PROT_OK);
    assert(srn_link_feed_rx(link, frame, frame_len) == SRN_LINK_OK);
    assert(fixture->deferred_message == message);
    assert(fixture->deferred_sequence == sequence);
    uint8_t body[16];
    size_t body_len = 0u;
    const uint16_t status = TSKPTT_ParamService_RunStorage(
        message, body, sizeof(body), &body_len);
    assert(srn_link_send_response(link, SRN_LINK_SERVICE_CONFIG,
                                  message, sequence, status, 0u,
                                  body, body_len) == SRN_LINK_OK);
    assert(srn_link_service_tx(link) == SRN_LINK_OK);
    const srn_cobs_prot_view_t response = decode(fixture, raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
    srn_link_on_tx_complete(link, true);
}

int main(void)
{
    fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    srn_link_t link;
    const srn_link_config_t link_config = {
        .now_ms = now_ms, .cycles = NULL, .tx_submit = tx,
        .lock = NULL, .unlock = NULL,
        .tx_storage = fixture.tx_storage,
        .tx_storage_size = sizeof(fixture.tx_storage),
        .user = &fixture,
    };
    assert(srn_link_init(&link, &link_config) == SRN_LINK_OK);
    assert(params_init() == PARAMS_STATUS_OK);
    const tskptt_param_service_config_t service_config = {
        .defer = defer, .user = &fixture,
    };
    assert(TSKPTT_ParamService_Register(&link, &service_config) == SRN_LINK_OK);

    uint8_t raw[SRN_COBS_PROT_MAX_RAW_LEN];
    const uint8_t schema_request[] = {0u, 0u, 2u};
    srn_cobs_prot_view_t response = transact(&link, &fixture,
                                             TSKPTT_PARAM_SERVICE_SCHEMA, 1u,
                                             schema_request,
                                             sizeof(schema_request), raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
    assert((response.header.flags & SRN_COBS_PROT_FLAG_MORE) != 0u);
    assert(response.payload[10] == 2u);
    assert(response.payload[13] == PARAMS_TYPE_FLOAT);
    assert(response.payload[35] == PARAMS_TYPE_INT32);

    const uint8_t schema_request_2[] = {2u, 0u, 2u};
    response = transact(&link, &fixture,
                        TSKPTT_PARAM_SERVICE_SCHEMA, 10u,
                        schema_request_2,
                        sizeof(schema_request_2), raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
    assert((response.header.flags & SRN_COBS_PROT_FLAG_MORE) == 0u);
    assert(response.payload[10] == 2u);
    assert(response.payload[13] == PARAMS_TYPE_UINT32);
    assert(response.payload[36] == PARAMS_TYPE_BOOL);

    uint8_t write[5u + (4u + 4u) * 2u];
    test_store_le32(&write[0], params_layout_hash());
    write[4] = 2u;
    test_store_le16(&write[5], 1u);
    write[7] = PARAMS_TYPE_INT32; write[8] = 4u;
    test_store_le32(&write[9], 20u);
    test_store_le16(&write[13], 2u);
    write[15] = PARAMS_TYPE_UINT32; write[16] = 4u;
    test_store_le32(&write[17], 200u);
    uint8_t before[4]; size_t written = 0u;
    assert(params_copy_binary_by_index(1u, PARAMS_BINARY_VALUE_CURRENT,
                                       before, sizeof(before), &written) == PARAMS_STATUS_OK);
    response = transact(&link, &fixture, TSKPTT_PARAM_SERVICE_WRITE, 2u,
                        write, sizeof(write), raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OUT_OF_RANGE);
    uint8_t after[4];
    assert(params_copy_binary_by_index(1u, PARAMS_BINARY_VALUE_CURRENT,
                                       after, sizeof(after), &written) == PARAMS_STATUS_OK);
    assert(memcmp(before, after, sizeof(before)) == 0);

    uint8_t valid[5u + (4u + 4u) * 3u + (4u + 1u)];
    test_store_le32(&valid[0], params_layout_hash());
    valid[4] = 4u;
    size_t pos = 5u;
    const float gain = 2.5f;
    test_store_le16(&valid[pos], 0u);
    valid[pos + 2u] = PARAMS_TYPE_FLOAT; valid[pos + 3u] = 4u;
    memcpy(&valid[pos + 4u], &gain, sizeof(gain)); pos += 8u;
    test_store_le16(&valid[pos], 1u);
    valid[pos + 2u] = PARAMS_TYPE_INT32; valid[pos + 3u] = 4u;
    test_store_le32(&valid[pos + 4u], 20u); pos += 8u;
    test_store_le16(&valid[pos], 2u);
    valid[pos + 2u] = PARAMS_TYPE_UINT32; valid[pos + 3u] = 4u;
    test_store_le32(&valid[pos + 4u], 30u); pos += 8u;
    test_store_le16(&valid[pos], 3u);
    valid[pos + 2u] = PARAMS_TYPE_BOOL; valid[pos + 3u] = 1u;
    valid[pos + 4u] = 0u; pos += 5u;
    assert(pos == sizeof(valid));
    response = transact(&link, &fixture, TSKPTT_PARAM_SERVICE_WRITE, 11u,
                        valid, sizeof(valid), raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);

    const uint8_t read_all[] = {4u, 0u, 0u, 1u, 0u, 2u, 0u, 3u, 0u};
    response = transact(&link, &fixture, TSKPTT_PARAM_SERVICE_READ, 12u,
                        read_all, sizeof(read_all), raw);
    assert(test_load_le16(response.payload) == SRN_LINK_STATUS_OK);
    assert(response.payload[6] == 4u);
    pos = 7u;
    const uint8_t expected_types[] = {
        PARAMS_TYPE_FLOAT, PARAMS_TYPE_INT32, PARAMS_TYPE_UINT32, PARAMS_TYPE_BOOL
    };
    const uint8_t expected_sizes[] = {4u, 4u, 4u, 1u};
    for (uint16_t index = 0u; index < 4u; ++index) {
        assert(test_load_le16(&response.payload[pos]) == index);
        assert(response.payload[pos + 2u] == expected_types[index]);
        assert(response.payload[pos + 3u] == expected_sizes[index]);
        pos += 4u + expected_sizes[index];
    }

    run_deferred(&link, &fixture, TSKPTT_PARAM_SERVICE_SAVE, 13u, raw);
    run_deferred(&link, &fixture, TSKPTT_PARAM_SERVICE_LOAD, 14u, raw);

    assert(srn_link_init(&link,&link_config)==SRN_LINK_OK);
    const tskptt_param_service_config_t disabled={.defer=defer,.user=&fixture,.disable_storage=true};
    assert(TSKPTT_ParamService_Register(&link,&disabled)==SRN_LINK_OK);
    fixture.deferred_message=0;
    response=transact(&link,&fixture,TSKPTT_PARAM_SERVICE_SAVE,20,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRN_LINK_STATUS_UNSUPPORTED_MESSAGE && fixture.deferred_message==0);
    response=transact(&link,&fixture,TSKPTT_PARAM_SERVICE_LOAD,21,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRN_LINK_STATUS_UNSUPPORTED_MESSAGE && fixture.deferred_message==0);
    puts("srn param service V2 host tests: PASS (including disabled storage gate)");
    return 0;
}
