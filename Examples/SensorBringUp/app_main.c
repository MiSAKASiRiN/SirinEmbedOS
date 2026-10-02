#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/IO/CAN/External/ExternalCAN.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include "SirinEmbedOS/IO/SPI/SPI.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
/* Pause and read only when sampling==false. No all-task stack scans in loop. */
typedef struct {
    bool sampling;
    uint32_t sequence, tick_ms;
    uint64_t uptime_us;
    SRNOS_SystemStatus system;
    SRNOS_SensorStatus sensors[4];
    SRNOS_IMUSample bmi, lsm;
    SRNOS_HeaterStatus heater;
    SRNOS_MagneticSample magnetic;
    SRNOS_PressureSample pressure;
    SRNOS_SPISnapshot spi;
    SRNOS_SPIStatistics bmi_acc_spi, bmi_gyro_spi, mcp_spi;
    SRNOS_SensorBusStatistics lsm_spi;
    uint32_t window_samples[4], window_polls[4];
    uint64_t window_us;
    uint32_t free_heap, minimum_heap;
} SensorBringUpSnapshot;
volatile SensorBringUpSnapshot srnos_sensor_bringup;
static SensorBringUpSnapshot snapshot;
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->ins.enabled=false;
    c->sensors.lsm_enabled=c->sensors.lis_enabled=c->sensors.spa_enabled=true;
    c->load_parameters=false;
    c->host_link_enabled=true;
    c->host_link_storage_enabled=false;
    c->host_link_setup=SRNOS_IMUTelemetryRegister;
    c->sensors.imu_observer=SRNOS_IMUTelemetryObserve;
    c->sensors.heater_enabled=true;
    c->sensors.heater_target_c=60.0f;
    c->bringup_indicators=true;
    c->system_monitor_enabled=false; /* Disable background ADC/task monitoring in this measurement example. */
}
static SRNOS_StepResult application_step(void *arg,const SRNOS_ThreadWake *wake)
{
 (void)arg;(void)wake;
 static uint64_t window_start;static uint32_t samples[4],polls[4];
 if(!window_start)window_start=SRNOS_TimeGetUptimeUs();

        snapshot.uptime_us=SRNOS_TimeGetUptimeUs(); snapshot.tick_ms=SRNOS_TimeMs();
        SRNOS_SystemGetStatus(&snapshot.system);
        for(unsigned i=0;i<4;i++) SRNOS_SensorGetStatus((SRNOS_SensorId)i,&snapshot.sensors[i]);
        SRNOS_SensorGetIMU(SRNOS_SENSOR_BMI088,&snapshot.bmi); SRNOS_SensorGetIMU(SRNOS_SENSOR_LSM6DS3,&snapshot.lsm);
        SRNOS_SensorGetMagnetic(&snapshot.magnetic); SRNOS_SensorGetPressure(&snapshot.pressure);
        SRNOS_SPIGetSnapshot(SRNOS_SPI_BUS_4,&snapshot.spi);
        SRNOS_ExternalCANGetSPIStatistics(&snapshot.mcp_spi);
        SRNOS_SensorGetBusStatistics(SRNOS_SENSOR_LSM6DS3,&snapshot.lsm_spi);
        SRNOS_SensorGetBMI088SPIStatistics(&snapshot.bmi_acc_spi,&snapshot.bmi_gyro_spi);
        SRNOS_SensorGetHeaterStatus(&snapshot.heater);
        SRNOS_KernelResources resources;SRNOS_KernelGetResources(&resources);
        snapshot.free_heap=resources.kernel_free_bytes;snapshot.minimum_heap=resources.kernel_minimum_free_bytes;
        if(snapshot.uptime_us-window_start>=10000000) {
            snapshot.window_us=snapshot.uptime_us-window_start;
            for(unsigned i=0;i<4;i++) { snapshot.window_samples[i]=snapshot.sensors[i].samples-samples[i]; snapshot.window_polls[i]=snapshot.sensors[i].polls-polls[i]; samples[i]=snapshot.sensors[i].samples; polls[i]=snapshot.sensors[i].polls; }
            window_start=snapshot.uptime_us;
        }
        ++snapshot.sequence; snapshot.sampling=true; srnos_sensor_bringup.sampling=true; srnos_sensor_bringup=snapshot; srnos_sensor_bringup.sampling=false;
        SRNOS_LOGI("Sensors BMI=%lu LSM=%lu MAG=%lu P=%lu states=%u/%u/%u/%u",(unsigned long)snapshot.sensors[0].samples,(unsigned long)snapshot.sensors[1].samples,(unsigned long)snapshot.sensors[2].samples,(unsigned long)snapshot.sensors[3].samples,snapshot.sensors[0].state,snapshot.sensors[1].state,snapshot.sensors[2].state,snapshot.sensors[3].state);
        SRNOS_LOGI("Heater src=%s T=%.2f target=%.1f duty=%.1f%% fault=%u",
             snapshot.heater.source==1?"LSM":snapshot.heater.source==0?"BMI":"none",
             (double)snapshot.heater.feedback_c,(double)snapshot.heater.target_c,
             (double)(snapshot.heater.duty*100),snapshot.heater.faulted);


 return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result srnos_example_create_result;
static SRNOS_Thread application_task;
void SRNOS_ApplicationMain(void)
{
 const SRNOS_ThreadConfig task={.name="SensorView",.step=application_step,.stack_bytes=4096,.priority=24,.mode=SRNOS_TASK_PERIODIC,.period_ms=1000};
 srnos_example_create_result=SRNOS_ThreadCreate(&task,&application_task);
}
