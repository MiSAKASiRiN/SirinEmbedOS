#include "thread_internal.h"
#include "ins_internal.h"
#include "imu_calibration_internal.h"
#include "Devices/heater_board.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "SirinEmbedOS/Services/HostLink/INSTelemetry.h"
#include "SirinEmbedOS/Core/C/Fault.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"
#include <string.h>
static SRNOS_INSConfig config;
static SRNOS_INSCore core;
void SRNOS_INSPrepare(const SRNOS_INSConfig *c) {
    config=*c;SRNOS_INSSnapshot initial={0};
    initial.status.phase=c->enabled?SRNOS_INS_PROBING:SRNOS_INS_DISABLED;
    initial.attitude.quaternion_wxyz[0]=1;SRNOS_INSPublish(&initial);
}
static void ins_step(void *unused) { (void)unused;
        const uint64_t begin=SRNOS_TimeGetUptimeUs();
        SRNOS_IMUSample imu[2];SRNOS_MagneticSample magnetic;SRNOS_PressureSample pressure;
        SRNOS_SensorGetIMU(SRNOS_SENSOR_BMI088,&imu[0]);SRNOS_SensorGetIMU(SRNOS_SENSOR_LSM6DS3,&imu[1]);
        SRNOS_SensorGetMagnetic(&magnetic);SRNOS_SensorGetPressure(&pressure);
        const uint64_t now=SRNOS_TimeGetUptimeUs();
        SRNOS_HeaterStatus heater=SRNOS_HeaterStep(imu,now);
        SRNOS_BoardHeaterDuty(heater.duty);
        if(core.calibration_pending && SRNOS_IMUCalibrationStartupPoll(SRNOS_TimeMs())) {
            SRNOS_IMUCalibration values[2];
            uint8_t mask=SRNOS_IMUCalibrationCopyActive(values);
            SRNOS_INSCoreSetCalibration(&core,values,mask);
        }
        bool fatal=SRNOS_INSCoreStep(&core,imu,&magnetic,&pressure,&heater,now);
        if(core.snapshot.attitude.flags&SRNOS_INS_VALID) core.snapshot.attitude.published_us=SRNOS_TimeGetUptimeUs();
        SRNOS_INSPublish(&core.snapshot);
        if(fatal) {SRNOS_BoardHeaterDuty(0);SRNOS_Fault_Panic(SRNOS_FAULT_REASON_EXPLICIT_PANIC,"INS: no usable six-axis sensor within 3 seconds",__FILE__,__func__,__LINE__);}
        SRNOS_INSTelemetryObserve(&core.snapshot);
        /* Includes snapshot publication and bounded telemetry copies. The
         * completed cycle's maximum is visible in the next status snapshot. */
        uint32_t elapsed=(uint32_t)(SRNOS_TimeGetUptimeUs()-begin);
        if(elapsed>core.snapshot.status.max_execution_us) core.snapshot.status.max_execution_us=elapsed;

}
static bool ins_wait(void *arg) { TickType_t *last=arg;        /* No catch-up burst when a deadline was missed. */
        TickType_t tick=xTaskGetTickCount();if((TickType_t)(tick-*last)>=pdMS_TO_TICKS(1)) *last=tick;
        vTaskDelayUntil(last,pdMS_TO_TICKS(1));

 return true;
}
void SRNOS_INSWorker(void *arg) {
    (void)arg;const SRNOS_SystemConfig *c=SRNOS_SystemGetConfig();
    bool hw=SRNOS_BoardHeaterInit(c->sensors.heater_enabled);
    SRNOS_HeaterInit(c->sensors.heater_enabled,c->sensors.heater_target_c,hw);
    SRNOS_INSCoreInit(&core,&config,SRNOS_TimeGetUptimeUs());
    core.calibration_pending=true;
    (void)SRNOS_IMUCalibrationStartupBegin(SRNOS_TimeMs());
    TickType_t last=xTaskGetTickCount();
    const SRNOS_InternalLoop loop={ins_step,ins_wait,&last,1,false};
    SRNOS_KernelRunLoop(&loop);
}
