#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/Services/Input.h"
#include "SirinEmbedOS/Services/Message.h"

static SRNOS_MessageRoute custom_messages;
/* Watch copies, written by this example's only application task. */
volatile SRNOS_InputSnapshot srnos_example_inputs[7];
volatile uint32_t srnos_example_messages;
static const char *const source_names[]={"dr16","vt03","wbus",
    "sirinrmt.host","sirinrmt.uart","sirinrmt.udp","sirinrmt.video"};
static SRNOS_StepResult sample(void *arg,const SRNOS_ThreadWake *wake)
{
    (void)arg;(void)wake;
    for(unsigned i=0;i<7;i++) {
        SRNOS_InputSnapshot s;
        if(SRNOS_InputReadSnapshot(SRNOS_InputFind(source_names[i]),&s)==SRNOS_INPUT_OK)
            srnos_example_inputs[i]=s;
    }
    /* Custom payloads do not change an input source's freshness. */
    SRNOS_MessageInfo info;uint8_t payload[20];
    for(unsigned i=0;i<4;i++) {
        if(SRNOS_MessageReceive(custom_messages,&info,payload,sizeof(payload))!=SRNOS_MESSAGE_OK)break;
        ++srnos_example_messages;
    }
    return SRNOS_STEP_CONTINUE;
}
static bool bind(void *user)
{
    (void)user;
    const SRNOS_MessageRouteConfig route={
        .transports=SRNOS_MESSAGE_HOST|SRNOS_MESSAGE_UART|SRNOS_MESSAGE_UDP|SRNOS_MESSAGE_VIDEO,
        .service=0x40,.type=1,.max_payload=20,.max_age_ms=1000,.depth=4,.policy=SRNOS_MESSAGE_FIFO
    };
    return SRNOS_MessageRegister(&route,&custom_messages)==SRNOS_MESSAGE_OK;
}
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c)
{
    /* Example port assignment; choose ports/wiring for the actual installation. */
    c->serial_count=4;
    c->serial[0]=(SRNOS_SerialConfig){.port=SRNOS_SERIAL_UART8,.baud_rate=100000,.data_bits=8,
        .stop_bits=2,.parity=SRNOS_SERIAL_PARITY_EVEN,.mode=SRNOS_SERIAL_DUPLEX,.rx_enabled=true};
    c->serial[1]=(SRNOS_SerialConfig){.port=SRNOS_SERIAL_UART9,.baud_rate=921600,.data_bits=8,
        .stop_bits=1,.parity=SRNOS_SERIAL_PARITY_NONE,.mode=SRNOS_SERIAL_DUPLEX,.rx_enabled=true,.tx_enabled=true};
    c->serial[2]=(SRNOS_SerialConfig){.port=SRNOS_SERIAL_USART6,.baud_rate=100000,.data_bits=8,
        .stop_bits=2,.parity=SRNOS_SERIAL_PARITY_EVEN,.mode=SRNOS_SERIAL_DUPLEX,.rx_enabled=true,.rx_inverted=true};
    c->serial[3]=(SRNOS_SerialConfig){.port=SRNOS_SERIAL_USART1,.baud_rate=115200,.data_bits=8,
        .stop_bits=1,.parity=SRNOS_SERIAL_PARITY_NONE,.mode=SRNOS_SERIAL_DUPLEX,.rx_enabled=true,.tx_enabled=true};
    c->remote.dr16_port=SRNOS_SERIAL_UART8;
    c->remote.vt03_port=SRNOS_SERIAL_UART9;
    c->remote.wbus_port=SRNOS_SERIAL_USART6;
    c->remote.video_link_enabled=true;
    /* RF206S CH5 measured endpoints for this example, not global W.Bus defaults. */
    c->remote.wbus_channels[4]=(SRNOS_InputChannelDescriptor){353,1024,1694,SRNOS_INPUT_CHANNEL_AXIS,false};
    c->messages.host_enabled=true;c->messages.uart_port=SRNOS_SERIAL_USART1;c->messages.udp_port=5001;
    c->ethernet.enabled=c->network.enabled=true;
    c->remote.sirinrmt_transports=SRNOS_MESSAGE_HOST|SRNOS_MESSAGE_UART|SRNOS_MESSAGE_UDP|SRNOS_MESSAGE_VIDEO;
    c->bind_channels=bind;
    c->thread_count=1;
    c->threads[0]=(SRNOS_ThreadConfig){.name="InputInspect",.step=sample,.stack_bytes=2048,
        .priority=24,.mode=SRNOS_TASK_PERIODIC,.period_ms=10};
}
void SRNOS_ApplicationMain(void) {}
