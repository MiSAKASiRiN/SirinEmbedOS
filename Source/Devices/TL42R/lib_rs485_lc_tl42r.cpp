#include "SirinEmbedOS/Devices/TL42R/lib_rs485_lc_tl42r.h"
#include "SirinEmbedOS/Core/Array.hpp"

#include <cstring>

#include "cmsis_os2.h"
static uint32_t tl42r_now_ms() { return uint32_t(uint64_t(osKernelGetTickCount()) * 1000 / osKernelGetTickFreq()); }

namespace {

static volatile uint32_t s_lc_tl42r_watch_seq = 0u;
static volatile uint32_t s_lc_tl42r_config_cache_seq = 0u;

static void lc_tl42r_copy_to_volatile(volatile void *dst, const void *src, size_t size)
{
    if ((dst == nullptr) || (src == nullptr) || (size == 0u)) {
        return;
    }

    volatile uint8_t *dst_bytes = reinterpret_cast<volatile uint8_t *>(dst);
    const uint8_t *src_bytes = reinterpret_cast<const uint8_t *>(src);
    for (size_t i = 0u; i < size; ++i) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void lc_tl42r_copy_from_volatile(void *dst, const volatile void *src, size_t size)
{
    if ((dst == nullptr) || (src == nullptr) || (size == 0u)) {
        return;
    }

    uint8_t *dst_bytes = reinterpret_cast<uint8_t *>(dst);
    const volatile uint8_t *src_bytes = reinterpret_cast<const volatile uint8_t *>(src);
    for (size_t i = 0u; i < size; ++i) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void lc_tl42r_store_watch(const lc_tl42r_watch_t *watch)
{
    if (watch == nullptr) {
        return;
    }

    __atomic_fetch_add(&s_lc_tl42r_watch_seq, 1u, __ATOMIC_ACQ_REL);
    lc_tl42r_copy_to_volatile(&g_lc_tl42r_watch, watch, sizeof(*watch));
    __atomic_fetch_add(&s_lc_tl42r_watch_seq, 1u, __ATOMIC_RELEASE);
}

static bool lc_tl42r_load_watch(lc_tl42r_watch_t *out_watch)
{
    if (out_watch == nullptr) {
        return false;
    }

    for (uint8_t i = 0u; i < 3u; ++i) {
        const uint32_t seq_start = __atomic_load_n(&s_lc_tl42r_watch_seq, __ATOMIC_ACQUIRE);
        if ((seq_start & 1u) != 0u) {
            continue;
        }

        lc_tl42r_copy_from_volatile(out_watch, &g_lc_tl42r_watch, sizeof(*out_watch));
        const uint32_t seq_end = __atomic_load_n(&s_lc_tl42r_watch_seq, __ATOMIC_ACQUIRE);
        if (seq_start == seq_end) {
            return true;
        }
    }

    lc_tl42r_copy_from_volatile(out_watch, &g_lc_tl42r_watch, sizeof(*out_watch));
    return true;
}

static void lc_tl42r_store_config_cache(const lc_tl42r_config_cache_t *cache)
{
    if (cache == nullptr) {
        return;
    }

    __atomic_fetch_add(&s_lc_tl42r_config_cache_seq, 1u, __ATOMIC_ACQ_REL);
    lc_tl42r_copy_to_volatile(&g_lc_tl42r_config_cache, cache, sizeof(*cache));
    __atomic_fetch_add(&s_lc_tl42r_config_cache_seq, 1u, __ATOMIC_RELEASE);
}

static bool lc_tl42r_load_config_cache(lc_tl42r_config_cache_t *out_cache)
{
    if (out_cache == nullptr) {
        return false;
    }

    for (uint8_t i = 0u; i < 3u; ++i) {
        const uint32_t seq_start = __atomic_load_n(&s_lc_tl42r_config_cache_seq, __ATOMIC_ACQUIRE);
        if ((seq_start & 1u) != 0u) {
            continue;
        }

        lc_tl42r_copy_from_volatile(out_cache, &g_lc_tl42r_config_cache, sizeof(*out_cache));
        const uint32_t seq_end = __atomic_load_n(&s_lc_tl42r_config_cache_seq, __ATOMIC_ACQUIRE);
        if (seq_start == seq_end) {
            return true;
        }
    }

    lc_tl42r_copy_from_volatile(out_cache, &g_lc_tl42r_config_cache, sizeof(*out_cache));
    return true;
}

static void lc_tl42r_invalidate_config_cache(uint32_t mask)
{
    lc_tl42r_config_cache_t cache = {};
    (void)lc_tl42r_load_config_cache(&cache);

    if ((mask & LC_TL42R_CONFIG_CACHE_BASIC) != 0u) {
        cache.basic_valid = false;
    }
    if ((mask & LC_TL42R_CONFIG_CACHE_MULTI_POSITION) != 0u) {
        cache.position_table_valid = false;
    }
    if ((mask & LC_TL42R_CONFIG_CACHE_MULTI_SPEED) != 0u) {
        cache.speed_table_valid = false;
    }
    if (mask != 0u) {
        cache.initialized = true;
        cache.last_refresh_ms = tl42r_now_ms();
        lc_tl42r_store_config_cache(&cache);
    }
}

typedef struct {
    lc_tl42r_param_id_t param;
    uint16_t address;
    lc_tl42r_param_type_t type;
    bool readable;
    bool writable;
    bool persistent;
    lc_tl42r_param_group_t group;
} lc_tl42r_param_descriptor_entry_t;

static const lc_tl42r_param_descriptor_entry_t s_base_descriptors[] = {
    {LC_TL42R_PARAM_PA_001, 1u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_002, 2u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_004, 4u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_005, 5u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_006, 6u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_007, 7u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_008, 8u, LC_TL42R_PARAM_TYPE_I32, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_009, 9u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},
    {LC_TL42R_PARAM_PA_00A, 10u, LC_TL42R_PARAM_TYPE_I16, true, false, false, LC_TL42R_PARAM_GROUP_MONITOR},

    {LC_TL42R_PARAM_PA_010, 16u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_011, 17u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_012, 18u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_013, 19u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_014, 20u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_015, 21u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_01A, 26u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_01B, 27u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_01C, 28u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_01D, 29u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},
    {LC_TL42R_PARAM_PA_01F, 31u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_DIO},

    {LC_TL42R_PARAM_PA_020, 32u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_021, 33u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_022, 34u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_023, 35u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_024, 36u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_025, 37u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_026, 38u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_028, 40u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_030, 48u, LC_TL42R_PARAM_TYPE_I16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_031, 49u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_032, 50u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_033, 51u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_034, 52u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_035, 53u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_036, 54u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_037, 55u, LC_TL42R_PARAM_TYPE_I32, true, true, false, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_038, 56u, LC_TL42R_PARAM_TYPE_U16, true, true, false, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_040, 64u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_041, 65u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_042, 66u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_043, 67u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_044, 68u, LC_TL42R_PARAM_TYPE_I32, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_045, 69u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_046, 70u, LC_TL42R_PARAM_TYPE_I32, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_047, 71u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_048, 72u, LC_TL42R_PARAM_TYPE_I32, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_049, 73u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_04A, 74u, LC_TL42R_PARAM_TYPE_U16, true, true, true, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_04E, 78u, LC_TL42R_PARAM_TYPE_U16, true, true, false, LC_TL42R_PARAM_GROUP_COMM},
    {LC_TL42R_PARAM_PA_04F, 79u, LC_TL42R_PARAM_TYPE_U16, true, true, false, LC_TL42R_PARAM_GROUP_COMM},

    {LC_TL42R_PARAM_PA_100, 256u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_101, 257u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_102, 258u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_103, 259u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_104, 260u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_105, 261u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_106, 262u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_107, 263u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_109, 265u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10A, 266u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10B, 267u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10C, 268u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10D, 269u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10E, 270u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_10F, 271u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_110, 272u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_111, 273u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_112, 274u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_113, 275u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_114, 276u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_115, 277u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_116, 278u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_117, 279u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_118, 280u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY},
    {LC_TL42R_PARAM_PA_119, 281u, LC_TL42R_PARAM_TYPE_U16, true, false, false, LC_TL42R_PARAM_GROUP_FACTORY}
};

static uint32_t lc_tl42r_cache_mask_from_group(lc_tl42r_param_group_t group)
{
    switch (group) {
        case LC_TL42R_PARAM_GROUP_DIO:
        case LC_TL42R_PARAM_GROUP_COMM:
        case LC_TL42R_PARAM_GROUP_FACTORY:
            return LC_TL42R_CONFIG_CACHE_BASIC;
        case LC_TL42R_PARAM_GROUP_MULTI_POSITION:
            return LC_TL42R_CONFIG_CACHE_MULTI_POSITION;
        case LC_TL42R_PARAM_GROUP_MULTI_SPEED:
            return LC_TL42R_CONFIG_CACHE_MULTI_SPEED;
        default:
            return 0u;
    }
}

static bool lc_tl42r_lookup_base_descriptor(lc_tl42r_param_id_t param,
                                            lc_tl42r_param_descriptor_t *out_descriptor)
{
    if (out_descriptor == nullptr) {
        return false;
    }

    for (size_t i = 0u; i < SRNOS::Array::count(s_base_descriptors); ++i) {
        if (s_base_descriptors[i].param != param) {
            continue;
        }

        out_descriptor->param = s_base_descriptors[i].param;
        out_descriptor->address = s_base_descriptors[i].address;
        out_descriptor->type = s_base_descriptors[i].type;
        out_descriptor->readable = s_base_descriptors[i].readable;
        out_descriptor->writable = s_base_descriptors[i].writable;
        out_descriptor->persistent = s_base_descriptors[i].persistent;
        out_descriptor->group = s_base_descriptors[i].group;
        return true;
    }

    return false;
}

static bool lc_tl42r_lookup_multi_position_descriptor(lc_tl42r_param_id_t param,
                                                      lc_tl42r_param_descriptor_t *out_descriptor)
{
    if (out_descriptor == nullptr) {
        return false;
    }

    const uint16_t addr = (uint16_t)param;
    if ((addr < 80u) || (addr > 174u)) {
        return false;
    }

    const uint16_t relative = (uint16_t)(addr - 80u);
    const uint16_t slot = (uint16_t)(relative % 6u);
    if (slot >= 5u) {
        return false;
    }

    out_descriptor->param = param;
    out_descriptor->address = addr;
    out_descriptor->readable = true;
    out_descriptor->writable = true;
    out_descriptor->persistent = true;
    out_descriptor->group = LC_TL42R_PARAM_GROUP_MULTI_POSITION;

    switch (slot) {
        case 0u:
            out_descriptor->type = LC_TL42R_PARAM_TYPE_I32;
            return true;
        case 1u:
            out_descriptor->type = LC_TL42R_PARAM_TYPE_U16;
            return true;
        case 2u:
        case 3u:
        case 4u:
            out_descriptor->type = LC_TL42R_PARAM_TYPE_U16;
            return true;
        default:
            return false;
    }
}

static bool lc_tl42r_lookup_multi_speed_descriptor(lc_tl42r_param_id_t param,
                                                   lc_tl42r_param_descriptor_t *out_descriptor)
{
    if (out_descriptor == nullptr) {
        return false;
    }

    const uint16_t addr = (uint16_t)param;
    if ((addr < 176u) || (addr > 223u)) {
        return false;
    }

    const uint16_t slot = (uint16_t)((addr - 176u) % 3u);
    out_descriptor->param = param;
    out_descriptor->address = addr;
    out_descriptor->readable = true;
    out_descriptor->writable = true;
    out_descriptor->persistent = true;
    out_descriptor->group = LC_TL42R_PARAM_GROUP_MULTI_SPEED;
    out_descriptor->type = (slot == 0u) ? LC_TL42R_PARAM_TYPE_I16 : LC_TL42R_PARAM_TYPE_U16;
    return true;
}

static bool lc_tl42r_get_param_descriptor_internal(lc_tl42r_param_id_t param,
                                                   lc_tl42r_param_descriptor_t *out_descriptor)
{
    if (out_descriptor == nullptr) {
        return false;
    }

    if (lc_tl42r_lookup_base_descriptor(param, out_descriptor)) {
        return true;
    }
    if (lc_tl42r_lookup_multi_position_descriptor(param, out_descriptor)) {
        return true;
    }
    if (lc_tl42r_lookup_multi_speed_descriptor(param, out_descriptor)) {
        return true;
    }
    return false;
}

static int lc_tl42r_read_registers(lc_tl42r_device_t *dev,
                                   uint16_t start_addr,
                                   uint16_t quantity,
                                   uint16_t *out_regs,
                                   uint16_t out_capacity,
                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_regs == nullptr) || (quantity == 0u)) {
        return LC_TL42R_ERR_PARAM;
    }
    if (out_capacity < quantity) {
        return LC_TL42R_ERR_LENGTH;
    }
    if (dev->modbus_client.router_handle == RS485_ROUTER_INVALID_HANDLE) {
        return LC_TL42R_ERR_STATE;
    }

    dev->last_error = LC_TL42R_OK;
    dev->last_exception_code = 0u;

    modbus_rtu_client_flush_rx(&dev->modbus_client);

    int ret = modbus_rtu_read_holding_registers(&dev->modbus_client, start_addr, quantity, timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    modbus_rtu_frame_t frame = {};
    ret = modbus_rtu_client_wait_response(&dev->modbus_client, &frame, timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = modbus_rtu_validate_response(&dev->modbus_client, &frame, MODBUS_RTU_FC_READ_HOLDING_REGISTERS);
    if (ret != MODBUS_RTU_OK) {
        if (ret == MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t reg_count = 0u;
    ret = modbus_rtu_parse_read_registers_response(&frame,
                                                   MODBUS_RTU_FC_READ_HOLDING_REGISTERS,
                                                   out_regs,
                                                   out_capacity,
                                                   &reg_count);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if (reg_count != quantity) {
        dev->last_error = LC_TL42R_ERR_LENGTH;
        return LC_TL42R_ERR_LENGTH;
    }

    dev->last_error = LC_TL42R_OK;
    return LC_TL42R_OK;
}

static int lc_tl42r_write_single_register(lc_tl42r_device_t *dev,
                                          uint16_t reg_addr,
                                          uint16_t value,
                                          uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }
    if (dev->modbus_client.router_handle == RS485_ROUTER_INVALID_HANDLE) {
        return LC_TL42R_ERR_STATE;
    }

    dev->last_error = LC_TL42R_OK;
    dev->last_exception_code = 0u;

    modbus_rtu_client_flush_rx(&dev->modbus_client);

    int ret = modbus_rtu_write_single_register(&dev->modbus_client, reg_addr, value, timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    modbus_rtu_frame_t frame = {};
    ret = modbus_rtu_client_wait_response(&dev->modbus_client, &frame, timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = modbus_rtu_validate_response(&dev->modbus_client, &frame, MODBUS_RTU_FC_WRITE_SINGLE_REGISTER);
    if (ret != MODBUS_RTU_OK) {
        if (ret == MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t echoed_addr = 0u;
    uint16_t echoed_value = 0u;
    ret = modbus_rtu_parse_write_single_response(&frame,
                                                 MODBUS_RTU_FC_WRITE_SINGLE_REGISTER,
                                                 &echoed_addr,
                                                 &echoed_value);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if ((echoed_addr != reg_addr) || (echoed_value != value)) {
        dev->last_error = LC_TL42R_ERR_STATE;
        return LC_TL42R_ERR_STATE;
    }

    dev->last_error = LC_TL42R_OK;
    return LC_TL42R_OK;
}

static int lc_tl42r_write_multiple_registers(lc_tl42r_device_t *dev,
                                             uint16_t start_addr,
                                             uint16_t quantity,
                                             const uint16_t *values,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (values == nullptr) || (quantity == 0u)) {
        return LC_TL42R_ERR_PARAM;
    }
    if (dev->modbus_client.router_handle == RS485_ROUTER_INVALID_HANDLE) {
        return LC_TL42R_ERR_STATE;
    }

    dev->last_error = LC_TL42R_OK;
    dev->last_exception_code = 0u;

    modbus_rtu_client_flush_rx(&dev->modbus_client);

    int ret = modbus_rtu_write_multiple_registers(&dev->modbus_client,
                                                  start_addr,
                                                  quantity,
                                                  values,
                                                  timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    modbus_rtu_frame_t frame = {};
    ret = modbus_rtu_client_wait_response(&dev->modbus_client, &frame, timeout_ms);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = modbus_rtu_validate_response(&dev->modbus_client, &frame, MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS);
    if (ret != MODBUS_RTU_OK) {
        if (ret == MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t echoed_addr = 0u;
    uint16_t echoed_quantity = 0u;
    ret = modbus_rtu_parse_write_multiple_response(&frame,
                                                   MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS,
                                                   &echoed_addr,
                                                   &echoed_quantity);
    if (ret != MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if ((echoed_addr != start_addr) || (echoed_quantity != quantity)) {
        dev->last_error = LC_TL42R_ERR_STATE;
        return LC_TL42R_ERR_STATE;
    }

    dev->last_error = LC_TL42R_OK;
    return LC_TL42R_OK;
}

static int lc_tl42r_write_block(lc_tl42r_device_t *dev,
                                uint16_t start_addr,
                                uint16_t quantity,
                                const uint16_t *values,
                                uint32_t timeout_ms)
{
    if ((values == nullptr) || (quantity == 0u)) {
        return LC_TL42R_ERR_PARAM;
    }
    if (quantity == 1u) {
        return lc_tl42r_write_single_register(dev, start_addr, values[0], timeout_ms);
    }
    return lc_tl42r_write_multiple_registers(dev, start_addr, quantity, values, timeout_ms);
}

static int lc_tl42r_read_param_raw(lc_tl42r_device_t *dev,
                                   const lc_tl42r_param_descriptor_t *desc,
                                   uint32_t *out_raw_value,
                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (desc == nullptr) || (out_raw_value == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }
    if (!desc->readable) {
        return LC_TL42R_ERR_ACCESS;
    }

    uint16_t regs[2] = {};
    const uint16_t quantity = ((desc->type == LC_TL42R_PARAM_TYPE_U32) || (desc->type == LC_TL42R_PARAM_TYPE_I32)) ? 2u : 1u;
    const int ret = lc_tl42r_read_registers(dev, desc->address, quantity, regs, quantity, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    if (quantity == 1u) {
        *out_raw_value = regs[0];
    } else {
        *out_raw_value = ((uint32_t)regs[0] << 16) | (uint32_t)regs[1];
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_param_raw(lc_tl42r_device_t *dev,
                                    const lc_tl42r_param_descriptor_t *desc,
                                    uint32_t raw_value,
                                    bool auto_save,
                                    uint32_t timeout_ms)
{
    if ((dev == nullptr) || (desc == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }
    if (!desc->writable) {
        return LC_TL42R_ERR_ACCESS;
    }

    int ret = LC_TL42R_OK;
    if ((desc->type == LC_TL42R_PARAM_TYPE_U32) || (desc->type == LC_TL42R_PARAM_TYPE_I32)) {
        const uint16_t regs[2] = {
            (uint16_t)(raw_value >> 16),
            (uint16_t)(raw_value & 0xFFFFu)
        };
        ret = lc_tl42r_write_multiple_registers(dev, desc->address, 2u, regs, timeout_ms);
    } else {
        ret = lc_tl42r_write_single_register(dev, desc->address, (uint16_t)raw_value, timeout_ms);
    }
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(lc_tl42r_cache_mask_from_group(desc->group));
    if (auto_save && desc->persistent) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static uint16_t lc_tl42r_multi_position_segment_base(uint8_t segment_index)
{
    return (uint16_t)(80u + (uint16_t)segment_index * 6u);
}

static uint16_t lc_tl42r_multi_speed_segment_base(uint8_t segment_index)
{
    return (uint16_t)(176u + (uint16_t)segment_index * 3u);
}

static void lc_tl42r_fill_monitor(lc_tl42r_monitor_t *monitor, const uint16_t *regs)
{
    if ((monitor == nullptr) || (regs == nullptr)) {
        return;
    }

    (void)memset(monitor, 0, sizeof(*monitor));
    monitor->raw_run_status = regs[0];
    monitor->raw_alarm = regs[1];
    monitor->raw_di_state = regs[2];
    monitor->raw_do_state = regs[3];
    monitor->raw_position_high = regs[4];
    monitor->raw_position_low = regs[5];
    monitor->raw_current_speed = regs[6];
    monitor->current_position_pulse = lc_tl42r_compose_position(monitor->raw_position_high,
                                                                monitor->raw_position_low);
    monitor->current_speed_rpm = lc_tl42r_decode_speed(monitor->raw_current_speed);
    lc_tl42r_decode_run_status(monitor->raw_run_status, &monitor->run_status);
    lc_tl42r_decode_di_state(monitor->raw_di_state, &monitor->di_state);
    lc_tl42r_decode_do_state(monitor->raw_do_state, &monitor->do_state);
}

static void lc_tl42r_fill_factory_params(lc_tl42r_factory_params_t *factory,
                                         const uint16_t *regs_256_263,
                                         const uint16_t *regs_265_281)
{
    if ((factory == nullptr) || (regs_256_263 == nullptr) || (regs_265_281 == nullptr)) {
        return;
    }

    (void)memset(factory, 0, sizeof(*factory));
    factory->run_mode = (lc_tl42r_run_mode_t)regs_256_263[0];
    factory->encoder_resolution = regs_256_263[1];
    factory->max_effective_current_ma = regs_256_263[2];
    factory->closed_loop_max_current_ratio = regs_256_263[3];
    factory->base_current_ratio = regs_256_263[4];
    factory->open_loop_max_current_ratio = regs_256_263[5];
    factory->locked_current_ratio = regs_256_263[6];
    factory->locked_time = regs_256_263[7];
    factory->lowpass_filter_coefficient = regs_265_281[0];
    factory->deviation_threshold = regs_265_281[1];
    factory->positioning_precision_threshold = regs_265_281[2];
    factory->positioning_done_time = regs_265_281[3];
    factory->mean_filter_coefficient = regs_265_281[4];
    factory->current_loop_gain_adjust_ratio = regs_265_281[5];
    factory->current_loop_kp = regs_265_281[6];
    factory->current_loop_ki = regs_265_281[7];
    factory->current_loop_kc = regs_265_281[8];
    factory->la_speed_kp1 = regs_265_281[9];
    factory->la_speed_kv1 = regs_265_281[10];
    factory->speed_node1 = regs_265_281[11];
    factory->la_speed_kp2 = regs_265_281[12];
    factory->la_speed_kv2 = regs_265_281[13];
    factory->speed_node2 = regs_265_281[14];
    factory->speed_feed_forward = regs_265_281[15];
    factory->position_integral = regs_265_281[16];
}

static int lc_tl42r_read_factory_params_internal(lc_tl42r_device_t *dev,
                                                 lc_tl42r_factory_params_t *out_factory,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_factory == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs_256_263[8] = {};
    uint16_t regs_265_281[17] = {};

    int ret = lc_tl42r_read_registers(dev, 256u, 8u, regs_256_263, 8u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 265u, 17u, regs_265_281, 17u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_fill_factory_params(out_factory, regs_256_263, regs_265_281);
    return LC_TL42R_OK;
}

static int lc_tl42r_write_di_config_internal(lc_tl42r_device_t *dev,
                                             const lc_tl42r_di_config_t *config,
                                             bool auto_save,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t regs_16_21[6] = {
        config->normally_closed_mask,
        (uint16_t)config->input_function[0],
        (uint16_t)config->input_function[1],
        (uint16_t)config->input_function[2],
        (uint16_t)config->input_function[3],
        (uint16_t)config->input_function[4]
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 16u, 6u, regs_16_21, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_write_single_register(dev, 26u, config->filter_coefficient, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_do_config_internal(lc_tl42r_device_t *dev,
                                             const lc_tl42r_do_config_t *config,
                                             bool auto_save,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t regs_27_29[3] = {
        config->normally_closed_mask,
        (uint16_t)config->output_function[0],
        (uint16_t)config->output_function[1]
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 27u, 3u, regs_27_29, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_write_single_register(dev, 31u, config->forced_output_mask, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_comm_config_internal(lc_tl42r_device_t *dev,
                                               const lc_tl42r_comm_config_t *config,
                                               bool auto_save,
                                               uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t regs_32_38[7] = {
        config->station_id,
        (uint16_t)config->serial_format,
        config->default_direction,
        config->subdivision,
        config->limit_stop_mode,
        config->soft_limit_enable,
        config->multi_position_trigger_mode
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 32u, 7u, regs_32_38, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_write_single_register(dev, 40u, (uint16_t)config->baudrate, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_motion_config_internal(lc_tl42r_device_t *dev,
                                                 const lc_tl42r_motion_config_t *config,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t persistent_regs[7] = {
        (uint16_t)config->jog_speed_rpm,
        config->jog_accel_ms,
        config->jog_decel_ms,
        config->position_start_speed_rpm,
        config->position_accel_ms,
        config->position_decel_ms,
        config->position_speed_rpm
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 48u, 7u, persistent_regs, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_write_single_register(dev, 74u, (uint16_t)config->multi_position_mode, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        ret = lc_tl42r_save_parameters(dev, timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
    }

    return lc_tl42r_set_position_target(dev, config->position_target_pulse, timeout_ms);
}

static int lc_tl42r_write_homing_config_internal(lc_tl42r_device_t *dev,
                                                 const lc_tl42r_homing_config_t *config,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint32_t raw_offset = (uint32_t)config->homing_offset_pulse;
    const uint32_t raw_pos_limit = (uint32_t)config->positive_soft_limit_pulse;
    const uint32_t raw_neg_limit = (uint32_t)config->negative_soft_limit_pulse;
    const uint16_t regs_64_73[10] = {
        (uint16_t)config->homing_mode,
        config->homing_approach_speed_rpm,
        config->homing_crawl_speed_rpm,
        config->homing_accel_ms,
        (uint16_t)(raw_offset >> 16),
        (uint16_t)(raw_offset & 0xFFFFu),
        (uint16_t)(raw_pos_limit >> 16),
        (uint16_t)(raw_pos_limit & 0xFFFFu),
        (uint16_t)(raw_neg_limit >> 16),
        (uint16_t)(raw_neg_limit & 0xFFFFu)
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 64u, 10u, regs_64_73, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_position_segment_internal(lc_tl42r_device_t *dev,
                                                    uint8_t segment_index,
                                                    const lc_tl42r_position_segment_t *segment,
                                                    bool auto_save,
                                                    uint32_t timeout_ms)
{
    if ((dev == nullptr) || (segment == nullptr) || (segment_index >= LC_TL42R_MULTI_POSITION_SEGMENT_COUNT)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint32_t raw_target = (uint32_t)segment->target_pulse;
    const uint16_t regs[5] = {
        (uint16_t)(raw_target >> 16),
        (uint16_t)(raw_target & 0xFFFFu),
        segment->speed_rpm,
        segment->accel_ms,
        segment->decel_ms
    };

    int ret = lc_tl42r_write_multiple_registers(dev,
                                                lc_tl42r_multi_position_segment_base(segment_index),
                                                5u,
                                                regs,
                                                timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_MULTI_POSITION);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_write_speed_segment_internal(lc_tl42r_device_t *dev,
                                                 uint8_t segment_index,
                                                 const lc_tl42r_speed_segment_t *segment,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (segment == nullptr) || (segment_index >= LC_TL42R_MULTI_SPEED_SEGMENT_COUNT)) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t regs[3] = {
        (uint16_t)segment->speed_rpm,
        segment->accel_ms,
        segment->decel_ms
    };

    int ret = lc_tl42r_write_multiple_registers(dev,
                                                lc_tl42r_multi_speed_segment_base(segment_index),
                                                3u,
                                                regs,
                                                timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_MULTI_SPEED);
    if (auto_save) {
        return lc_tl42r_save_parameters(dev, timeout_ms);
    }
    return LC_TL42R_OK;
}

static int lc_tl42r_read_multi_position_segment_internal(lc_tl42r_device_t *dev,
                                                         uint8_t segment_index,
                                                         lc_tl42r_position_segment_t *out_segment,
                                                         uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_segment == nullptr) || (segment_index >= LC_TL42R_MULTI_POSITION_SEGMENT_COUNT)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[5] = {};
    const int ret = lc_tl42r_read_registers(dev,
                                            lc_tl42r_multi_position_segment_base(segment_index),
                                            5u,
                                            regs,
                                            5u,
                                            timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    out_segment->target_pulse = lc_tl42r_compose_position(regs[0], regs[1]);
    out_segment->speed_rpm = regs[2];
    out_segment->accel_ms = regs[3];
    out_segment->decel_ms = regs[4];
    return LC_TL42R_OK;
}

static int lc_tl42r_read_multi_speed_segment_internal(lc_tl42r_device_t *dev,
                                                      uint8_t segment_index,
                                                      lc_tl42r_speed_segment_t *out_segment,
                                                      uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_segment == nullptr) || (segment_index >= LC_TL42R_MULTI_SPEED_SEGMENT_COUNT)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[3] = {};
    const int ret = lc_tl42r_read_registers(dev,
                                            lc_tl42r_multi_speed_segment_base(segment_index),
                                            3u,
                                            regs,
                                            3u,
                                            timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    out_segment->speed_rpm = (int16_t)regs[0];
    out_segment->accel_ms = regs[1];
    out_segment->decel_ms = regs[2];
    return LC_TL42R_OK;
}

} // namespace

extern "C" {
volatile lc_tl42r_watch_t g_lc_tl42r_watch = {};
volatile lc_tl42r_config_cache_t g_lc_tl42r_config_cache = {};
}

extern "C" void lc_tl42r_reset(lc_tl42r_device_t *dev)
{
    if (SRNOS_RS485IsFrozen()) return;
    if (dev == nullptr) {
        return;
    }

    (void)memset(dev, 0, sizeof(*dev));
}

extern "C" int lc_tl42r_init(lc_tl42r_device_t *dev, uint8_t port, uint8_t unit_id)
{
    if (SRNOS_RS485IsFrozen()) return LC_TL42R_ERR_STATE;
    if ((dev == nullptr) || (port == 0u) || (unit_id == 0u)) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_reset(dev);
    dev->port = port;
    dev->unit_id = unit_id;

    const int ret = modbus_rtu_client_init(&dev->modbus_client, port, unit_id);
    dev->last_error = ret;
    return ret;
}

extern "C" int lc_tl42r_deinit(lc_tl42r_device_t *dev)
{
    if (SRNOS_RS485IsFrozen()) return LC_TL42R_ERR_STATE;
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    if (dev->modbus_client.router_handle == RS485_ROUTER_INVALID_HANDLE) {
        lc_tl42r_reset(dev);
        return LC_TL42R_OK;
    }

    const int ret = modbus_rtu_client_deinit(&dev->modbus_client);
    lc_tl42r_reset(dev);
    return ret;
}

extern "C" bool lc_tl42r_get_param_descriptor(lc_tl42r_param_id_t param,
                                              lc_tl42r_param_descriptor_t *out_descriptor)
{
    return lc_tl42r_get_param_descriptor_internal(param, out_descriptor);
}

extern "C" int lc_tl42r_read_u16(lc_tl42r_device_t *dev,
                                 lc_tl42r_param_id_t param,
                                 uint16_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_U16) {
        return LC_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = lc_tl42r_read_param_raw(dev, &desc, &raw_value, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    *out_value = (uint16_t)raw_value;
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_read_i16(lc_tl42r_device_t *dev,
                                 lc_tl42r_param_id_t param,
                                 int16_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_I16) {
        return LC_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = lc_tl42r_read_param_raw(dev, &desc, &raw_value, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    *out_value = (int16_t)(uint16_t)raw_value;
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_read_u32(lc_tl42r_device_t *dev,
                                 lc_tl42r_param_id_t param,
                                 uint32_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_U32) {
        return LC_TL42R_ERR_TYPE;
    }

    return lc_tl42r_read_param_raw(dev, &desc, out_value, timeout_ms);
}

extern "C" int lc_tl42r_read_i32(lc_tl42r_device_t *dev,
                                 lc_tl42r_param_id_t param,
                                 int32_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_I32) {
        return LC_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = lc_tl42r_read_param_raw(dev, &desc, &raw_value, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    *out_value = (int32_t)raw_value;
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_write_u16(lc_tl42r_device_t *dev,
                                  lc_tl42r_param_id_t param,
                                  uint16_t value,
                                  uint32_t timeout_ms)
{
    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_U16) {
        return LC_TL42R_ERR_TYPE;
    }

    return lc_tl42r_write_param_raw(dev, &desc, value, true, timeout_ms);
}

extern "C" int lc_tl42r_write_i16(lc_tl42r_device_t *dev,
                                  lc_tl42r_param_id_t param,
                                  int16_t value,
                                  uint32_t timeout_ms)
{
    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_I16) {
        return LC_TL42R_ERR_TYPE;
    }

    return lc_tl42r_write_param_raw(dev, &desc, (uint16_t)value, true, timeout_ms);
}

extern "C" int lc_tl42r_write_u32(lc_tl42r_device_t *dev,
                                  lc_tl42r_param_id_t param,
                                  uint32_t value,
                                  uint32_t timeout_ms)
{
    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_U32) {
        return LC_TL42R_ERR_TYPE;
    }

    return lc_tl42r_write_param_raw(dev, &desc, value, true, timeout_ms);
}

extern "C" int lc_tl42r_write_i32(lc_tl42r_device_t *dev,
                                  lc_tl42r_param_id_t param,
                                  int32_t value,
                                  uint32_t timeout_ms)
{
    lc_tl42r_param_descriptor_t desc = {};
    if (!lc_tl42r_get_param_descriptor_internal(param, &desc)) {
        return LC_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != LC_TL42R_PARAM_TYPE_I32) {
        return LC_TL42R_ERR_TYPE;
    }

    return lc_tl42r_write_param_raw(dev, &desc, (uint32_t)value, true, timeout_ms);
}

extern "C" int lc_tl42r_save_parameters(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_write_single_register(dev, LC_TL42R_REG_AUX_CONTROL, (uint16_t)LC_TL42R_AUX_SAVE_CURRENT, timeout_ms);
}

extern "C" int lc_tl42r_execute_aux_command(lc_tl42r_device_t *dev,
                                            lc_tl42r_aux_command_t command,
                                            uint32_t timeout_ms)
{
    const int ret = lc_tl42r_write_single_register(dev, LC_TL42R_REG_AUX_CONTROL, (uint16_t)command, timeout_ms);
    if (ret == LC_TL42R_OK) {
        lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_BASIC |
                                         LC_TL42R_CONFIG_CACHE_MULTI_POSITION |
                                         LC_TL42R_CONFIG_CACHE_MULTI_SPEED);
    }
    return ret;
}

extern "C" int lc_tl42r_read_versions(lc_tl42r_device_t *dev,
                                      uint16_t *out_software_version,
                                      uint16_t *out_hardware_version,
                                      uint32_t timeout_ms)
{
    if ((out_software_version == nullptr) || (out_hardware_version == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    int ret = lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_001, out_software_version, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_002, out_hardware_version, timeout_ms);
}

extern "C" int lc_tl42r_read_run_status(lc_tl42r_device_t *dev,
                                        uint16_t *out_status,
                                        uint32_t timeout_ms)
{
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_004, out_status, timeout_ms);
}

extern "C" int lc_tl42r_read_alarm(lc_tl42r_device_t *dev,
                                   uint16_t *out_alarm,
                                   uint32_t timeout_ms)
{
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_005, out_alarm, timeout_ms);
}

extern "C" int lc_tl42r_read_status_and_alarm(lc_tl42r_device_t *dev,
                                              uint16_t *out_status,
                                              uint16_t *out_alarm,
                                              uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_status == nullptr) || (out_alarm == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[2] = {};
    const int ret = lc_tl42r_read_registers(dev, 4u, 2u, regs, 2u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    *out_status = regs[0];
    *out_alarm = regs[1];
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_read_digital_inputs(lc_tl42r_device_t *dev,
                                            uint16_t *out_di_state,
                                            uint32_t timeout_ms)
{
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_006, out_di_state, timeout_ms);
}

extern "C" int lc_tl42r_read_digital_outputs(lc_tl42r_device_t *dev,
                                             uint16_t *out_do_state,
                                             uint32_t timeout_ms)
{
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_007, out_do_state, timeout_ms);
}

extern "C" int lc_tl42r_read_current_position(lc_tl42r_device_t *dev,
                                              int32_t *out_position_pulse,
                                              uint32_t timeout_ms)
{
    return lc_tl42r_read_i32(dev, LC_TL42R_PARAM_PA_008, out_position_pulse, timeout_ms);
}

extern "C" int lc_tl42r_read_current_speed(lc_tl42r_device_t *dev,
                                           int16_t *out_speed_rpm,
                                           uint32_t timeout_ms)
{
    return lc_tl42r_read_i16(dev, LC_TL42R_PARAM_PA_00A, out_speed_rpm, timeout_ms);
}

extern "C" int lc_tl42r_read_subdivision(lc_tl42r_device_t *dev,
                                         uint16_t *out_subdivision,
                                         uint32_t timeout_ms)
{
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_023, out_subdivision, timeout_ms);
}

extern "C" int lc_tl42r_read_monitor(lc_tl42r_device_t *dev,
                                     lc_tl42r_monitor_t *out_monitor,
                                     uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_monitor == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[LC_TL42R_MONITOR_REG_COUNT] = {};
    const int ret = lc_tl42r_read_registers(dev,
                                            LC_TL42R_MONITOR_REG_START,
                                            LC_TL42R_MONITOR_REG_COUNT,
                                            regs,
                                            LC_TL42R_MONITOR_REG_COUNT,
                                            timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_fill_monitor(out_monitor, regs);
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_read_drive_info(lc_tl42r_device_t *dev,
                                        uint16_t *out_run_mode,
                                        uint16_t *out_encoder_resolution,
                                        uint32_t timeout_ms)
{
    if ((out_run_mode == nullptr) || (out_encoder_resolution == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    int ret = lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_100, out_run_mode, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    return lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_101, out_encoder_resolution, timeout_ms);
}

extern "C" int lc_tl42r_read_factory_params(lc_tl42r_device_t *dev,
                                            lc_tl42r_factory_params_t *out_factory_params,
                                            uint32_t timeout_ms)
{
    return lc_tl42r_read_factory_params_internal(dev, out_factory_params, timeout_ms);
}

extern "C" int lc_tl42r_read_basic_config(lc_tl42r_device_t *dev,
                                          lc_tl42r_basic_config_t *out_basic_config,
                                          uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_basic_config == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    (void)memset(out_basic_config, 0, sizeof(*out_basic_config));

    uint16_t regs_16_21[6] = {};
    uint16_t regs_26_29[4] = {};
    uint16_t reg_31 = 0u;
    uint16_t regs_32_38[7] = {};
    uint16_t reg_40 = 0u;
    uint16_t regs_48_56[9] = {};
    uint16_t regs_64_74[11] = {};
    uint16_t regs_78_79[2] = {};

    int ret = lc_tl42r_read_registers(dev, 16u, 6u, regs_16_21, 6u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 26u, 4u, regs_26_29, 4u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_01F, &reg_31, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 32u, 7u, regs_32_38, 7u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_u16(dev, LC_TL42R_PARAM_PA_028, &reg_40, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 48u, 9u, regs_48_56, 9u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 64u, 11u, regs_64_74, 11u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_registers(dev, 78u, 2u, regs_78_79, 2u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    ret = lc_tl42r_read_factory_params_internal(dev, &out_basic_config->factory_params, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    out_basic_config->di_config.normally_closed_mask = regs_16_21[0];
    out_basic_config->di_config.input_function[0] = (lc_tl42r_di_function_t)regs_16_21[1];
    out_basic_config->di_config.input_function[1] = (lc_tl42r_di_function_t)regs_16_21[2];
    out_basic_config->di_config.input_function[2] = (lc_tl42r_di_function_t)regs_16_21[3];
    out_basic_config->di_config.input_function[3] = (lc_tl42r_di_function_t)regs_16_21[4];
    out_basic_config->di_config.input_function[4] = (lc_tl42r_di_function_t)regs_16_21[5];
    out_basic_config->di_config.filter_coefficient = regs_26_29[0];

    out_basic_config->do_config.normally_closed_mask = regs_26_29[1];
    out_basic_config->do_config.output_function[0] = (lc_tl42r_do_function_t)regs_26_29[2];
    out_basic_config->do_config.output_function[1] = (lc_tl42r_do_function_t)regs_26_29[3];
    out_basic_config->do_config.forced_output_mask = reg_31;

    out_basic_config->comm_config.station_id = regs_32_38[0];
    out_basic_config->comm_config.serial_format = (lc_tl42r_serial_format_t)regs_32_38[1];
    out_basic_config->comm_config.default_direction = regs_32_38[2];
    out_basic_config->comm_config.subdivision = regs_32_38[3];
    out_basic_config->comm_config.limit_stop_mode = regs_32_38[4];
    out_basic_config->comm_config.soft_limit_enable = regs_32_38[5];
    out_basic_config->comm_config.multi_position_trigger_mode = regs_32_38[6];
    out_basic_config->comm_config.baudrate = (lc_tl42r_baudrate_t)reg_40;

    out_basic_config->motion_config.jog_speed_rpm = (int16_t)regs_48_56[0];
    out_basic_config->motion_config.jog_accel_ms = regs_48_56[1];
    out_basic_config->motion_config.jog_decel_ms = regs_48_56[2];
    out_basic_config->motion_config.position_start_speed_rpm = regs_48_56[3];
    out_basic_config->motion_config.position_accel_ms = regs_48_56[4];
    out_basic_config->motion_config.position_decel_ms = regs_48_56[5];
    out_basic_config->motion_config.position_speed_rpm = regs_48_56[6];
    out_basic_config->motion_config.position_target_pulse = lc_tl42r_compose_position(regs_48_56[7], regs_48_56[8]);
    out_basic_config->motion_config.multi_position_mode = (lc_tl42r_multi_position_mode_t)regs_64_74[10];
    out_basic_config->motion_config.control_word = regs_78_79[0];
    out_basic_config->motion_config.aux_control = regs_78_79[1];

    out_basic_config->homing_config.homing_mode = (lc_tl42r_homing_mode_t)regs_64_74[0];
    out_basic_config->homing_config.homing_approach_speed_rpm = regs_64_74[1];
    out_basic_config->homing_config.homing_crawl_speed_rpm = regs_64_74[2];
    out_basic_config->homing_config.homing_accel_ms = regs_64_74[3];
    out_basic_config->homing_config.homing_offset_pulse = lc_tl42r_compose_position(regs_64_74[4], regs_64_74[5]);
    out_basic_config->homing_config.positive_soft_limit_pulse = lc_tl42r_compose_position(regs_64_74[6], regs_64_74[7]);
    out_basic_config->homing_config.negative_soft_limit_pulse = lc_tl42r_compose_position(regs_64_74[8], regs_64_74[9]);

    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_set_di_config(lc_tl42r_device_t *dev,
                                      const lc_tl42r_di_config_t *config,
                                      uint32_t timeout_ms)
{
    return lc_tl42r_write_di_config_internal(dev, config, true, timeout_ms);
}

extern "C" int lc_tl42r_set_do_config(lc_tl42r_device_t *dev,
                                      const lc_tl42r_do_config_t *config,
                                      uint32_t timeout_ms)
{
    return lc_tl42r_write_do_config_internal(dev, config, true, timeout_ms);
}

extern "C" int lc_tl42r_set_communication_config(lc_tl42r_device_t *dev,
                                                 const lc_tl42r_comm_config_t *config,
                                                 uint32_t timeout_ms)
{
    return lc_tl42r_write_comm_config_internal(dev, config, true, timeout_ms);
}

extern "C" int lc_tl42r_set_motion_config(lc_tl42r_device_t *dev,
                                          const lc_tl42r_motion_config_t *config,
                                          uint32_t timeout_ms)
{
    return lc_tl42r_write_motion_config_internal(dev, config, true, timeout_ms);
}

extern "C" int lc_tl42r_set_homing_config(lc_tl42r_device_t *dev,
                                          const lc_tl42r_homing_config_t *config,
                                          uint32_t timeout_ms)
{
    return lc_tl42r_write_homing_config_internal(dev, config, true, timeout_ms);
}

extern "C" int lc_tl42r_read_multi_position_segment(lc_tl42r_device_t *dev,
                                                    uint8_t segment_index,
                                                    lc_tl42r_position_segment_t *out_segment,
                                                    uint32_t timeout_ms)
{
    return lc_tl42r_read_multi_position_segment_internal(dev, segment_index, out_segment, timeout_ms);
}

extern "C" int lc_tl42r_write_multi_position_segment(lc_tl42r_device_t *dev,
                                                     uint8_t segment_index,
                                                     const lc_tl42r_position_segment_t *segment,
                                                     uint32_t timeout_ms)
{
    return lc_tl42r_write_position_segment_internal(dev, segment_index, segment, true, timeout_ms);
}

extern "C" int lc_tl42r_read_multi_position_table(lc_tl42r_device_t *dev,
                                                  lc_tl42r_multi_position_table_t *out_table,
                                                  uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_table == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    (void)memset(out_table, 0, sizeof(*out_table));
    for (uint8_t i = 0u; i < LC_TL42R_MULTI_POSITION_SEGMENT_COUNT; ++i) {
        const int ret = lc_tl42r_read_multi_position_segment_internal(dev,
                                                                      i,
                                                                      &out_table->segments[i],
                                                                      timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
    }
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_write_multi_position_table(lc_tl42r_device_t *dev,
                                                   const lc_tl42r_multi_position_table_t *table,
                                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (table == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    for (uint8_t i = 0u; i < LC_TL42R_MULTI_POSITION_SEGMENT_COUNT; ++i) {
        const int ret = lc_tl42r_write_position_segment_internal(dev,
                                                                 i,
                                                                 &table->segments[i],
                                                                 false,
                                                                 timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
    }

    return lc_tl42r_save_parameters(dev, timeout_ms);
}

extern "C" int lc_tl42r_read_multi_speed_segment(lc_tl42r_device_t *dev,
                                                 uint8_t segment_index,
                                                 lc_tl42r_speed_segment_t *out_segment,
                                                 uint32_t timeout_ms)
{
    return lc_tl42r_read_multi_speed_segment_internal(dev, segment_index, out_segment, timeout_ms);
}

extern "C" int lc_tl42r_write_multi_speed_segment(lc_tl42r_device_t *dev,
                                                  uint8_t segment_index,
                                                  const lc_tl42r_speed_segment_t *segment,
                                                  uint32_t timeout_ms)
{
    return lc_tl42r_write_speed_segment_internal(dev, segment_index, segment, true, timeout_ms);
}

extern "C" int lc_tl42r_read_multi_speed_table(lc_tl42r_device_t *dev,
                                               lc_tl42r_multi_speed_table_t *out_table,
                                               uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_table == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[48] = {};
    const int ret = lc_tl42r_read_registers(dev, 176u, 48u, regs, 48u, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    for (uint8_t i = 0u; i < LC_TL42R_MULTI_SPEED_SEGMENT_COUNT; ++i) {
        out_table->segments[i].speed_rpm = (int16_t)regs[i * 3u];
        out_table->segments[i].accel_ms = regs[i * 3u + 1u];
        out_table->segments[i].decel_ms = regs[i * 3u + 2u];
    }
    return LC_TL42R_OK;
}

extern "C" int lc_tl42r_write_multi_speed_table(lc_tl42r_device_t *dev,
                                                const lc_tl42r_multi_speed_table_t *table,
                                                uint32_t timeout_ms)
{
    if ((dev == nullptr) || (table == nullptr)) {
        return LC_TL42R_ERR_PARAM;
    }

    uint16_t regs[48] = {};
    for (uint8_t i = 0u; i < LC_TL42R_MULTI_SPEED_SEGMENT_COUNT; ++i) {
        regs[i * 3u] = (uint16_t)table->segments[i].speed_rpm;
        regs[i * 3u + 1u] = table->segments[i].accel_ms;
        regs[i * 3u + 2u] = table->segments[i].decel_ms;
    }

    const int ret = lc_tl42r_write_multiple_registers(dev, 176u, 48u, regs, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    lc_tl42r_invalidate_config_cache(LC_TL42R_CONFIG_CACHE_MULTI_SPEED);
    return lc_tl42r_save_parameters(dev, timeout_ms);
}

extern "C" int lc_tl42r_set_position_speed(lc_tl42r_device_t *dev,
                                           uint16_t speed_rpm,
                                           uint32_t timeout_ms)
{
    return lc_tl42r_write_single_register(dev, LC_TL42R_REG_POSITION_SPEED, speed_rpm, timeout_ms);
}

extern "C" int lc_tl42r_set_position_target(lc_tl42r_device_t *dev,
                                            int32_t target_pulse,
                                            uint32_t timeout_ms)
{
    const uint32_t raw_target = (uint32_t)target_pulse;
    const uint16_t regs[2] = {
        (uint16_t)(raw_target >> 16),
        (uint16_t)(raw_target & 0xFFFFu)
    };
    return lc_tl42r_write_multiple_registers(dev, LC_TL42R_REG_POSITION_TARGET_H, 2u, regs, timeout_ms);
}

extern "C" int lc_tl42r_write_control_word(lc_tl42r_device_t *dev,
                                           uint16_t control_word,
                                           uint32_t timeout_ms)
{
    return lc_tl42r_write_single_register(dev, LC_TL42R_REG_CONTROL_WORD, control_word, timeout_ms);
}

extern "C" int lc_tl42r_start_relative_move(lc_tl42r_device_t *dev,
                                            int32_t target_pulse,
                                            uint16_t speed_rpm,
                                            bool interrupt_current,
                                            uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    int ret = lc_tl42r_set_position_target(dev, target_pulse, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    ret = lc_tl42r_set_position_speed(dev, speed_rpm, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    uint16_t control_word = LC_TL42R_CTRL_POSITION_TRIGGER;
    if (interrupt_current) {
        control_word |= LC_TL42R_CTRL_INTERRUPT_MODE;
    }
    return lc_tl42r_write_control_word(dev, control_word, timeout_ms);
}

extern "C" int lc_tl42r_start_absolute_move(lc_tl42r_device_t *dev,
                                            int32_t target_pulse,
                                            uint16_t speed_rpm,
                                            bool interrupt_current,
                                            uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    int ret = lc_tl42r_set_position_target(dev, target_pulse, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    ret = lc_tl42r_set_position_speed(dev, speed_rpm, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }

    uint16_t control_word = (uint16_t)(LC_TL42R_CTRL_POSITION_TRIGGER | LC_TL42R_CTRL_ABSOLUTE_MODE);
    if (interrupt_current) {
        control_word |= LC_TL42R_CTRL_INTERRUPT_MODE;
    }
    return lc_tl42r_write_control_word(dev, control_word, timeout_ms);
}

extern "C" int lc_tl42r_start_jog(lc_tl42r_device_t *dev,
                                  int16_t speed_rpm,
                                  uint16_t accel_ms,
                                  uint16_t decel_ms,
                                  uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }

    const uint16_t regs[3] = {
        (uint16_t)speed_rpm,
        accel_ms,
        decel_ms
    };

    int ret = lc_tl42r_write_multiple_registers(dev, 48u, 3u, regs, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    return lc_tl42r_write_control_word(dev, LC_TL42R_CTRL_JOG_TRIGGER, timeout_ms);
}

extern "C" int lc_tl42r_start_homing(lc_tl42r_device_t *dev,
                                     lc_tl42r_homing_mode_t homing_mode,
                                     uint16_t approach_speed_rpm,
                                     uint16_t crawl_speed_rpm,
                                     uint16_t accel_ms,
                                     int32_t offset_pulse,
                                     uint32_t timeout_ms)
{
    lc_tl42r_homing_config_t config = {};
    config.homing_mode = homing_mode;
    config.homing_approach_speed_rpm = approach_speed_rpm;
    config.homing_crawl_speed_rpm = crawl_speed_rpm;
    config.homing_accel_ms = accel_ms;
    config.homing_offset_pulse = offset_pulse;
    config.positive_soft_limit_pulse = 0;
    config.negative_soft_limit_pulse = 0;

    int ret = lc_tl42r_write_homing_config_internal(dev, &config, false, timeout_ms);
    if (ret != LC_TL42R_OK) {
        return ret;
    }
    return lc_tl42r_write_control_word(dev, LC_TL42R_CTRL_HOMING_TRIGGER, timeout_ms);
}

extern "C" int lc_tl42r_stop(lc_tl42r_device_t *dev,
                             bool emergency_stop,
                             uint32_t timeout_ms)
{
    return lc_tl42r_write_control_word(dev,
                                       emergency_stop ? LC_TL42R_CTRL_ESTOP : LC_TL42R_CTRL_STOP,
                                       timeout_ms);
}

extern "C" int lc_tl42r_estop(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_stop(dev, true, timeout_ms);
}

extern "C" int lc_tl42r_restore_factory_defaults(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_execute_aux_command(dev, LC_TL42R_AUX_RESTORE_FACTORY, timeout_ms);
}

extern "C" int lc_tl42r_clear_alarm(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_execute_aux_command(dev, LC_TL42R_AUX_CLEAR_ALARM, timeout_ms);
}

extern "C" int lc_tl42r_clear_position(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_execute_aux_command(dev, LC_TL42R_AUX_CLEAR_POSITION, timeout_ms);
}

extern "C" int lc_tl42r_motor_enable(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_execute_aux_command(dev, LC_TL42R_AUX_MOTOR_ENABLE, timeout_ms);
}

extern "C" int lc_tl42r_motor_release(lc_tl42r_device_t *dev, uint32_t timeout_ms)
{
    return lc_tl42r_execute_aux_command(dev, LC_TL42R_AUX_MOTOR_RELEASE, timeout_ms);
}

extern "C" void lc_tl42r_watch_reset(uint8_t port, uint8_t unit_id, int32_t pulses_per_rev)
{
    lc_tl42r_watch_t watch = {};
    watch.online = false;
    watch.port = port;
    watch.unit_id = unit_id;
    watch.last_error = LC_TL42R_OK;
    watch.pulses_per_rev = pulses_per_rev;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_set_versions(uint16_t software_version, uint16_t hardware_version)
{
    lc_tl42r_watch_t watch = {};
    (void)lc_tl42r_load_watch(&watch);
    watch.software_version = software_version;
    watch.hardware_version = hardware_version;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_set_drive_info(uint16_t run_mode, uint16_t encoder_resolution)
{
    lc_tl42r_watch_t watch = {};
    (void)lc_tl42r_load_watch(&watch);
    watch.run_mode = run_mode;
    watch.encoder_resolution = encoder_resolution;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_set_pulses_per_rev(int32_t pulses_per_rev)
{
    lc_tl42r_watch_t watch = {};
    (void)lc_tl42r_load_watch(&watch);
    watch.pulses_per_rev = pulses_per_rev;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_set_zero_current(void)
{
    lc_tl42r_watch_t watch = {};
    if (!lc_tl42r_load_watch(&watch) || !watch.initialized) {
        return;
    }

    watch.zero_offset_pulse = watch.tracked_position_pulse;
    watch.relative_position_pulse = 0;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_mark_error(int error_code, uint32_t now_ms)
{
    lc_tl42r_watch_t watch = {};
    (void)lc_tl42r_load_watch(&watch);
    watch.online = false;
    watch.last_update_ms = now_ms;
    watch.last_error = error_code;
    watch.error_count++;
    lc_tl42r_store_watch(&watch);
}

extern "C" void lc_tl42r_watch_update(const lc_tl42r_monitor_t *monitor, uint32_t now_ms)
{
    if (monitor == nullptr) {
        return;
    }

    lc_tl42r_watch_t watch = {};
    (void)lc_tl42r_load_watch(&watch);

    const uint32_t raw_now = ((uint32_t)monitor->raw_position_high << 16) |
                             (uint32_t)monitor->raw_position_low;

    watch.online = true;
    watch.last_update_ms = now_ms;
    watch.ok_count++;
    watch.last_error = LC_TL42R_OK;
    watch.raw_position_u32 = raw_now;
    watch.monitor = *monitor;

    if (!watch.initialized) {
        watch.initialized = true;
        watch.last_raw_position_u32 = raw_now;
        watch.tracked_position_pulse = (int64_t)(int32_t)raw_now;
        watch.zero_offset_pulse = watch.tracked_position_pulse;
    } else {
        const int32_t delta = (int32_t)(raw_now - watch.last_raw_position_u32);
        watch.tracked_position_pulse += (int64_t)delta;
        watch.last_raw_position_u32 = raw_now;
    }

    watch.relative_position_pulse = watch.tracked_position_pulse - watch.zero_offset_pulse;
    lc_tl42r_store_watch(&watch);
}

extern "C" bool lc_tl42r_get_watch(lc_tl42r_watch_t *out_watch)
{
    if (out_watch == nullptr) {
        return false;
    }
    (void)lc_tl42r_load_watch(out_watch);
    return out_watch->initialized;
}

extern "C" void lc_tl42r_config_cache_reset(void)
{
    lc_tl42r_config_cache_t cache = {};
    lc_tl42r_store_config_cache(&cache);
}

extern "C" int lc_tl42r_refresh_config_cache(lc_tl42r_device_t *dev,
                                             uint32_t refresh_mask,
                                             uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return LC_TL42R_ERR_PARAM;
    }
    if ((refresh_mask & LC_TL42R_CONFIG_CACHE_ALL) == 0u) {
        return LC_TL42R_ERR_PARAM;
    }

    lc_tl42r_config_cache_t cache = {};
    (void)lc_tl42r_load_config_cache(&cache);

    int ret = LC_TL42R_OK;
    if ((refresh_mask & LC_TL42R_CONFIG_CACHE_BASIC) != 0u) {
        ret = lc_tl42r_read_basic_config(dev, &cache.basic, timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
        cache.basic_valid = true;
    }
    if ((refresh_mask & LC_TL42R_CONFIG_CACHE_MULTI_POSITION) != 0u) {
        ret = lc_tl42r_read_multi_position_table(dev, &cache.position_table, timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
        cache.position_table_valid = true;
    }
    if ((refresh_mask & LC_TL42R_CONFIG_CACHE_MULTI_SPEED) != 0u) {
        ret = lc_tl42r_read_multi_speed_table(dev, &cache.speed_table, timeout_ms);
        if (ret != LC_TL42R_OK) {
            return ret;
        }
        cache.speed_table_valid = true;
    }

    cache.initialized = true;
    cache.last_refresh_ms = tl42r_now_ms();
    lc_tl42r_store_config_cache(&cache);
    return LC_TL42R_OK;
}

extern "C" bool lc_tl42r_get_config_cache(lc_tl42r_config_cache_t *out_cache)
{
    if (out_cache == nullptr) {
        return false;
    }
    (void)lc_tl42r_load_config_cache(out_cache);
    return out_cache->initialized;
}

extern "C" void lc_tl42r_decode_run_status(uint16_t raw_status, lc_tl42r_run_status_bits_t *out_status)
{
    if (out_status == nullptr) {
        return;
    }

    out_status->in_position = (raw_status & LC_TL42R_STATUS_IN_POSITION) != 0u;
    out_status->homing_done = (raw_status & LC_TL42R_STATUS_HOMING_DONE) != 0u;
    out_status->motor_running = (raw_status & LC_TL42R_STATUS_MOTOR_RUNNING) != 0u;
    out_status->fault = (raw_status & LC_TL42R_STATUS_FAULT) != 0u;
    out_status->enabled = (raw_status & LC_TL42R_STATUS_ENABLED) != 0u;
    out_status->positive_soft_limit = (raw_status & LC_TL42R_STATUS_POSITIVE_SOFT_LIM) != 0u;
    out_status->negative_soft_limit = (raw_status & LC_TL42R_STATUS_NEGATIVE_SOFT_LIM) != 0u;
}

extern "C" void lc_tl42r_decode_di_state(uint16_t raw_di_state, lc_tl42r_di_state_bits_t *out_state)
{
    if (out_state == nullptr) {
        return;
    }

    out_state->di0 = (raw_di_state & (1u << 0)) != 0u;
    out_state->di1 = (raw_di_state & (1u << 1)) != 0u;
    out_state->di2 = (raw_di_state & (1u << 2)) != 0u;
    out_state->di3 = (raw_di_state & (1u << 3)) != 0u;
    out_state->di4 = (raw_di_state & (1u << 4)) != 0u;
}

extern "C" void lc_tl42r_decode_do_state(uint16_t raw_do_state, lc_tl42r_do_state_bits_t *out_state)
{
    if (out_state == nullptr) {
        return;
    }

    out_state->do0 = (raw_do_state & (1u << 0)) != 0u;
    out_state->do1 = (raw_do_state & (1u << 1)) != 0u;
    out_state->do2 = (raw_do_state & (1u << 2)) != 0u;
}

extern "C" int32_t lc_tl42r_compose_position(uint16_t position_high, uint16_t position_low)
{
    return (int32_t)(((uint32_t)position_high << 16) | (uint32_t)position_low);
}

extern "C" int16_t lc_tl42r_decode_speed(uint16_t raw_speed)
{
    return (int16_t)raw_speed;
}

extern "C" const char *lc_tl42r_alarm_to_string(uint16_t alarm_code)
{
    switch (alarm_code) {
        case LC_TL42R_ALARM_NONE:
            return "none";
        case LC_TL42R_ALARM_OVERCURRENT:
            return "overcurrent";
        case LC_TL42R_ALARM_OVERVOLTAGE:
            return "overvoltage";
        case LC_TL42R_ALARM_UNDERVOLTAGE:
            return "undervoltage";
        default:
            return "unknown";
    }
}

extern "C" const char *lc_tl42r_run_mode_to_string(uint16_t run_mode)
{
    switch (run_mode) {
        case LC_TL42R_RUN_MODE_OPEN_LOOP:
            return "open_loop";
        case LC_TL42R_RUN_MODE_CLOSED_LOOP:
            return "closed_loop";
        default:
            return "unknown";
    }
}

extern "C" const char *lc_tl42r_error_to_string(int error_code)
{
    switch (error_code) {
        case LC_TL42R_OK:
            return "ok";
        case LC_TL42R_ERR_PARAM:
            return "tl42r_param";
        case LC_TL42R_ERR_STATE:
            return "tl42r_state";
        case LC_TL42R_ERR_LENGTH:
            return "tl42r_length";
        case LC_TL42R_ERR_ACCESS:
            return "tl42r_access";
        case LC_TL42R_ERR_TYPE:
            return "tl42r_type";
        case LC_TL42R_ERR_RANGE:
            return "tl42r_range";
        case LC_TL42R_ERR_NOT_FOUND:
            return "tl42r_not_found";
        case MODBUS_RTU_ERR_PARAM:
            return "modbus_param";
        case MODBUS_RTU_ERR_STATE:
            return "modbus_state";
        case MODBUS_RTU_ERR_FULL:
            return "modbus_full";
        case MODBUS_RTU_ERR_TIMEOUT:
            return "modbus_timeout";
        case MODBUS_RTU_ERR_CRC:
            return "modbus_crc";
        case MODBUS_RTU_ERR_LENGTH:
            return "modbus_length";
        case MODBUS_RTU_ERR_EXCEPTION:
            return "modbus_exception";
        case MODBUS_RTU_ERR_FUNCTION:
            return "modbus_function";
        default:
            return "unknown";
    }
}
