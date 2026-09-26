#ifndef H723VG_V2_FREERTOS_TSKPTT_IMU_INTERNAL_H
#define H723VG_V2_FREERTOS_TSKPTT_IMU_INTERNAL_H

#include "SirinEmbedOS/Devices/IMU/lib_imu_project_config.h"
#include "SirinEmbedOS/Devices/IMU/Worker/tskptt_imu.h"
#include "SirinEmbedOS/Devices/IMU/lib_ins.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t g_tskptt_imu_calib_done;
extern uint8_t g_tskptt_imu_calib_started;
extern uint8_t g_tskptt_imu_temp_ctrl_ready;
extern volatile omx_dbg_imu_t g_omx_dbg_imu;

void tskptt_imu_debug_init(void);
void tskptt_imu_debug_sync(uint32_t now_ms, uint32_t status);

void tskptt_imu_omx_publish(const OmxImuState *in);
void tskptt_imu_snapshot_update_internal(const imu_sample_t *samp,
                                         const lib_ins_state_t *ins,
                                         uint32_t now_ms,
                                         uint8_t online);
void tskptt_imu_snapshot_set_internal_offline(uint32_t now_ms);
void tskptt_imu_snapshot_update_external(const OmxImuState *ext, uint32_t now_ms);

void tskptt_imu_dt_init(void);
void tskptt_imu_dt_reset(void);
float tskptt_imu_dt_compute(uint32_t now_ms);

uint8_t tskptt_imu_recovery_is_active(void);
void tskptt_imu_recovery_on_fresh_sample(void);
void tskptt_imu_recovery_tick(uint32_t now_ms);
void tskptt_imu_handle_sample_fault(imu_status_t st, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* H723VG_V2_FREERTOS_TSKPTT_IMU_INTERNAL_H */
