#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/IO/CAN/FD/CANFD.h"

/* The default example only receives. A parent may opt into one test frame
 * with a target-local compile definition; this is not a runtime parameter. */
#ifndef SRNOS_CANFD_EXAMPLE_SEND_ONCE
#define SRNOS_CANFD_EXAMPLE_SEND_ONCE 0
#endif
#define SRNOS_CANFD_EXAMPLE_RX_ID 0x1fff0123u
#define SRNOS_CANFD_EXAMPLE_TX_ID 0x1fff0124u

/* Debugger observations. Only the CAN1 RX worker writes the RX fields; these
 * are not a synchronized interface for other application tasks. */
volatile uint32_t SRNOS_CANFDExampleRxCount;
volatile SRNOS_CANFDFrame SRNOS_CANFDExampleLastRx;
volatile SRNOS_CANFDResult SRNOS_CANFDExampleBindResult;
volatile bool SRNOS_CANFDExampleConfigRead;
volatile SRNOS_CANControllerConfig SRNOS_CANFDExampleConfig;
volatile SRNOS_SystemStatus SRNOS_CANFDExampleStatus;
volatile bool SRNOS_CANFDExampleStatsRead;
volatile SRNOS_CANFDStats SRNOS_CANFDExampleStats;
volatile bool SRNOS_CANFDExamplePostAttempted;
volatile SRNOS_CANFDResult SRNOS_CANFDExamplePostResult;

static void SRNOS_CANFDExampleReceive(SRNOS_CANChannel channel,
                                     const SRNOS_CANFDFrame *frame, void *user)
{
    (void)channel;
    (void)user;
    SRNOS_CANFDExampleLastRx = *frame;
    ++SRNOS_CANFDExampleRxCount;
}

static bool SRNOS_CANFDExampleBind(void *user)
{
    (void)user;
    const SRNOS_CANRxFilter filter = {
        .id = SRNOS_CANFD_EXAMPLE_RX_ID,
        .flags = SRNOS_CAN_FRAME_FLAG_EXT,
    };
    const SRNOS_CANFDResult result = SRNOS_CANFDSubscribeRaw(
        SRNOS_CAN_CH_NTFDCAN1, filter, SRNOS_CANFDExampleReceive, NULL);
    SRNOS_CANFDExampleBindResult = result;
    return result == SRNOS_CANFD_OK;
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    for (unsigned i = 0u; i < SRNOS_CAN_CONTROLLER_COUNT; ++i) {
        config->can[i] = (SRNOS_CANControllerConfig){
            .mode = SRNOS_CAN_MODE_CLASSIC, .nominal_bitrate = 1000000u,
            .data_bitrate = 0u, .brs = false,
        };
    }
    config->can[0] = (SRNOS_CANControllerConfig){
        .mode = SRNOS_CAN_MODE_FD, .nominal_bitrate = 1000000u,
        .data_bitrate = 4000000u, .brs = true,
    };
    config->bind_channels = SRNOS_CANFDExampleBind;
    config->bind_channels_user = NULL;
}

void SRNOS_ApplicationMain(void)
{
    SRNOS_CANControllerConfig config;
    SRNOS_SystemStatus status;
    SRNOS_CANFDStats stats;
    SRNOS_SystemGetStatus(&status);
    SRNOS_CANFDExampleStatus = status;
    SRNOS_CANFDExampleConfigRead = SRNOS_CANControllerGetConfig(SRNOS_CAN_CH_NTFDCAN1, &config);
    if (SRNOS_CANFDExampleConfigRead) SRNOS_CANFDExampleConfig = config;
    SRNOS_CANFDExampleStatsRead = SRNOS_CANFDGetStats(SRNOS_CAN_CH_NTFDCAN1, &stats);
    if (SRNOS_CANFDExampleStatsRead) SRNOS_CANFDExampleStats = stats;

#if SRNOS_CANFD_EXAMPLE_SEND_ONCE
    if (status.native_can_ready && SRNOS_CANFDExampleConfigRead) {
        SRNOS_CANFDFrame frame = {
            .id = SRNOS_CANFD_EXAMPLE_TX_ID,
            .length = 12u,
            .flags = SRNOS_CANFD_FRAME_FLAG_EXT | SRNOS_CANFD_FRAME_FLAG_BRS,
            .data = {'S', 'R', 'N', 'O', 'S', '-', 'F', 'D', 1u, 2u, 3u, 4u},
        };
        SRNOS_CANFDExamplePostAttempted = true;
        SRNOS_CANFDExamplePostResult = SRNOS_CANFDPostOneshot(
            SRNOS_CAN_CH_NTFDCAN1, &frame, 1u, 20u);
    }
#endif
    /* No application loop or extra task; the registered SDK RX callback stays active. */
}
