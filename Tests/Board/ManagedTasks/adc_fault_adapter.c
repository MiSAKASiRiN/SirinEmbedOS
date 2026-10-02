#include "Devices/monitor_adc_board.h"
#include "SirinEmbedOS/Kernel/Thread.h"

/* Test firmware only; set one command after board_test.stage == 3.
 * All real conversions and stops still execute. No production fault switch.
 * 1: completion reports error; 2: lost completion; 3: task handles it 20 ms
 * late; 4: stop confirmation unavailable (hardware is actually stopped). */
volatile unsigned board_adc_fault;
volatile unsigned board_adc_injected;
bool __real_SRNOS_BoardADCTake(SRNOS_BoardADCResult *out);
bool __real_SRNOS_BoardADCStop(void);
bool __wrap_SRNOS_BoardADCTake(SRNOS_BoardADCResult *out)
{
    unsigned command=board_adc_fault;
    if(command>=1 && command<=3) {
        board_adc_fault=0;++board_adc_injected;
        if(command==3)(void)SRNOS_Delay(20);
    }
    bool ready=__real_SRNOS_BoardADCTake(out);
    if(command==1 && ready)out->error=true;
    return command==2?false:ready;
}
bool __wrap_SRNOS_BoardADCStop(void)
{
    bool stopped=__real_SRNOS_BoardADCStop();
    if(board_adc_fault==4) {
        board_adc_fault=0;++board_adc_injected;return false;
    }
    return stopped;
}
