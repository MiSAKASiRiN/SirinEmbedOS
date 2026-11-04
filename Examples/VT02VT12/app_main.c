#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/Input.h"
#include "SirinEmbedOS/Services/Message.h"
#include "SirinEmbedOS/Services/RemoteControl/VT02/VT02.h"

static SRNOS_MessageRoute custom_route;
/* Watch copies owned by the application's single receive task. */
volatile SRNOS_InputSnapshot vt12_example_input;
volatile SRNOS_MessageInfo vt12_example_custom_info;
volatile uint8_t vt12_example_custom[SRNOS_VT02_CUSTOM_LENGTH];
volatile uint32_t vt12_example_custom_count;

static bool bind_messages(void *user)
{
    (void)user;
    const SRNOS_MessageRouteConfig route = {
        .transports = SRNOS_MESSAGE_VT02, .endpoint = SRNOS_SERIAL_UART9,
        .service = SRNOS_VT02_CUSTOM_SERVICE, .type = SRNOS_VT02_CUSTOM_MESSAGE,
        .max_payload = SRNOS_VT02_CUSTOM_LENGTH, .max_age_ms = 1000,
        .depth = 4, .policy = SRNOS_MESSAGE_FIFO
    };
    return SRNOS_MessageRegister(&route, &custom_route) == SRNOS_MESSAGE_OK;
}

static SRNOS_StepResult receive_step(void *arg, const SRNOS_ThreadWake *wake)
{
    (void)arg; (void)wake;
    SRNOS_InputSnapshot input;
    if (SRNOS_InputReadSnapshot(SRNOS_InputFind("vt02"), &input) == SRNOS_INPUT_OK)
        vt12_example_input = input;
    SRNOS_MessageInfo info;
    uint8_t payload[SRNOS_VT02_CUSTOM_LENGTH];
    for (unsigned count = 0; count < 4; ++count) {
        if (SRNOS_MessageReceive(custom_route, &info, payload, sizeof(payload))
                != SRNOS_MESSAGE_OK) break;
        vt12_example_custom_info = info;
        for (unsigned i = 0; i < sizeof(payload); ++i)
            vt12_example_custom[i] = payload[i];
        ++vt12_example_custom_count;
    }
    return SRNOS_STEP_CONTINUE;
}

/* Application helper: call explicitly from a task with sufficient stack.
 * This example never calls it automatically. No retry or delivery guarantee. */
SRNOS_SerialStatus VT12ExampleSend(uint16_t command, const uint8_t *payload,
                                  size_t length, uint8_t sequence)
{
    SRNOS_SerialStatus status = SRNOS_VT02Send(command, payload, length, sequence, 200);
    return status == SRNOS_SERIAL_OK ? SRNOS_VT02Flush(200) : status;
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->serial_count = 1;
    config->serial[0] = (SRNOS_SerialConfig){
        .port = SRNOS_SERIAL_UART9, .baud_rate = 115200,
        .data_bits = 8, .stop_bits = 1, .parity = SRNOS_SERIAL_PARITY_NONE,
        .mode = SRNOS_SERIAL_DUPLEX, .rx_enabled = true, .tx_enabled = true
    };
    config->remote.vt02_port = SRNOS_SERIAL_UART9;
    config->remote.vt03_port = SRNOS_SERIAL_NONE;
    config->remote.video_link_enabled = false;
    config->referee_port = config->log_port = SRNOS_SERIAL_NONE;
    config->sensors.heater_enabled = false;
    config->ins.enabled = false;
    config->load_parameters = false;
    config->bind_channels = bind_messages;
    config->thread_count = 1;
    config->threads[0] = (SRNOS_ThreadConfig){
        .name = "VT12Receive", .step = receive_step, .stack_bytes = 2048,
        .priority = 24, .mode = SRNOS_TASK_PERIODIC, .period_ms = 10
    };
}

void SRNOS_ApplicationMain(void) {}
