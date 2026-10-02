#include "link_internal.h"
#include "SirinEmbedOS/Services/Parameters/HostLink/ParameterProtocol.h"

#include "SirinEmbedOS/Services/Parameters/Parameters.h"

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
    uint8_t tx[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t tx_len;
    uint32_t now;
    uint8_t deferred_message;
    uint32_t deferred_sequence;
    SRNOS_HOST_LINK_ALIGN32 uint8_t tx_storage[
        SRNOS_HOST_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
} fixture_t;

/* Protocol-only adapter: production registration is checked by HostComm. */
SRNOS_HostLinkResult SRNOS_HostLinkRegisterHandler(SRNOS_HostLinkRegistration *r,
    uint8_t service,uint8_t message,uint8_t kinds,SRNOS_HostLinkQos qos,SRNOS_HostLinkHandlerFn handler,void *user)
{ return SRNOS_HostLinkCoreRegisterHandler((SRNOS_HostLinkContext *)r,service,message,kinds,qos,handler,user); }
static uint32_t now_ms(void *user) { return ((fixture_t *)user)->now; }
static SRNOS_HostLinkCoreTxResult tx(void *user, const uint8_t *data, size_t len)
{
    fixture_t *fixture = (fixture_t *)user;
    memcpy(fixture->tx, data, len);
    fixture->tx_len = len;
    return SRNOS_HOST_LINK_TX_ACCEPTED;
}
static bool defer(void *user, uint8_t message, uint32_t sequence)
{
    fixture_t *fixture = (fixture_t *)user;
    fixture->deferred_message = message;
    fixture->deferred_sequence = sequence;
    return true;
}

static SRNOS_ProtocolView decode(const fixture_t *fixture, uint8_t *raw)
{
    size_t raw_len = 0u;
    assert(SRNOS_COBSDecode(&fixture->tx[1], fixture->tx_len - 2u,
                           raw, SRNOS_PROTOCOL_MAX_RAW_LEN, &raw_len) == SRNOS_COBS_OK);
    SRNOS_ProtocolView view;
    assert(SRNOS_ProtocolParse(raw, raw_len, &view) == SRNOS_PROTOCOL_OK);
    return view;
}

static SRNOS_ProtocolView transact(SRNOS_HostLinkContext *link,
                                     fixture_t *fixture,
                                     uint8_t message,
                                     uint32_t sequence,
                                     const uint8_t *payload,
                                     size_t payload_len,
                                     uint8_t *raw)
{
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_CONFIG, message, sequence,
                              payload, payload_len, frame, sizeof(frame),
                              &frame_len) == SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(link) == SRNOS_HOST_LINK_OK);
    SRNOS_ProtocolView response = decode(fixture, raw);
    SRNOS_HostLinkCoreOnTxComplete(link, true);
    return response;
}

static void run_deferred(SRNOS_HostLinkContext *link,
                         fixture_t *fixture,
                         uint8_t message,
                         uint32_t sequence,
                         uint8_t *raw)
{
    uint8_t frame[SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
    size_t frame_len = 0u;
    assert(SRNOS_ProtocolPack(SRNOS_PROTOCOL_KIND_REQUEST, 0u,
                              SRNOS_HOST_LINK_SERVICE_CONFIG, message, sequence,
                              NULL, 0u, frame, sizeof(frame),
                              &frame_len) == SRNOS_PROTOCOL_OK);
    assert(SRNOS_HostLinkCoreFeedRx(link, frame, frame_len) == SRNOS_HOST_LINK_OK);
    assert(fixture->deferred_message == message);
    assert(fixture->deferred_sequence == sequence);
    uint8_t body[16];
    size_t body_len = 0u;
    const uint16_t status = SRNOS_ParameterProtocolRunStorage(
        message, body, sizeof(body), &body_len);
    assert(SRNOS_HostLinkCoreSendResponse(link, SRNOS_HOST_LINK_SERVICE_CONFIG,
                                  message, sequence, status, 0u,
                                  body, body_len) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_HostLinkCoreServiceTx(link) == SRNOS_HOST_LINK_OK);
    const SRNOS_ProtocolView response = decode(fixture, raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
    SRNOS_HostLinkCoreOnTxComplete(link, true);
}

int main(void)
{
    fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    SRNOS_HostLinkContext link;
    const SRNOS_HostLinkCoreConfig link_config = {
        .now_ms = now_ms, .cycles = NULL, .tx_submit = tx,
        .lock = NULL, .unlock = NULL,
        .tx_storage = fixture.tx_storage,
        .tx_storage_size = sizeof(fixture.tx_storage),
        .user = &fixture,
    };
    assert(SRNOS_HostLinkCoreInit(&link, &link_config) == SRNOS_HOST_LINK_OK);
    assert(SRNOS_ParametersInit() == SRNOS_PARAMETERS_STATUS_OK);
    const SRNOS_ParameterProtocolConfig service_config = {
        .defer = defer, .user = &fixture,
    };
    assert(SRNOS_ParameterProtocolRegister((SRNOS_HostLinkRegistration *)&link, &service_config) == SRNOS_HOST_LINK_OK);
    const SRNOS_ParameterProtocolConfig rejected_policy={.defer=NULL,.user=NULL,.disable_storage=true};
    assert(SRNOS_ParameterProtocolRegister((SRNOS_HostLinkRegistration *)&link,&rejected_policy)!=SRNOS_HOST_LINK_OK);
    /* Existing SAVE/LOAD below must retain the accepted callback and policy. */

    uint8_t raw[SRNOS_PROTOCOL_MAX_RAW_LEN];
    const uint8_t schema_request[] = {0u, 0u, 2u};
    SRNOS_ProtocolView response = transact(&link, &fixture,
                                             SRNOS_PARAMETER_PROTOCOL_SCHEMA, 1u,
                                             schema_request,
                                             sizeof(schema_request), raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert((response.header.flags & SRNOS_PROTOCOL_FLAG_MORE) != 0u);
    assert(response.payload[10] == 2u);
    assert(response.payload[13] == SRNOS_PARAMETERS_TYPE_FLOAT);
    assert(response.payload[35] == SRNOS_PARAMETERS_TYPE_INT32);

    const uint8_t schema_request_2[] = {2u, 0u, 2u};
    response = transact(&link, &fixture,
                        SRNOS_PARAMETER_PROTOCOL_SCHEMA, 10u,
                        schema_request_2,
                        sizeof(schema_request_2), raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert((response.header.flags & SRNOS_PROTOCOL_FLAG_MORE) == 0u);
    assert(response.payload[10] == 2u);
    assert(response.payload[13] == SRNOS_PARAMETERS_TYPE_UINT32);
    assert(response.payload[36] == SRNOS_PARAMETERS_TYPE_BOOL);

    uint8_t write[5u + (4u + 4u) * 2u];
    test_store_le32(&write[0], SRNOS_ParametersLayoutHash());
    write[4] = 2u;
    test_store_le16(&write[5], 1u);
    write[7] = SRNOS_PARAMETERS_TYPE_INT32; write[8] = 4u;
    test_store_le32(&write[9], 20u);
    test_store_le16(&write[13], 2u);
    write[15] = SRNOS_PARAMETERS_TYPE_UINT32; write[16] = 4u;
    test_store_le32(&write[17], 200u);
    uint8_t before[4]; size_t written = 0u;
    assert(SRNOS_ParametersCopyBinaryByIndex(1u, SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,
                                       before, sizeof(before), &written) == SRNOS_PARAMETERS_STATUS_OK);
    response = transact(&link, &fixture, SRNOS_PARAMETER_PROTOCOL_WRITE, 2u,
                        write, sizeof(write), raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OUT_OF_RANGE);
    uint8_t after[4];
    assert(SRNOS_ParametersCopyBinaryByIndex(1u, SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,
                                       after, sizeof(after), &written) == SRNOS_PARAMETERS_STATUS_OK);
    assert(memcmp(before, after, sizeof(before)) == 0);

    test_store_le16(&write[13],1u);write[15]=SRNOS_PARAMETERS_TYPE_INT32;test_store_le32(&write[17],30u);
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_WRITE,3u,write,sizeof(write),raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_INVALID_ARGUMENT);
    assert(SRNOS_ParametersCopyBinaryByIndex(1u,SRNOS_PARAMETERS_BINARY_VALUE_CURRENT,after,sizeof(after),&written)==SRNOS_PARAMETERS_STATUS_OK);
    assert(memcmp(before,after,sizeof(before))==0);

    uint8_t valid[5u + (4u + 4u) * 3u + (4u + 1u)];
    test_store_le32(&valid[0], SRNOS_ParametersLayoutHash());
    valid[4] = 4u;
    size_t pos = 5u;
    const float gain = 2.5f;
    test_store_le16(&valid[pos], 0u);
    valid[pos + 2u] = SRNOS_PARAMETERS_TYPE_FLOAT; valid[pos + 3u] = 4u;
    memcpy(&valid[pos + 4u], &gain, sizeof(gain)); pos += 8u;
    test_store_le16(&valid[pos], 1u);
    valid[pos + 2u] = SRNOS_PARAMETERS_TYPE_INT32; valid[pos + 3u] = 4u;
    test_store_le32(&valid[pos + 4u], 20u); pos += 8u;
    test_store_le16(&valid[pos], 2u);
    valid[pos + 2u] = SRNOS_PARAMETERS_TYPE_UINT32; valid[pos + 3u] = 4u;
    test_store_le32(&valid[pos + 4u], 30u); pos += 8u;
    test_store_le16(&valid[pos], 3u);
    valid[pos + 2u] = SRNOS_PARAMETERS_TYPE_BOOL; valid[pos + 3u] = 1u;
    valid[pos + 4u] = 0u; pos += 5u;
    assert(pos == sizeof(valid));
    response = transact(&link, &fixture, SRNOS_PARAMETER_PROTOCOL_WRITE, 11u,
                        valid, sizeof(valid), raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);

    const uint8_t read_all[] = {4u, 0u, 0u, 1u, 0u, 2u, 0u, 3u, 0u};
    response = transact(&link, &fixture, SRNOS_PARAMETER_PROTOCOL_READ, 12u,
                        read_all, sizeof(read_all), raw);
    assert(test_load_le16(response.payload) == SRNOS_HOST_LINK_STATUS_OK);
    assert(response.payload[6] == 4u);
    pos = 7u;
    const uint8_t expected_types[] = {
        SRNOS_PARAMETERS_TYPE_FLOAT, SRNOS_PARAMETERS_TYPE_INT32, SRNOS_PARAMETERS_TYPE_UINT32, SRNOS_PARAMETERS_TYPE_BOOL
    };
    const uint8_t expected_sizes[] = {4u, 4u, 4u, 1u};
    for (uint16_t index = 0u; index < 4u; ++index) {
        assert(test_load_le16(&response.payload[pos]) == index);
        assert(response.payload[pos + 2u] == expected_types[index]);
        assert(response.payload[pos + 3u] == expected_sizes[index]);
        pos += 4u + expected_sizes[index];
    }

    run_deferred(&link, &fixture, SRNOS_PARAMETER_PROTOCOL_SAVE, 13u, raw);
    run_deferred(&link, &fixture, SRNOS_PARAMETER_PROTOCOL_LOAD, 14u, raw);

    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_STATE,15,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_OK && response.payload_len==34);
    assert(response.payload[2]==1 && response.payload[3]==3 && test_load_le16(response.payload+4)==2);
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_IDENTITY,16,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_OK && response.payload_len==30);
    assert(response.payload[2]==1 && response.payload[3]==1 && !memcmp(response.payload+10,"00123456789012345678",20));
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_STABLE_IDS,17,schema_request,sizeof(schema_request),raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_OK && response.payload[10]==2);
    assert(test_load_le16(response.payload+11)==0 && response.payload[13]==100);
    assert((response.header.flags & SRNOS_PROTOCOL_FLAG_MORE)!=0);
    const uint8_t invalid_request=1;
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_IDENTITY,18,&invalid_request,1,raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_BAD_LENGTH);

    assert(SRNOS_HostLinkCoreInit(&link,&link_config)==SRNOS_HOST_LINK_OK);
    const SRNOS_ParameterProtocolConfig disabled={.defer=defer,.user=&fixture,.disable_storage=true};
    assert(SRNOS_ParameterProtocolRegister((SRNOS_HostLinkRegistration *)&link,&disabled)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_ParameterProtocolRegister((SRNOS_HostLinkRegistration *)&link,&service_config)!=SRNOS_HOST_LINK_OK);
    fixture.deferred_message=0;
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_SAVE,20,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE && fixture.deferred_message==0);
    response=transact(&link,&fixture,SRNOS_PARAMETER_PROTOCOL_LOAD,21,NULL,0,raw);
    assert(test_load_le16(response.payload)==SRNOS_HOST_LINK_STATUS_UNSUPPORTED_MESSAGE && fixture.deferred_message==0);
    puts("SRNOS parameter protocol V2 host tests: PASS (including disabled storage gate)");
    return 0;
}
