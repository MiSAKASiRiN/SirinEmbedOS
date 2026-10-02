#include "Devices/heater_board.h"
#include "tim.h"
#include <math.h>
bool SRNOS_BoardHeaterInit(bool enabled) {
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,0);
    return !enabled||HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4)==HAL_OK;
}
void SRNOS_BoardHeaterDuty(float duty) {
    if(!isfinite(duty)||duty<0) duty=0;
    if(duty>1) duty=1;
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,
        (uint32_t)(duty*(float)__HAL_TIM_GET_AUTORELOAD(&htim3)));
}
