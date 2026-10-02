#include "stm32h7xx_hal.h"
#include "eeprom_internal.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/IO/I2C/I2C.h"
#include "cmsis_os2.h"

/* DS24C128-A2: 5 ms maximum tWR. One extra HAL tick avoids rounding a
 * fractional tick down. Shared I2C4 remains available during this interval. */
#define SRNOS_EEPROM_WRITE_SETTLE_MS 6U
#define SRNOS_EEPROM_READ_CHUNK_BYTES 32U
#define SRNOS_EEPROM_WRITE_CHUNK_BYTES 32U
_Static_assert(SRNOS_I2C_MAX_TRANSFER_BYTES >= SRNOS_EEPROM_READ_CHUNK_BYTES &&
               SRNOS_I2C_MAX_TRANSFER_BYTES >= SRNOS_EEPROM_WRITE_CHUNK_BYTES, "EEPROM transaction exceeds bus limit");

typedef enum { SRNOS_EEPROM_PHASE_SUBMIT, SRNOS_EEPROM_PHASE_TRANSFER, SRNOS_EEPROM_PHASE_WRITE_WAIT, SRNOS_EEPROM_PHASE_VERIFY_SUBMIT, SRNOS_EEPROM_PHASE_VERIFY_TRANSFER } SRNOS_EEPROMPhase;
static struct {
    SRNOS_I2CDevice device;
    bool initialized, write, cooldown;
    volatile bool busy;
    osThreadId_t owner, storage_owner;
    bool storage_configured, managed;
    SRNOS_EEPROMPhase phase;
    uint16_t address;
    uint8_t *rx;
    const uint8_t *tx;
    size_t length, offset, chunk;
    uint32_t timeout_ms, phase_tick, cooldown_tick;
    SRNOS_EEPROMStatus result;
    /* Only this static buffer is exposed to BSP RX. User memory cannot be
     * touched by a late IRQ after timeout/cancel, even if bus is quarantined. */
    uint8_t staging[SRNOS_EEPROM_READ_CHUNK_BYTES];
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
                                size_t length, uint32_t timeout_ms, bool storage)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    if ((!rx && !tx) || !length || address >= SRNOS_EEPROM_SIZE_BYTES ||
        length > SRNOS_EEPROM_SIZE_BYTES - (size_t)address ||
        !timeout_ms || timeout_ms > INT32_MAX) return SRNOS_EEPROM_INVALID_ARGUMENT;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (s.busy || (s.storage_owner && !storage)) { __set_PRIMASK(irq); return SRNOS_EEPROM_BUSY; }
    if (storage && s.storage_owner != osThreadGetId()) { __set_PRIMASK(irq); return SRNOS_EEPROM_BUSY; }
    if (!storage && tx && s.managed && address < 0x3000u) { __set_PRIMASK(irq); return SRNOS_EEPROM_READ_ONLY; }
    s.busy = true;
    s.owner = osThreadGetId();
    __set_PRIMASK(irq);
    s.write = tx != NULL;
    if (!s.initialized) {
        if (SRNOS_I2CDeviceInit(&s.device, SRNOS_I2C_BUS_4, SRNOS_EEPROM_ADDRESS_7BIT) != SRNOS_I2C_OK)
            return finish(SRNOS_EEPROM_IO_ERROR);
        s.initialized = true;
    }
    s.address = address; s.rx = rx; s.tx = tx;
    s.length = length; s.offset = 0; s.chunk = 0;
    s.timeout_ms = timeout_ms; s.phase_tick = HAL_GetTick();
    s.phase = SRNOS_EEPROM_PHASE_SUBMIT; s.result = SRNOS_EEPROM_IN_PROGRESS;
    return SRNOS_EEPROM_OK;
}

SRNOS_EEPROMStatus SRNOS_EEPROMReadAsync(uint16_t address, void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, data, NULL, length, timeout_ms, false); }

SRNOS_EEPROMStatus SRNOS_EEPROMWriteAsync(uint16_t address, const void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, NULL, data, length, timeout_ms, false); }

static SRNOS_EEPROMStatus abort_operation(SRNOS_EEPROMStatus result)
{
    /* Never recover another slave's transfer when waiting to acquire I2C4. */
    if (s.device.pending &&
        SRNOS_I2CCancelDevice(&s.device, result == SRNOS_EEPROM_TIMEOUT ?
                              SRNOS_I2C_RECOVER_REASON_TIMEOUT : SRNOS_I2C_RECOVER_REASON_USER) != SRNOS_I2C_OK)
        result = SRNOS_EEPROM_IO_ERROR;
    return finish(result);
}

SRNOS_EEPROMStatus SRNOS_EEPROMPoll(void)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    if (!s.busy) return s.result;
    if (s.owner != osThreadGetId()) return SRNOS_EEPROM_BUSY;
    uint32_t now = HAL_GetTick();
    if (s.phase == SRNOS_EEPROM_PHASE_WRITE_WAIT || (s.phase == SRNOS_EEPROM_PHASE_SUBMIT && s.cooldown)) {
        if (now - s.cooldown_tick < SRNOS_EEPROM_WRITE_SETTLE_MS) return SRNOS_EEPROM_IN_PROGRESS;
        s.cooldown = false;
        s.phase_tick = now;
        if (s.phase == SRNOS_EEPROM_PHASE_WRITE_WAIT) s.phase = SRNOS_EEPROM_PHASE_VERIFY_SUBMIT;
        return SRNOS_EEPROM_IN_PROGRESS;
    }
    if (s.phase == SRNOS_EEPROM_PHASE_TRANSFER || s.phase == SRNOS_EEPROM_PHASE_VERIFY_TRANSFER) {
        if (s.device.pending) {
            if (now - s.phase_tick >= s.timeout_ms) return abort_operation(SRNOS_EEPROM_TIMEOUT);
            return SRNOS_EEPROM_IN_PROGRESS;
        }
        if (s.device.completed_status != SRNOS_I2C_OK)
            return finish(s.device.completed_status == SRNOS_I2C_TIMEOUT ? SRNOS_EEPROM_TIMEOUT :
                          s.device.completed_status == SRNOS_I2C_CANCELLED ? SRNOS_EEPROM_CANCELLED : SRNOS_EEPROM_IO_ERROR);
        if (s.phase == SRNOS_EEPROM_PHASE_TRANSFER && s.write) {
            s.cooldown_tick = now; s.cooldown = true; s.phase = SRNOS_EEPROM_PHASE_WRITE_WAIT;
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
        s.phase = SRNOS_EEPROM_PHASE_SUBMIT; s.phase_tick = now;
        return SRNOS_EEPROM_IN_PROGRESS;
    }
    if (now - s.phase_tick >= s.timeout_ms) return finish(SRNOS_EEPROM_TIMEOUT);
    uint16_t address = (uint16_t)(s.address + s.offset);
    if (s.phase == SRNOS_EEPROM_PHASE_SUBMIT) {
        size_t maximum = s.write ? SRNOS_EEPROM_PAGE_BYTES - address % SRNOS_EEPROM_PAGE_BYTES : SRNOS_EEPROM_READ_CHUNK_BYTES;
        if (s.write && maximum > SRNOS_EEPROM_WRITE_CHUNK_BYTES) maximum = SRNOS_EEPROM_WRITE_CHUNK_BYTES;
        s.chunk = s.length - s.offset;
        if (s.chunk > maximum) s.chunk = maximum;
    }
    SRNOS_I2CStatus st;
    if (s.phase == SRNOS_EEPROM_PHASE_SUBMIT && s.write)
        st = SRNOS_I2CMemWrite(&s.device, address, I2C_MEMADD_SIZE_16BIT,
                               s.tx + s.offset, s.chunk, NULL, NULL);
    else
        st = SRNOS_I2CMemRead(&s.device, address, I2C_MEMADD_SIZE_16BIT,
                              s.staging, s.chunk, NULL, NULL);
    if (st == SRNOS_I2C_BUSY) return SRNOS_EEPROM_IN_PROGRESS;
    if (st != SRNOS_I2C_OK) return finish(SRNOS_EEPROM_IO_ERROR);
    s.phase = s.phase == SRNOS_EEPROM_PHASE_VERIFY_SUBMIT ? SRNOS_EEPROM_PHASE_VERIFY_TRANSFER : SRNOS_EEPROM_PHASE_TRANSFER;
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

bool SRNOS_EEPROMStorageConfigure(bool managed)
{
    int state = (int)osKernelGetState();
    if (__get_IPSR() || state < 0 || state >= (int)osKernelRunning || s.storage_configured) return false;
    s.managed = managed;
    s.storage_configured = true;
    return true;
}
SRNOS_EEPROMStatus SRNOS_EEPROMStorageAcquire(void)
{
    if (!task_context()) return SRNOS_EEPROM_CONTEXT_ERROR;
    uint32_t irq = __get_PRIMASK(); __disable_irq();
    if (s.busy || s.storage_owner) { __set_PRIMASK(irq); return SRNOS_EEPROM_BUSY; }
    s.storage_owner = osThreadGetId();
    __set_PRIMASK(irq);
    return SRNOS_EEPROM_OK;
}
void SRNOS_EEPROMStorageRelease(void)
{
    uint32_t irq = __get_PRIMASK(); __disable_irq();
    if (!s.busy && s.storage_owner == osThreadGetId()) s.storage_owner = NULL;
    __set_PRIMASK(irq);
}
SRNOS_EEPROMStatus SRNOS_EEPROMStorageReadAsync(uint16_t address, void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, data, NULL, length, timeout_ms, true); }
SRNOS_EEPROMStatus SRNOS_EEPROMStorageWriteAsync(uint16_t address, const void *data, size_t length, uint32_t timeout_ms)
{ return begin(address, NULL, data, length, timeout_ms, true); }
