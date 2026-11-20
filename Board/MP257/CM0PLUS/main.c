#include "SirinEmbedOS/IO/MP257/WBusReceiver.h"

int main(void)
{
    SRNOS_MP257WBusReceiverInit();
    for (;;) {
        SRNOS_MP257WBusReceiverWaitAndPoll();
    }
}
