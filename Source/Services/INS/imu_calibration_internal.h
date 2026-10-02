#ifndef SRNOS_IMU_CALIBRATION_INTERNAL_H
#define SRNOS_IMU_CALIBRATION_INTERNAL_H
#include "SirinEmbedOS/Services/IMUCalibration.h"
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_IMUCalibrationPrepare(bool storage_enabled);
uint32_t SRNOS_IMUCalibrationStartupBegin(uint32_t now_ms);
bool SRNOS_IMUCalibrationStartupPoll(uint32_t now_ms);
void SRNOS_IMUCalibrationLoadStartup(uint32_t deadline_ms);
/* Private exclusive writer gate. Held across static sampling and save so a
 * local Save cannot race the baseline. No IRQ mask is held during this lease. */
SRNOS_StorageStatus SRNOS_IMUCalibrationBeginSave(void);
void SRNOS_IMUCalibrationEndSave(void);
SRNOS_StorageStatus SRNOS_IMUCalibrationReadStored(SRNOS_SensorId, SRNOS_IMUCalibration *, uint32_t timeout_ms);
SRNOS_StorageStatus SRNOS_IMUCalibrationWriteLocked(const SRNOS_IMUCalibration *, uint32_t timeout_ms);
/* Frozen active values after StartupPoll resolves; copied once by INS. */
uint8_t SRNOS_IMUCalibrationCopyActive(SRNOS_IMUCalibration values[2]);
#ifdef __cplusplus
}
#endif
#endif
