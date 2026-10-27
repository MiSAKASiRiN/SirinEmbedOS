#include "nor_internal.h"
#include "Devices/nor_board.h"
#include <stdbool.h>
#include <string.h>

/* W25Q128JV rev F: tPP <= 3 ms, tSE <= 400 ms. Margins include tick
 * quantization; HAL transaction timeouts are separately bounded by the BSP. */
#define SRNOS_NOR_PROGRAM_TIMEOUT_MS 10U
#define SRNOS_NOR_ERASE_TIMEOUT_MS 500U
#define SRNOS_NOR_CMD_READ 0x03U
#define SRNOS_NOR_CMD_PROGRAM 0x02U
#define SRNOS_NOR_CMD_ERASE 0x20U
#define SRNOS_NOR_CMD_WREN 0x06U
#define SRNOS_NOR_CMD_JEDEC 0x9FU
#define SRNOS_NOR_CMD_SR1 0x05U
#define SRNOS_NOR_CMD_SR2 0x35U
#define SRNOS_NOR_CMD_SR3 0x15U
#define SRNOS_NOR_SR1_BUSY 0x01U
#define SRNOS_NOR_SR1_WEL 0x02U
#define SRNOS_NOR_SR1_BP 0x1CU
#define SRNOS_NOR_SR2_CMP 0x40U
#define SRNOS_NOR_SR2_SUS 0x80U
#define SRNOS_NOR_SR3_WPS 0x04U

static struct {
    bool attempted;
    bool ready;
    uint32_t jedec_id;
    SRNOS_NORStatus initialization;
    SRNOS_NORStatus fault;
} s;

static SRNOS_NORStatus latch(SRNOS_NORStatus status)
{
    if (status != SRNOS_NOR_OK && s.fault == SRNOS_NOR_OK) s.fault = status;
    return status;
}

static bool range_valid(uint32_t address, size_t length)
{
    return length != 0U && address < SRNOS_NOR_SIZE_BYTES &&
           length <= (size_t)(SRNOS_NOR_SIZE_BYTES - address);
}

static SRNOS_NORStatus read_status(uint8_t command, uint8_t *value)
{
    return SRNOS_NORBoardTransfer(command, false, 0U, NULL, value, 1U);
}

static SRNOS_NORStatus wait_ready(uint32_t timeout_ms, uint8_t *last_status)
{
    const uint32_t start = SRNOS_NORBoardTimeMs();
    for (;;) {
        uint8_t status = 0U;
        SRNOS_NORStatus result = read_status(SRNOS_NOR_CMD_SR1, &status);
        if (result != SRNOS_NOR_OK) return result;
        if ((uint32_t)(SRNOS_NORBoardTimeMs() - start) > timeout_ms)
            return SRNOS_NOR_TIMEOUT;
        if ((status & SRNOS_NOR_SR1_BUSY) == 0U) {
            if (last_status != NULL) *last_status = status;
            return SRNOS_NOR_OK;
        }
        if ((uint32_t)(SRNOS_NORBoardTimeMs() - start) >= timeout_ms)
            return SRNOS_NOR_TIMEOUT;
        result = SRNOS_NORBoardYield();
        if (result != SRNOS_NOR_OK) return result;
    }
}

static SRNOS_NORStatus check_protection(void)
{
    uint8_t sr1 = 0U, sr2 = 0U, sr3 = 0U;
    SRNOS_NORStatus result = wait_ready(SRNOS_NOR_ERASE_TIMEOUT_MS, &sr1);
    if (result == SRNOS_NOR_OK) result = read_status(SRNOS_NOR_CMD_SR2, &sr2);
    if (result == SRNOS_NOR_OK) result = read_status(SRNOS_NOR_CMD_SR3, &sr3);
    if (result != SRNOS_NOR_OK) return result;
    /* Refuse any protected region, even if a requested address is outside it.
     * WPS=1 uses individual locks: never unlock them or guess their state. */
    if ((sr1 & SRNOS_NOR_SR1_BP) != 0U || (sr2 & SRNOS_NOR_SR2_CMP) != 0U ||
        (sr3 & SRNOS_NOR_SR3_WPS) != 0U) return SRNOS_NOR_PROTECTED;
    if ((sr2 & SRNOS_NOR_SR2_SUS) != 0U) return SRNOS_NOR_IO_ERROR;
    return SRNOS_NOR_OK;
}

SRNOS_NORStatus SRNOS_NORInitialize(uint32_t *jedec_id)
{
    if (jedec_id == NULL) return SRNOS_NOR_INVALID;
    *jedec_id = s.jedec_id;
    if (s.attempted) {
        if (!SRNOS_NORBoardIsOwner()) return SRNOS_NOR_INVALID;
        return s.fault != SRNOS_NOR_OK ? s.fault : s.initialization;
    }
    SRNOS_NORStatus result = SRNOS_NORBoardInitialize();
    /* An invalid caller must not consume the legitimate worker's attempt. */
    if (result == SRNOS_NOR_INVALID) return result;
    s.attempted = true;
    uint8_t id[3] = {0};
    /* An MCU-only reset can leave the NOR finishing its previous erase.
     * JEDEC is ignored while BUSY: first use only the readable SR1 command.
     * Floating/all-ones status stays BUSY and times out without identification
     * or any write command; readiness alone never establishes identity. */
    if (result == SRNOS_NOR_OK) result = wait_ready(SRNOS_NOR_ERASE_TIMEOUT_MS, NULL);
    if (result == SRNOS_NOR_OK)
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_JEDEC, false, 0U, NULL, id, sizeof id);
    if (result == SRNOS_NOR_OK) {
        s.jedec_id = ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U) | id[2];
        *jedec_id = s.jedec_id;
        /* The JEDEC table explicitly lists these two W25Q128JV variants.
         * Capacity alone is insufficient; no EFxx18 wildcard is accepted. */
        if (s.jedec_id != 0xEF4018U && s.jedec_id != 0xEF7018U)
            result = SRNOS_NOR_UNSUPPORTED;
    }
    if (result == SRNOS_NOR_OK) result = check_protection();
    s.ready = result == SRNOS_NOR_OK;
    s.initialization = result;
    return latch(result);
}

SRNOS_NORStatus SRNOS_NORRead(uint32_t address, void *data, size_t length)
{
    if (data == NULL || !range_valid(address, length) || !SRNOS_NORBoardIsOwner())
        return SRNOS_NOR_INVALID;
    if (!s.ready) return s.fault != SRNOS_NOR_OK ? s.fault : SRNOS_NOR_IO_ERROR;
    SRNOS_NORStatus result = wait_ready(SRNOS_NOR_ERASE_TIMEOUT_MS, NULL);
    uint8_t *output = data;
    while (result == SRNOS_NOR_OK && length != 0U) {
        size_t chunk = length < SRNOS_NOR_PAGE_BYTES ? length : SRNOS_NOR_PAGE_BYTES;
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_READ, true, address, NULL, output, chunk);
        address += (uint32_t)chunk;
        output += chunk;
        length -= chunk;
        if (result == SRNOS_NOR_OK && length != 0U) result = SRNOS_NORBoardYield();
    }
    return latch(result);
}

static SRNOS_NORStatus write_enable(void)
{
    if (!s.ready) return s.fault != SRNOS_NOR_OK ? s.fault : SRNOS_NOR_IO_ERROR;
    if (s.fault != SRNOS_NOR_OK) return s.fault;
    SRNOS_NORStatus result = check_protection();
    if (result == SRNOS_NOR_OK)
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_WREN, false, 0U, NULL, NULL, 0U);
    uint8_t status = 0U;
    if (result == SRNOS_NOR_OK) result = read_status(SRNOS_NOR_CMD_SR1, &status);
    if (result == SRNOS_NOR_OK && (status & (SRNOS_NOR_SR1_BUSY | SRNOS_NOR_SR1_WEL)) != SRNOS_NOR_SR1_WEL)
        result = SRNOS_NOR_IO_ERROR;
    return latch(result);
}

static SRNOS_NORStatus write_complete(uint32_t timeout_ms)
{
    uint8_t status = 0U;
    SRNOS_NORStatus result = wait_ready(timeout_ms, &status);
    /* Program/erase clears WEL. If still set, the command was not accepted. */
    if (result == SRNOS_NOR_OK && (status & SRNOS_NOR_SR1_WEL) != 0U)
        result = SRNOS_NOR_IO_ERROR;
    return latch(result);
}

SRNOS_NORStatus SRNOS_NORProgram(uint32_t address, const void *data, size_t length)
{
    if (data == NULL || !range_valid(address, length) ||
        length > SRNOS_NOR_PAGE_BYTES - address % SRNOS_NOR_PAGE_BYTES ||
        !SRNOS_NORBoardIsOwner()) return SRNOS_NOR_INVALID;
    SRNOS_NORStatus result = write_enable();
    if (result == SRNOS_NOR_OK)
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_PROGRAM, true, address, data, NULL, length);
    if (result == SRNOS_NOR_OK) result = write_complete(SRNOS_NOR_PROGRAM_TIMEOUT_MS);
    if (result == SRNOS_NOR_OK) {
        uint8_t verify[SRNOS_NOR_PAGE_BYTES];
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_READ, true, address, NULL, verify, length);
        if (result == SRNOS_NOR_OK && memcmp(verify, data, length) != 0)
            result = SRNOS_NOR_IO_ERROR;
    }
    return latch(result);
}

SRNOS_NORStatus SRNOS_NOREraseSector(uint32_t address)
{
    if (!range_valid(address, SRNOS_NOR_SECTOR_BYTES) ||
        address % SRNOS_NOR_SECTOR_BYTES != 0U || !SRNOS_NORBoardIsOwner())
        return SRNOS_NOR_INVALID;
    SRNOS_NORStatus result = write_enable();
    if (result == SRNOS_NOR_OK)
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_ERASE, true, address, NULL, NULL, 0U);
    if (result == SRNOS_NOR_OK) result = write_complete(SRNOS_NOR_ERASE_TIMEOUT_MS);
    uint8_t verify[SRNOS_NOR_PAGE_BYTES];
    for (uint32_t offset = 0U; result == SRNOS_NOR_OK && offset < SRNOS_NOR_SECTOR_BYTES;
         offset += sizeof verify) {
        result = SRNOS_NORBoardTransfer(SRNOS_NOR_CMD_READ, true, address + offset, NULL, verify, sizeof verify);
        for (size_t i = 0U; result == SRNOS_NOR_OK && i < sizeof verify; ++i)
            if (verify[i] != 0xFFU) result = SRNOS_NOR_IO_ERROR;
        if (result == SRNOS_NOR_OK && offset + sizeof verify < SRNOS_NOR_SECTOR_BYTES)
            result = SRNOS_NORBoardYield();
    }
    return latch(result);
}
