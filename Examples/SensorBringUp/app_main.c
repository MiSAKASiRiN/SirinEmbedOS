#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/HostLink/IMUTelemetry.h"
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_spi.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/Devices/IMU/BMI088/bsp_bmi088.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
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
    bsp_spi_snapshot_t spi;
    bsp_spi_statistics_t bmi_acc_spi, bmi_gyro_spi, mcp_spi;
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
    c->diagnostics_enabled=false; /* No periodic stack scanning in measurement. */
}
void SRNOS_ApplicationMain(void) {
    uint64_t window_start=DWT_GetUptimeUs(); uint32_t samples[4]={0},polls[4]={0};
    TickType_t last=xTaskGetTickCount();
    for(;;) {
        snapshot.uptime_us=DWT_GetUptimeUs(); snapshot.tick_ms=HAL_GetTick();
        SRNOS_SystemGetStatus(&snapshot.system);
        for(unsigned i=0;i<4;i++) SRNOS_SensorGetStatus((SRNOS_SensorId)i,&snapshot.sensors[i]);
        SRNOS_SensorGetIMU(SRNOS_SENSOR_BMI088,&snapshot.bmi); SRNOS_SensorGetIMU(SRNOS_SENSOR_LSM6DS3,&snapshot.lsm);
        SRNOS_SensorGetMagnetic(&snapshot.magnetic); SRNOS_SensorGetPressure(&snapshot.pressure);
        bsp_spi_get_snapshot(BSP_SPI_BUS_4,&snapshot.spi);
        DRV_SPI_GetStatistics(0,&snapshot.mcp_spi);
        SRNOS_SensorGetBusStatistics(SRNOS_SENSOR_LSM6DS3,&snapshot.lsm_spi);
        BSP_BMI088_GetSPIStatistics(&snapshot.bmi_acc_spi,&snapshot.bmi_gyro_spi);
        SRNOS_SensorGetHeaterStatus(&snapshot.heater);
        snapshot.free_heap=xPortGetFreeHeapSize(); snapshot.minimum_heap=xPortGetMinimumEverFreeHeapSize();
        if(snapshot.uptime_us-window_start>=10000000) {
            snapshot.window_us=snapshot.uptime_us-window_start;
            for(unsigned i=0;i<4;i++) { snapshot.window_samples[i]=snapshot.sensors[i].samples-samples[i]; snapshot.window_polls[i]=snapshot.sensors[i].polls-polls[i]; samples[i]=snapshot.sensors[i].samples; polls[i]=snapshot.sensors[i].polls; }
            window_start=snapshot.uptime_us;
        }
        ++snapshot.sequence; snapshot.sampling=true; srnos_sensor_bringup.sampling=true; srnos_sensor_bringup=snapshot; srnos_sensor_bringup.sampling=false;
        LOGI("Sensors BMI=%lu LSM=%lu MAG=%lu P=%lu states=%u/%u/%u/%u",(unsigned long)snapshot.sensors[0].samples,(unsigned long)snapshot.sensors[1].samples,(unsigned long)snapshot.sensors[2].samples,(unsigned long)snapshot.sensors[3].samples,snapshot.sensors[0].state,snapshot.sensors[1].state,snapshot.sensors[2].state,snapshot.sensors[3].state);
        LOGI("Heater src=%s T=%.2f target=%.1f duty=%.1f%% fault=%u",
             snapshot.heater.source==1?"LSM":snapshot.heater.source==0?"BMI":"none",
             (double)snapshot.heater.feedback_c,(double)snapshot.heater.target_c,
             (double)(snapshot.heater.duty*100),snapshot.heater.faulted);
        vTaskDelayUntil(&last,pdMS_TO_TICKS(1000));
    }
}
