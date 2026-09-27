#include "SirinEmbedOS/Devices/EEPROM.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include "cmsis_os2.h"

/* DS24C128-A2: 5 ms maximum tWR. One extra HAL tick avoids rounding a
 * fractional tick down. Shared I2C4 remains available during this interval. */
#define WRITE_SETTLE_MS 6U
#define READ_CHUNK_BYTES 32U
#define WRITE_CHUNK_BYTES 32U
_Static_assert(BSP_I2C_MAX_TRANSFER_BYTES >= READ_CHUNK_BYTES &&
               BSP_I2C_MAX_TRANSFER_BYTES >= WRITE_CHUNK_BYTES, "EEPROM transaction exceeds bus limit");

typedef enum { SUBMIT, TRANSFER, WRITE_WAIT, VERIFY_SUBMIT, VERIFY_TRANSFER } Phase;
static struct {
    bsp_i2c_device_t device;
    bool initialized, write, cooldown;
    volatile bool busy;
    osThreadId_t owner;
    Phase phase;
    uint16_t address;
    uint8_t *rx;
    const uint8_t *tx;
    size_t length, offset, chunk;
    uint32_t timeout_ms, phase_tick, cooldown_tick;
    SRNOS_EEPROMStatus result;
    /* Only this static buffer is exposed to BSP RX. User memory cannot be
     * touched by a late IRQ after timeout/cancel, even if bus is quarantined. */
    uint8_t staging[READ_CHUNK_BYTES];
} s;

static bool task_context(void)
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U &&
           __get_BASEPRI() == 0U && osKernelGetState() == osKernelRunning &&
           osThreadGetId() != NULL;
}

static SRNOS_EEPROMStatus finish(SRNOS_EEPROMStatus result)
{
    if (s.write) { s.cooldown = true; s.cooldown_tick = HAL_GetTick(); }
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    s.rx = NULL; s.tx = NULL; s.owner = NULL;
    s.result = result;
    s.busy = false;
    __set_PRIMASK(irq);
    return result;
}

static SRNOS_EEPROMStatus begin(uint16_t address, void *rx, const void *tx,
                                size_t length, uint32_t timeout_ms)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    if ((!rx && !tx) || !length || address >= SRNOS_EEPROM_SIZE_BYTES ||
        length > SRNOS_EEPROM_SIZE_BYTES - (size_t)address ||
        !timeout_ms || timeout_ms > INT32_MAX) return SRNOS_EEPROM_INVALID_ARGUMENT;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (s.busy) { __set_PRIMASK(irq); return SRNOS_EEPROM_BUSY; }
    s.busy = true;
    s.owner = osThreadGetId();
    __set_PRIMASK(irq);
    s.write = tx != NULL;
    if (!s.initialized) {
        if (bsp_i2c_device_init(&s.device, BSP_I2C_BUS_4, SRNOS_EEPROM_ADDRESS_7BIT) != BSP_I2C_OK)
            return finish(SRNOS_EEPROM_IO_ERROR);
        s.initialized = true;
    }
    s.address = address; s.rx = rx; s.tx = tx;
    s.length = length; s.offset = 0; s.chunk = 0;
    s.timeout_ms = timeout_ms; s.phase_tick = HAL_GetTick();
    s.phase = SUBMIT; s.result = SRNOS_EEPROM_IN_PROGRESS;
    return SRNOS_EEPROM_OK;
}

SRNOS_EEPROMStatus SRNOS_EEPROMReadAsync(uint16_t address, void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, data, NULL, length, timeout_ms); }

SRNOS_EEPROMStatus SRNOS_EEPROMWriteAsync(uint16_t address, const void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, NULL, data, length, timeout_ms); }

static SRNOS_EEPROMStatus abort_operation(SRNOS_EEPROMStatus result)
{
    /* Never recover another slave's transfer when waiting to acquire I2C4. */
    if (s.device.pending &&
        bsp_i2c_cancel_device(&s.device, result == SRNOS_EEPROM_TIMEOUT ?
                              BSP_I2C_RECOVER_REASON_TIMEOUT : BSP_I2C_RECOVER_REASON_USER) != BSP_I2C_OK)
        result = SRNOS_EEPROM_IO_ERROR;
    return finish(result);
}

SRNOS_EEPROMStatus SRNOS_EEPROMPoll(void)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    if (!s.busy) return s.result;
    if (s.owner != osThreadGetId()) return SRNOS_EEPROM_BUSY;
    uint32_t now = HAL_GetTick();
    if (s.phase == WRITE_WAIT || (s.phase == SUBMIT && s.cooldown)) {
        if (now - s.cooldown_tick < WRITE_SETTLE_MS) return SRNOS_EEPROM_IN_PROGRESS;
        s.cooldown = false;
        s.phase_tick = now;
        if (s.phase == WRITE_WAIT) s.phase = VERIFY_SUBMIT;
        return SRNOS_EEPROM_IN_PROGRESS;
    }
    if (s.phase == TRANSFER || s.phase == VERIFY_TRANSFER) {
        if (s.device.pending) {
            if (now - s.phase_tick >= s.timeout_ms) return abort_operation(SRNOS_EEPROM_TIMEOUT);
            return SRNOS_EEPROM_IN_PROGRESS;
        }
        if (s.device.completed_status != BSP_I2C_OK)
            return finish(s.device.completed_status == BSP_I2C_TIMEOUT ? SRNOS_EEPROM_TIMEOUT :
                          s.device.completed_status == BSP_I2C_CANCELLED ? SRNOS_EEPROM_CANCELLED : SRNOS_EEPROM_IO_ERROR);
        if (s.phase == TRANSFER && s.write) {
            s.cooldown_tick = now; s.cooldown = true; s.phase = WRITE_WAIT;
            return SRNOS_EEPROM_IN_PROGRESS;
        }
        if (s.write) {
            for (size_t i = 0; i < s.chunk; ++i)
                if (s.staging[i] != s.tx[s.offset + i]) return finish(SRNOS_EEPROM_VERIFY_ERROR);
        } else {
            (void)SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(s.rx + s.offset, s.chunk),
                                    SRNOS_Memory_Bytes(s.staging, s.chunk));
        }
        s.offset += s.chunk;
        if (s.offset == s.length) return finish(SRNOS_EEPROM_OK);
        s.phase = SUBMIT; s.phase_tick = now;
        return SRNOS_EEPROM_IN_PROGRESS;
    }
    if (now - s.phase_tick >= s.timeout_ms) return finish(SRNOS_EEPROM_TIMEOUT);
    uint16_t address = (uint16_t)(s.address + s.offset);
    if (s.phase == SUBMIT) {
        size_t maximum = s.write ? SRNOS_EEPROM_PAGE_BYTES - address % SRNOS_EEPROM_PAGE_BYTES : READ_CHUNK_BYTES;
        if (s.write && maximum > WRITE_CHUNK_BYTES) maximum = WRITE_CHUNK_BYTES;
        s.chunk = s.length - s.offset;
        if (s.chunk > maximum) s.chunk = maximum;
    }
    bsp_i2c_status_t st;
    if (s.phase == SUBMIT && s.write)
        st = bsp_i2c_mem_write(&s.device, address, I2C_MEMADD_SIZE_16BIT,
                               s.tx + s.offset, s.chunk, NULL, NULL);
    else
        st = bsp_i2c_mem_read(&s.device, address, I2C_MEMADD_SIZE_16BIT,
                              s.staging, s.chunk, NULL, NULL);
    if (st == BSP_I2C_BUSY) return SRNOS_EEPROM_IN_PROGRESS;
    if (st != BSP_I2C_OK) return finish(SRNOS_EEPROM_IO_ERROR);
    s.phase = s.phase == VERIFY_SUBMIT ? VERIFY_TRANSFER : TRANSFER;
    s.phase_tick = HAL_GetTick();
    return SRNOS_EEPROM_IN_PROGRESS;
}

SRNOS_EEPROMStatus SRNOS_EEPROMCancel(void)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    if (!s.busy) return s.result;
    if (s.owner != osThreadGetId()) return SRNOS_EEPROM_BUSY;
    return abort_operation(SRNOS_EEPROM_CANCELLED);
}

bool SRNOS_EEPROMIsBusy(void) { return s.busy; }

static SRNOS_EEPROMStatus wait_result(SRNOS_EEPROMStatus status)
{
    if (status != SRNOS_EEPROM_OK) return status;
    while ((status = SRNOS_EEPROMPoll()) == SRNOS_EEPROM_IN_PROGRESS) osDelay(1U);
    return status;
}

SRNOS_EEPROMStatus SRNOS_EEPROMRead(uint16_t address, void *data, size_t length, uint32_t timeout_ms)
{ return wait_result(SRNOS_EEPROMReadAsync(address, data, length, timeout_ms)); }

SRNOS_EEPROMStatus SRNOS_EEPROMWrite(uint16_t address, const void *data, size_t length, uint32_t timeout_ms)
{ return wait_result(SRNOS_EEPROMWriteAsync(address, data, length, timeout_ms)); }
