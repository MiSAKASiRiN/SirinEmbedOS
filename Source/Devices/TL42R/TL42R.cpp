#include "SirinEmbedOS/Devices/TL42R/TL42R.h"
#include "IO/RS485/rs485_internal.h"
#include "SirinEmbedOS/Core/Array.hpp"

#include <cstring>

#include "cmsis_os2.h"
static uint32_t tl42r_now_ms() { return uint32_t(uint64_t(osKernelGetTickCount()) * 1000 / osKernelGetTickFreq()); }

namespace {

static volatile uint32_t s_lc_tl42r_watch_seq = 0u;
static volatile uint32_t s_lc_tl42r_config_cache_seq = 0u;

static void SRNOS_TL42RCopyToVolatile(volatile void *dst, const void *src, size_t size)
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

static void SRNOS_TL42RCopyFromVolatile(void *dst, const volatile void *src, size_t size)
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

static void SRNOS_TL42RStoreWatch(const SRNOS_TL42RWatch *watch)
{
    if (watch == nullptr) {
        return;
    }

    __atomic_fetch_add(&s_lc_tl42r_watch_seq, 1u, __ATOMIC_ACQ_REL);
    SRNOS_TL42RCopyToVolatile(&SRNOS_DebugTL42RWatch, watch, sizeof(*watch));
    __atomic_fetch_add(&s_lc_tl42r_watch_seq, 1u, __ATOMIC_RELEASE);
}

static bool SRNOS_TL42RLoadWatch(SRNOS_TL42RWatch *out_watch)
{
    if (out_watch == nullptr) {
        return false;
    }

    for (uint8_t i = 0u; i < 3u; ++i) {
        const uint32_t seq_start = __atomic_load_n(&s_lc_tl42r_watch_seq, __ATOMIC_ACQUIRE);
        if ((seq_start & 1u) != 0u) {
            continue;
        }

        SRNOS_TL42RCopyFromVolatile(out_watch, &SRNOS_DebugTL42RWatch, sizeof(*out_watch));
        const uint32_t seq_end = __atomic_load_n(&s_lc_tl42r_watch_seq, __ATOMIC_ACQUIRE);
        if (seq_start == seq_end) {
            return true;
        }
    }

    SRNOS_TL42RCopyFromVolatile(out_watch, &SRNOS_DebugTL42RWatch, sizeof(*out_watch));
    return true;
}

static void SRNOS_TL42RStoreConfigCache(const SRNOS_TL42RConfigCache *cache)
{
    if (cache == nullptr) {
        return;
    }

    __atomic_fetch_add(&s_lc_tl42r_config_cache_seq, 1u, __ATOMIC_ACQ_REL);
    SRNOS_TL42RCopyToVolatile(&SRNOS_DebugTL42RConfigCache, cache, sizeof(*cache));
    __atomic_fetch_add(&s_lc_tl42r_config_cache_seq, 1u, __ATOMIC_RELEASE);
}

static bool SRNOS_TL42RLoadConfigCache(SRNOS_TL42RConfigCache *out_cache)
{
    if (out_cache == nullptr) {
        return false;
    }

    for (uint8_t i = 0u; i < 3u; ++i) {
        const uint32_t seq_start = __atomic_load_n(&s_lc_tl42r_config_cache_seq, __ATOMIC_ACQUIRE);
        if ((seq_start & 1u) != 0u) {
            continue;
        }

        SRNOS_TL42RCopyFromVolatile(out_cache, &SRNOS_DebugTL42RConfigCache, sizeof(*out_cache));
        const uint32_t seq_end = __atomic_load_n(&s_lc_tl42r_config_cache_seq, __ATOMIC_ACQUIRE);
        if (seq_start == seq_end) {
            return true;
        }
    }

    SRNOS_TL42RCopyFromVolatile(out_cache, &SRNOS_DebugTL42RConfigCache, sizeof(*out_cache));
    return true;
}

static void SRNOS_TL42RInvalidateConfigCache(uint32_t mask)
{
    SRNOS_TL42RConfigCache cache = {};
    (void)SRNOS_TL42RLoadConfigCache(&cache);

    if ((mask & SRNOS_TL42R_CONFIG_CACHE_BASIC) != 0u) {
        cache.basic_valid = false;
    }
    if ((mask & SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION) != 0u) {
        cache.position_table_valid = false;
    }
    if ((mask & SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED) != 0u) {
        cache.speed_table_valid = false;
    }
    if (mask != 0u) {
        cache.initialized = true;
        cache.last_refresh_ms = tl42r_now_ms();
        SRNOS_TL42RStoreConfigCache(&cache);
    }
}

typedef struct {
    SRNOS_TL42RParamId param;
    uint16_t address;
    SRNOS_TL42RParamType type;
    bool readable;
    bool writable;
    bool persistent;
    SRNOS_TL42RParamGroup group;
} SRNOS_TL42RParamDescriptorEntry;

static const SRNOS_TL42RParamDescriptorEntry s_base_descriptors[] = {
    {SRNOS_TL42R_PARAM_PA_001, 1u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_002, 2u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_004, 4u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_005, 5u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_006, 6u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_007, 7u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_008, 8u, SRNOS_TL42R_PARAM_TYPE_I32, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_009, 9u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},
    {SRNOS_TL42R_PARAM_PA_00A, 10u, SRNOS_TL42R_PARAM_TYPE_I16, true, false, false, SRNOS_TL42R_PARAM_GROUP_MONITOR},

    {SRNOS_TL42R_PARAM_PA_010, 16u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_011, 17u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_012, 18u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_013, 19u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_014, 20u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_015, 21u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_01A, 26u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_01B, 27u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_01C, 28u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_01D, 29u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},
    {SRNOS_TL42R_PARAM_PA_01F, 31u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_DIO},

    {SRNOS_TL42R_PARAM_PA_020, 32u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_021, 33u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_022, 34u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_023, 35u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_024, 36u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_025, 37u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_026, 38u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_028, 40u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_030, 48u, SRNOS_TL42R_PARAM_TYPE_I16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_031, 49u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_032, 50u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_033, 51u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_034, 52u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_035, 53u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_036, 54u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_037, 55u, SRNOS_TL42R_PARAM_TYPE_I32, true, true, false, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_038, 56u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, false, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_040, 64u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_041, 65u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_042, 66u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_043, 67u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_044, 68u, SRNOS_TL42R_PARAM_TYPE_I32, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_045, 69u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_046, 70u, SRNOS_TL42R_PARAM_TYPE_I32, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_047, 71u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_048, 72u, SRNOS_TL42R_PARAM_TYPE_I32, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_049, 73u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_04A, 74u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, true, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_04E, 78u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, false, SRNOS_TL42R_PARAM_GROUP_COMM},
    {SRNOS_TL42R_PARAM_PA_04F, 79u, SRNOS_TL42R_PARAM_TYPE_U16, true, true, false, SRNOS_TL42R_PARAM_GROUP_COMM},

    {SRNOS_TL42R_PARAM_PA_100, 256u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_101, 257u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_102, 258u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_103, 259u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_104, 260u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_105, 261u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_106, 262u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_107, 263u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_109, 265u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10A, 266u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10B, 267u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10C, 268u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10D, 269u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10E, 270u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_10F, 271u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_110, 272u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_111, 273u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_112, 274u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_113, 275u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_114, 276u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_115, 277u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_116, 278u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_117, 279u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_118, 280u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY},
    {SRNOS_TL42R_PARAM_PA_119, 281u, SRNOS_TL42R_PARAM_TYPE_U16, true, false, false, SRNOS_TL42R_PARAM_GROUP_FACTORY}
};

static uint32_t SRNOS_TL42RCacheMaskFromGroup(SRNOS_TL42RParamGroup group)
{
    switch (group) {
        case SRNOS_TL42R_PARAM_GROUP_DIO:
        case SRNOS_TL42R_PARAM_GROUP_COMM:
        case SRNOS_TL42R_PARAM_GROUP_FACTORY:
            return SRNOS_TL42R_CONFIG_CACHE_BASIC;
        case SRNOS_TL42R_PARAM_GROUP_MULTI_POSITION:
            return SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION;
        case SRNOS_TL42R_PARAM_GROUP_MULTI_SPEED:
            return SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED;
        default:
            return 0u;
    }
}

static bool SRNOS_TL42RLookupBaseDescriptor(SRNOS_TL42RParamId param,
                                            SRNOS_TL42RParamDescriptor *out_descriptor)
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

static bool SRNOS_TL42RLookupMultiPositionDescriptor(SRNOS_TL42RParamId param,
                                                      SRNOS_TL42RParamDescriptor *out_descriptor)
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
    out_descriptor->group = SRNOS_TL42R_PARAM_GROUP_MULTI_POSITION;

    switch (slot) {
        case 0u:
            out_descriptor->type = SRNOS_TL42R_PARAM_TYPE_I32;
            return true;
        case 1u:
            out_descriptor->type = SRNOS_TL42R_PARAM_TYPE_U16;
            return true;
        case 2u:
        case 3u:
        case 4u:
            out_descriptor->type = SRNOS_TL42R_PARAM_TYPE_U16;
            return true;
        default:
            return false;
    }
}

static bool SRNOS_TL42RLookupMultiSpeedDescriptor(SRNOS_TL42RParamId param,
                                                   SRNOS_TL42RParamDescriptor *out_descriptor)
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
    out_descriptor->group = SRNOS_TL42R_PARAM_GROUP_MULTI_SPEED;
    out_descriptor->type = (slot == 0u) ? SRNOS_TL42R_PARAM_TYPE_I16 : SRNOS_TL42R_PARAM_TYPE_U16;
    return true;
}

static bool SRNOS_TL42RGetParamDescriptorInternal(SRNOS_TL42RParamId param,
                                                   SRNOS_TL42RParamDescriptor *out_descriptor)
{
    if (out_descriptor == nullptr) {
        return false;
    }

    if (SRNOS_TL42RLookupBaseDescriptor(param, out_descriptor)) {
        return true;
    }
    if (SRNOS_TL42RLookupMultiPositionDescriptor(param, out_descriptor)) {
        return true;
    }
    if (SRNOS_TL42RLookupMultiSpeedDescriptor(param, out_descriptor)) {
        return true;
    }
    return false;
}

static int SRNOS_TL42RReadRegisters(SRNOS_TL42RDevice *dev,
                                   uint16_t start_addr,
                                   uint16_t quantity,
                                   uint16_t *out_regs,
                                   uint16_t out_capacity,
                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_regs == nullptr) || (quantity == 0u)) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if (out_capacity < quantity) {
        return SRNOS_TL42R_ERR_LENGTH;
    }
    if (dev->SRNOS_ModbusClient.router_handle == SRNOS_RS485_ROUTER_INVALID_HANDLE) {
        return SRNOS_TL42R_ERR_STATE;
    }

    dev->last_error = SRNOS_TL42R_OK;
    dev->last_exception_code = 0u;

    SRNOS_ModbusRtuClientFlushRx(&dev->SRNOS_ModbusClient);

    int ret = SRNOS_ModbusRtuReadHoldingRegisters(&dev->SRNOS_ModbusClient, start_addr, quantity, timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    SRNOS_ModbusRtuFrame frame = {};
    ret = SRNOS_ModbusRtuClientWaitResponse(&dev->SRNOS_ModbusClient, &frame, timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = SRNOS_ModbusRtuValidateResponse(&dev->SRNOS_ModbusClient, &frame, SRNOS_MODBUS_RTU_FC_READ_HOLDING_REGISTERS);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        if (ret == SRNOS_MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t reg_count = 0u;
    ret = SRNOS_ModbusRtuParseReadRegistersResponse(&frame,
                                                   SRNOS_MODBUS_RTU_FC_READ_HOLDING_REGISTERS,
                                                   out_regs,
                                                   out_capacity,
                                                   &reg_count);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if (reg_count != quantity) {
        dev->last_error = SRNOS_TL42R_ERR_LENGTH;
        return SRNOS_TL42R_ERR_LENGTH;
    }

    dev->last_error = SRNOS_TL42R_OK;
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteSingleRegister(SRNOS_TL42RDevice *dev,
                                          uint16_t reg_addr,
                                          uint16_t value,
                                          uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if (dev->SRNOS_ModbusClient.router_handle == SRNOS_RS485_ROUTER_INVALID_HANDLE) {
        return SRNOS_TL42R_ERR_STATE;
    }

    dev->last_error = SRNOS_TL42R_OK;
    dev->last_exception_code = 0u;

    SRNOS_ModbusRtuClientFlushRx(&dev->SRNOS_ModbusClient);

    int ret = SRNOS_ModbusRtuWriteSingleRegister(&dev->SRNOS_ModbusClient, reg_addr, value, timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    SRNOS_ModbusRtuFrame frame = {};
    ret = SRNOS_ModbusRtuClientWaitResponse(&dev->SRNOS_ModbusClient, &frame, timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = SRNOS_ModbusRtuValidateResponse(&dev->SRNOS_ModbusClient, &frame, SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_REGISTER);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        if (ret == SRNOS_MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t echoed_addr = 0u;
    uint16_t echoed_value = 0u;
    ret = SRNOS_ModbusRtuParseWriteSingleResponse(&frame,
                                                 SRNOS_MODBUS_RTU_FC_WRITE_SINGLE_REGISTER,
                                                 &echoed_addr,
                                                 &echoed_value);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if ((echoed_addr != reg_addr) || (echoed_value != value)) {
        dev->last_error = SRNOS_TL42R_ERR_STATE;
        return SRNOS_TL42R_ERR_STATE;
    }

    dev->last_error = SRNOS_TL42R_OK;
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteMultipleRegisters(SRNOS_TL42RDevice *dev,
                                             uint16_t start_addr,
                                             uint16_t quantity,
                                             const uint16_t *values,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (values == nullptr) || (quantity == 0u)) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if (dev->SRNOS_ModbusClient.router_handle == SRNOS_RS485_ROUTER_INVALID_HANDLE) {
        return SRNOS_TL42R_ERR_STATE;
    }

    dev->last_error = SRNOS_TL42R_OK;
    dev->last_exception_code = 0u;

    SRNOS_ModbusRtuClientFlushRx(&dev->SRNOS_ModbusClient);

    int ret = SRNOS_ModbusRtuWriteMultipleRegisters(&dev->SRNOS_ModbusClient,
                                                  start_addr,
                                                  quantity,
                                                  values,
                                                  timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    SRNOS_ModbusRtuFrame frame = {};
    ret = SRNOS_ModbusRtuClientWaitResponse(&dev->SRNOS_ModbusClient, &frame, timeout_ms);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }

    ret = SRNOS_ModbusRtuValidateResponse(&dev->SRNOS_ModbusClient, &frame, SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        if (ret == SRNOS_MODBUS_RTU_ERR_EXCEPTION) {
            dev->last_exception_code = frame.exception_code;
        }
        dev->last_error = ret;
        return ret;
    }

    uint16_t echoed_addr = 0u;
    uint16_t echoed_quantity = 0u;
    ret = SRNOS_ModbusRtuParseWriteMultipleResponse(&frame,
                                                   SRNOS_MODBUS_RTU_FC_WRITE_MULTIPLE_REGISTERS,
                                                   &echoed_addr,
                                                   &echoed_quantity);
    if (ret != SRNOS_MODBUS_RTU_OK) {
        dev->last_error = ret;
        return ret;
    }
    if ((echoed_addr != start_addr) || (echoed_quantity != quantity)) {
        dev->last_error = SRNOS_TL42R_ERR_STATE;
        return SRNOS_TL42R_ERR_STATE;
    }

    dev->last_error = SRNOS_TL42R_OK;
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RReadParamRaw(SRNOS_TL42RDevice *dev,
                                   const SRNOS_TL42RParamDescriptor *desc,
                                   uint32_t *out_raw_value,
                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (desc == nullptr) || (out_raw_value == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if (!desc->readable) {
        return SRNOS_TL42R_ERR_ACCESS;
    }

    uint16_t regs[2] = {};
    const uint16_t quantity = ((desc->type == SRNOS_TL42R_PARAM_TYPE_U32) || (desc->type == SRNOS_TL42R_PARAM_TYPE_I32)) ? 2u : 1u;
    const int ret = SRNOS_TL42RReadRegisters(dev, desc->address, quantity, regs, quantity, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    if (quantity == 1u) {
        *out_raw_value = regs[0];
    } else {
        *out_raw_value = ((uint32_t)regs[0] << 16) | (uint32_t)regs[1];
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteParamRaw(SRNOS_TL42RDevice *dev,
                                    const SRNOS_TL42RParamDescriptor *desc,
                                    uint32_t raw_value,
                                    bool auto_save,
                                    uint32_t timeout_ms)
{
    if ((dev == nullptr) || (desc == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if (!desc->writable) {
        return SRNOS_TL42R_ERR_ACCESS;
    }

    int ret = SRNOS_TL42R_OK;
    if ((desc->type == SRNOS_TL42R_PARAM_TYPE_U32) || (desc->type == SRNOS_TL42R_PARAM_TYPE_I32)) {
        const uint16_t regs[2] = {
            (uint16_t)(raw_value >> 16),
            (uint16_t)(raw_value & 0xFFFFu)
        };
        ret = SRNOS_TL42RWriteMultipleRegisters(dev, desc->address, 2u, regs, timeout_ms);
    } else {
        ret = SRNOS_TL42RWriteSingleRegister(dev, desc->address, (uint16_t)raw_value, timeout_ms);
    }
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42RCacheMaskFromGroup(desc->group));
    if (auto_save && desc->persistent) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static uint16_t SRNOS_TL42RMultiPositionSegmentBase(uint8_t segment_index)
{
    return (uint16_t)(80u + (uint16_t)segment_index * 6u);
}

static uint16_t SRNOS_TL42RMultiSpeedSegmentBase(uint8_t segment_index)
{
    return (uint16_t)(176u + (uint16_t)segment_index * 3u);
}

static void SRNOS_TL42RFillMonitor(SRNOS_TL42RMonitor *monitor, const uint16_t *regs)
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
    monitor->current_position_pulse = SRNOS_TL42RComposePosition(monitor->raw_position_high,
                                                                monitor->raw_position_low);
    monitor->current_speed_rpm = SRNOS_TL42RDecodeSpeed(monitor->raw_current_speed);
    SRNOS_TL42RDecodeRunStatus(monitor->raw_run_status, &monitor->run_status);
    SRNOS_TL42RDecodeDiState(monitor->raw_di_state, &monitor->di_state);
    SRNOS_TL42RDecodeDoState(monitor->raw_do_state, &monitor->do_state);
}

static void SRNOS_TL42RFillFactoryParams(SRNOS_TL42RFactoryParams *factory,
                                         const uint16_t *regs_256_263,
                                         const uint16_t *regs_265_281)
{
    if ((factory == nullptr) || (regs_256_263 == nullptr) || (regs_265_281 == nullptr)) {
        return;
    }

    (void)memset(factory, 0, sizeof(*factory));
    factory->run_mode = (SRNOS_TL42RRunMode)regs_256_263[0];
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

static int SRNOS_TL42RReadFactoryParamsInternal(SRNOS_TL42RDevice *dev,
                                                 SRNOS_TL42RFactoryParams *out_factory,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_factory == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs_256_263[8] = {};
    uint16_t regs_265_281[17] = {};

    int ret = SRNOS_TL42RReadRegisters(dev, 256u, 8u, regs_256_263, 8u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 265u, 17u, regs_265_281, 17u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RFillFactoryParams(out_factory, regs_256_263, regs_265_281);
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteDiConfigInternal(SRNOS_TL42RDevice *dev,
                                             const SRNOS_TL42RDiConfig *config,
                                             bool auto_save,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    const uint16_t regs_16_21[6] = {
        config->normally_closed_mask,
        (uint16_t)config->input_function[0],
        (uint16_t)config->input_function[1],
        (uint16_t)config->input_function[2],
        (uint16_t)config->input_function[3],
        (uint16_t)config->input_function[4]
    };

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 16u, 6u, regs_16_21, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RWriteSingleRegister(dev, 26u, config->filter_coefficient, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteDoConfigInternal(SRNOS_TL42RDevice *dev,
                                             const SRNOS_TL42RDoConfig *config,
                                             bool auto_save,
                                             uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    const uint16_t regs_27_29[3] = {
        config->normally_closed_mask,
        (uint16_t)config->output_function[0],
        (uint16_t)config->output_function[1]
    };

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 27u, 3u, regs_27_29, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RWriteSingleRegister(dev, 31u, config->forced_output_mask, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteCommConfigInternal(SRNOS_TL42RDevice *dev,
                                               const SRNOS_TL42RCommConfig *config,
                                               bool auto_save,
                                               uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
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

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 32u, 7u, regs_32_38, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RWriteSingleRegister(dev, 40u, (uint16_t)config->baudrate, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteMotionConfigInternal(SRNOS_TL42RDevice *dev,
                                                 const SRNOS_TL42RMotionConfig *config,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
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

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 48u, 7u, persistent_regs, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RWriteSingleRegister(dev, 74u, (uint16_t)config->multi_position_mode, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        ret = SRNOS_TL42RSaveParameters(dev, timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
    }

    return SRNOS_TL42RSetPositionTarget(dev, config->position_target_pulse, timeout_ms);
}

static int SRNOS_TL42RWriteHomingConfigInternal(SRNOS_TL42RDevice *dev,
                                                 const SRNOS_TL42RHomingConfig *config,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
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

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 64u, 10u, regs_64_73, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWritePositionSegmentInternal(SRNOS_TL42RDevice *dev,
                                                    uint8_t segment_index,
                                                    const SRNOS_TL42RPositionSegment *segment,
                                                    bool auto_save,
                                                    uint32_t timeout_ms)
{
    if ((dev == nullptr) || (segment == nullptr) || (segment_index >= SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    const uint32_t raw_target = (uint32_t)segment->target_pulse;
    const uint16_t regs[5] = {
        (uint16_t)(raw_target >> 16),
        (uint16_t)(raw_target & 0xFFFFu),
        segment->speed_rpm,
        segment->accel_ms,
        segment->decel_ms
    };

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev,
                                                SRNOS_TL42RMultiPositionSegmentBase(segment_index),
                                                5u,
                                                regs,
                                                timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RWriteSpeedSegmentInternal(SRNOS_TL42RDevice *dev,
                                                 uint8_t segment_index,
                                                 const SRNOS_TL42RSpeedSegment *segment,
                                                 bool auto_save,
                                                 uint32_t timeout_ms)
{
    if ((dev == nullptr) || (segment == nullptr) || (segment_index >= SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    const uint16_t regs[3] = {
        (uint16_t)segment->speed_rpm,
        segment->accel_ms,
        segment->decel_ms
    };

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev,
                                                SRNOS_TL42RMultiSpeedSegmentBase(segment_index),
                                                3u,
                                                regs,
                                                timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED);
    if (auto_save) {
        return SRNOS_TL42RSaveParameters(dev, timeout_ms);
    }
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RReadMultiPositionSegmentInternal(SRNOS_TL42RDevice *dev,
                                                         uint8_t segment_index,
                                                         SRNOS_TL42RPositionSegment *out_segment,
                                                         uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_segment == nullptr) || (segment_index >= SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[5] = {};
    const int ret = SRNOS_TL42RReadRegisters(dev,
                                            SRNOS_TL42RMultiPositionSegmentBase(segment_index),
                                            5u,
                                            regs,
                                            5u,
                                            timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    out_segment->target_pulse = SRNOS_TL42RComposePosition(regs[0], regs[1]);
    out_segment->speed_rpm = regs[2];
    out_segment->accel_ms = regs[3];
    out_segment->decel_ms = regs[4];
    return SRNOS_TL42R_OK;
}

static int SRNOS_TL42RReadMultiSpeedSegmentInternal(SRNOS_TL42RDevice *dev,
                                                      uint8_t segment_index,
                                                      SRNOS_TL42RSpeedSegment *out_segment,
                                                      uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_segment == nullptr) || (segment_index >= SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[3] = {};
    const int ret = SRNOS_TL42RReadRegisters(dev,
                                            SRNOS_TL42RMultiSpeedSegmentBase(segment_index),
                                            3u,
                                            regs,
                                            3u,
                                            timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    out_segment->speed_rpm = (int16_t)regs[0];
    out_segment->accel_ms = regs[1];
    out_segment->decel_ms = regs[2];
    return SRNOS_TL42R_OK;
}

} // namespace

extern "C" {
volatile SRNOS_TL42RWatch SRNOS_DebugTL42RWatch = {};
volatile SRNOS_TL42RConfigCache SRNOS_DebugTL42RConfigCache = {};
}

extern "C" void SRNOS_TL42RReset(SRNOS_TL42RDevice *dev)
{
    if (SRNOS_RS485IsFrozen()) return;
    if (dev == nullptr) {
        return;
    }

    (void)memset(dev, 0, sizeof(*dev));
}

extern "C" int SRNOS_TL42RInit(SRNOS_TL42RDevice *dev, uint8_t port, uint8_t unit_id)
{
    if (SRNOS_RS485IsFrozen()) return SRNOS_TL42R_ERR_STATE;
    if ((dev == nullptr) || (port == 0u) || (unit_id == 0u)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RReset(dev);
    dev->port = port;
    dev->unit_id = unit_id;

    const int ret = SRNOS_ModbusRtuClientInit(&dev->SRNOS_ModbusClient, port, unit_id);
    dev->last_error = ret;
    return ret;
}

extern "C" int SRNOS_TL42RDeinit(SRNOS_TL42RDevice *dev)
{
    if (SRNOS_RS485IsFrozen()) return SRNOS_TL42R_ERR_STATE;
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    if (dev->SRNOS_ModbusClient.router_handle == SRNOS_RS485_ROUTER_INVALID_HANDLE) {
        SRNOS_TL42RReset(dev);
        return SRNOS_TL42R_OK;
    }

    const int ret = SRNOS_ModbusRtuClientDeinit(&dev->SRNOS_ModbusClient);
    SRNOS_TL42RReset(dev);
    return ret;
}

extern "C" bool SRNOS_TL42RGetParamDescriptor(SRNOS_TL42RParamId param,
                                              SRNOS_TL42RParamDescriptor *out_descriptor)
{
    return SRNOS_TL42RGetParamDescriptorInternal(param, out_descriptor);
}

extern "C" int SRNOS_TL42RReadU16(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RParamId param,
                                 uint16_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_U16) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = SRNOS_TL42RReadParamRaw(dev, &desc, &raw_value, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    *out_value = (uint16_t)raw_value;
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RReadI16(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RParamId param,
                                 int16_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_I16) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = SRNOS_TL42RReadParamRaw(dev, &desc, &raw_value, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    *out_value = (int16_t)(uint16_t)raw_value;
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RReadU32(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RParamId param,
                                 uint32_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_U32) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    return SRNOS_TL42RReadParamRaw(dev, &desc, out_value, timeout_ms);
}

extern "C" int SRNOS_TL42RReadI32(SRNOS_TL42RDevice *dev,
                                 SRNOS_TL42RParamId param,
                                 int32_t *out_value,
                                 uint32_t timeout_ms)
{
    if (out_value == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_I32) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    uint32_t raw_value = 0u;
    const int ret = SRNOS_TL42RReadParamRaw(dev, &desc, &raw_value, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    *out_value = (int32_t)raw_value;
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RWriteU16(SRNOS_TL42RDevice *dev,
                                  SRNOS_TL42RParamId param,
                                  uint16_t value,
                                  uint32_t timeout_ms)
{
    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_U16) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    return SRNOS_TL42RWriteParamRaw(dev, &desc, value, true, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteI16(SRNOS_TL42RDevice *dev,
                                  SRNOS_TL42RParamId param,
                                  int16_t value,
                                  uint32_t timeout_ms)
{
    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_I16) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    return SRNOS_TL42RWriteParamRaw(dev, &desc, (uint16_t)value, true, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteU32(SRNOS_TL42RDevice *dev,
                                  SRNOS_TL42RParamId param,
                                  uint32_t value,
                                  uint32_t timeout_ms)
{
    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_U32) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    return SRNOS_TL42RWriteParamRaw(dev, &desc, value, true, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteI32(SRNOS_TL42RDevice *dev,
                                  SRNOS_TL42RParamId param,
                                  int32_t value,
                                  uint32_t timeout_ms)
{
    SRNOS_TL42RParamDescriptor desc = {};
    if (!SRNOS_TL42RGetParamDescriptorInternal(param, &desc)) {
        return SRNOS_TL42R_ERR_NOT_FOUND;
    }
    if (desc.type != SRNOS_TL42R_PARAM_TYPE_I32) {
        return SRNOS_TL42R_ERR_TYPE;
    }

    return SRNOS_TL42RWriteParamRaw(dev, &desc, (uint32_t)value, true, timeout_ms);
}

extern "C" int SRNOS_TL42RSaveParameters(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteSingleRegister(dev, SRNOS_TL42R_REG_AUX_CONTROL, (uint16_t)SRNOS_TL42R_AUX_SAVE_CURRENT, timeout_ms);
}

extern "C" int SRNOS_TL42RExecuteAuxCommand(SRNOS_TL42RDevice *dev,
                                            SRNOS_TL42RAuxCommand command,
                                            uint32_t timeout_ms)
{
    const int ret = SRNOS_TL42RWriteSingleRegister(dev, SRNOS_TL42R_REG_AUX_CONTROL, (uint16_t)command, timeout_ms);
    if (ret == SRNOS_TL42R_OK) {
        SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_BASIC |
                                         SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION |
                                         SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED);
    }
    return ret;
}

extern "C" int SRNOS_TL42RReadVersions(SRNOS_TL42RDevice *dev,
                                      uint16_t *out_software_version,
                                      uint16_t *out_hardware_version,
                                      uint32_t timeout_ms)
{
    if ((out_software_version == nullptr) || (out_hardware_version == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    int ret = SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_001, out_software_version, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_002, out_hardware_version, timeout_ms);
}

extern "C" int SRNOS_TL42RReadRunStatus(SRNOS_TL42RDevice *dev,
                                        uint16_t *out_status,
                                        uint32_t timeout_ms)
{
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_004, out_status, timeout_ms);
}

extern "C" int SRNOS_TL42RReadAlarm(SRNOS_TL42RDevice *dev,
                                   uint16_t *out_alarm,
                                   uint32_t timeout_ms)
{
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_005, out_alarm, timeout_ms);
}

extern "C" int SRNOS_TL42RReadStatusAndAlarm(SRNOS_TL42RDevice *dev,
                                              uint16_t *out_status,
                                              uint16_t *out_alarm,
                                              uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_status == nullptr) || (out_alarm == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[2] = {};
    const int ret = SRNOS_TL42RReadRegisters(dev, 4u, 2u, regs, 2u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    *out_status = regs[0];
    *out_alarm = regs[1];
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RReadDigitalInputs(SRNOS_TL42RDevice *dev,
                                            uint16_t *out_di_state,
                                            uint32_t timeout_ms)
{
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_006, out_di_state, timeout_ms);
}

extern "C" int SRNOS_TL42RReadDigitalOutputs(SRNOS_TL42RDevice *dev,
                                             uint16_t *out_do_state,
                                             uint32_t timeout_ms)
{
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_007, out_do_state, timeout_ms);
}

extern "C" int SRNOS_TL42RReadCurrentPosition(SRNOS_TL42RDevice *dev,
                                              int32_t *out_position_pulse,
                                              uint32_t timeout_ms)
{
    return SRNOS_TL42RReadI32(dev, SRNOS_TL42R_PARAM_PA_008, out_position_pulse, timeout_ms);
}

extern "C" int SRNOS_TL42RReadCurrentSpeed(SRNOS_TL42RDevice *dev,
                                           int16_t *out_speed_rpm,
                                           uint32_t timeout_ms)
{
    return SRNOS_TL42RReadI16(dev, SRNOS_TL42R_PARAM_PA_00A, out_speed_rpm, timeout_ms);
}

extern "C" int SRNOS_TL42RReadSubdivision(SRNOS_TL42RDevice *dev,
                                         uint16_t *out_subdivision,
                                         uint32_t timeout_ms)
{
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_023, out_subdivision, timeout_ms);
}

extern "C" int SRNOS_TL42RReadMonitor(SRNOS_TL42RDevice *dev,
                                     SRNOS_TL42RMonitor *out_monitor,
                                     uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_monitor == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[SRNOS_TL42R_MONITOR_REG_COUNT] = {};
    const int ret = SRNOS_TL42RReadRegisters(dev,
                                            SRNOS_TL42R_MONITOR_REG_START,
                                            SRNOS_TL42R_MONITOR_REG_COUNT,
                                            regs,
                                            SRNOS_TL42R_MONITOR_REG_COUNT,
                                            timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RFillMonitor(out_monitor, regs);
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RReadDriveInfo(SRNOS_TL42RDevice *dev,
                                        uint16_t *out_run_mode,
                                        uint16_t *out_encoder_resolution,
                                        uint32_t timeout_ms)
{
    if ((out_run_mode == nullptr) || (out_encoder_resolution == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    int ret = SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_100, out_run_mode, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    return SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_101, out_encoder_resolution, timeout_ms);
}

extern "C" int SRNOS_TL42RReadFactoryParams(SRNOS_TL42RDevice *dev,
                                            SRNOS_TL42RFactoryParams *out_factory_params,
                                            uint32_t timeout_ms)
{
    return SRNOS_TL42RReadFactoryParamsInternal(dev, out_factory_params, timeout_ms);
}

extern "C" int SRNOS_TL42RReadBasicConfig(SRNOS_TL42RDevice *dev,
                                          SRNOS_TL42RBasicConfig *out_basic_config,
                                          uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_basic_config == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
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

    int ret = SRNOS_TL42RReadRegisters(dev, 16u, 6u, regs_16_21, 6u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 26u, 4u, regs_26_29, 4u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_01F, &reg_31, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 32u, 7u, regs_32_38, 7u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadU16(dev, SRNOS_TL42R_PARAM_PA_028, &reg_40, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 48u, 9u, regs_48_56, 9u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 64u, 11u, regs_64_74, 11u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadRegisters(dev, 78u, 2u, regs_78_79, 2u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    ret = SRNOS_TL42RReadFactoryParamsInternal(dev, &out_basic_config->factory_params, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    out_basic_config->di_config.normally_closed_mask = regs_16_21[0];
    out_basic_config->di_config.input_function[0] = (SRNOS_TL42RDiFunction)regs_16_21[1];
    out_basic_config->di_config.input_function[1] = (SRNOS_TL42RDiFunction)regs_16_21[2];
    out_basic_config->di_config.input_function[2] = (SRNOS_TL42RDiFunction)regs_16_21[3];
    out_basic_config->di_config.input_function[3] = (SRNOS_TL42RDiFunction)regs_16_21[4];
    out_basic_config->di_config.input_function[4] = (SRNOS_TL42RDiFunction)regs_16_21[5];
    out_basic_config->di_config.filter_coefficient = regs_26_29[0];

    out_basic_config->do_config.normally_closed_mask = regs_26_29[1];
    out_basic_config->do_config.output_function[0] = (SRNOS_TL42RDoFunction)regs_26_29[2];
    out_basic_config->do_config.output_function[1] = (SRNOS_TL42RDoFunction)regs_26_29[3];
    out_basic_config->do_config.forced_output_mask = reg_31;

    out_basic_config->comm_config.station_id = regs_32_38[0];
    out_basic_config->comm_config.serial_format = (SRNOS_TL42RSerialFormat)regs_32_38[1];
    out_basic_config->comm_config.default_direction = regs_32_38[2];
    out_basic_config->comm_config.subdivision = regs_32_38[3];
    out_basic_config->comm_config.limit_stop_mode = regs_32_38[4];
    out_basic_config->comm_config.soft_limit_enable = regs_32_38[5];
    out_basic_config->comm_config.multi_position_trigger_mode = regs_32_38[6];
    out_basic_config->comm_config.baudrate = (SRNOS_TL42RBaudrate)reg_40;

    out_basic_config->motion_config.jog_speed_rpm = (int16_t)regs_48_56[0];
    out_basic_config->motion_config.jog_accel_ms = regs_48_56[1];
    out_basic_config->motion_config.jog_decel_ms = regs_48_56[2];
    out_basic_config->motion_config.position_start_speed_rpm = regs_48_56[3];
    out_basic_config->motion_config.position_accel_ms = regs_48_56[4];
    out_basic_config->motion_config.position_decel_ms = regs_48_56[5];
    out_basic_config->motion_config.position_speed_rpm = regs_48_56[6];
    out_basic_config->motion_config.position_target_pulse = SRNOS_TL42RComposePosition(regs_48_56[7], regs_48_56[8]);
    out_basic_config->motion_config.multi_position_mode = (SRNOS_TL42RMultiPositionMode)regs_64_74[10];
    out_basic_config->motion_config.control_word = regs_78_79[0];
    out_basic_config->motion_config.aux_control = regs_78_79[1];

    out_basic_config->homing_config.homing_mode = (SRNOS_TL42RHomingMode)regs_64_74[0];
    out_basic_config->homing_config.homing_approach_speed_rpm = regs_64_74[1];
    out_basic_config->homing_config.homing_crawl_speed_rpm = regs_64_74[2];
    out_basic_config->homing_config.homing_accel_ms = regs_64_74[3];
    out_basic_config->homing_config.homing_offset_pulse = SRNOS_TL42RComposePosition(regs_64_74[4], regs_64_74[5]);
    out_basic_config->homing_config.positive_soft_limit_pulse = SRNOS_TL42RComposePosition(regs_64_74[6], regs_64_74[7]);
    out_basic_config->homing_config.negative_soft_limit_pulse = SRNOS_TL42RComposePosition(regs_64_74[8], regs_64_74[9]);

    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RSetDiConfig(SRNOS_TL42RDevice *dev,
                                      const SRNOS_TL42RDiConfig *config,
                                      uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteDiConfigInternal(dev, config, true, timeout_ms);
}

extern "C" int SRNOS_TL42RSetDoConfig(SRNOS_TL42RDevice *dev,
                                      const SRNOS_TL42RDoConfig *config,
                                      uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteDoConfigInternal(dev, config, true, timeout_ms);
}

extern "C" int SRNOS_TL42RSetCommunicationConfig(SRNOS_TL42RDevice *dev,
                                                 const SRNOS_TL42RCommConfig *config,
                                                 uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteCommConfigInternal(dev, config, true, timeout_ms);
}

extern "C" int SRNOS_TL42RSetMotionConfig(SRNOS_TL42RDevice *dev,
                                          const SRNOS_TL42RMotionConfig *config,
                                          uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteMotionConfigInternal(dev, config, true, timeout_ms);
}

extern "C" int SRNOS_TL42RSetHomingConfig(SRNOS_TL42RDevice *dev,
                                          const SRNOS_TL42RHomingConfig *config,
                                          uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteHomingConfigInternal(dev, config, true, timeout_ms);
}

extern "C" int SRNOS_TL42RReadMultiPositionSegment(SRNOS_TL42RDevice *dev,
                                                    uint8_t segment_index,
                                                    SRNOS_TL42RPositionSegment *out_segment,
                                                    uint32_t timeout_ms)
{
    return SRNOS_TL42RReadMultiPositionSegmentInternal(dev, segment_index, out_segment, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteMultiPositionSegment(SRNOS_TL42RDevice *dev,
                                                     uint8_t segment_index,
                                                     const SRNOS_TL42RPositionSegment *segment,
                                                     uint32_t timeout_ms)
{
    return SRNOS_TL42RWritePositionSegmentInternal(dev, segment_index, segment, true, timeout_ms);
}

extern "C" int SRNOS_TL42RReadMultiPositionTable(SRNOS_TL42RDevice *dev,
                                                  SRNOS_TL42RMultiPositionTable *out_table,
                                                  uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_table == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    (void)memset(out_table, 0, sizeof(*out_table));
    for (uint8_t i = 0u; i < SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT; ++i) {
        const int ret = SRNOS_TL42RReadMultiPositionSegmentInternal(dev,
                                                                      i,
                                                                      &out_table->segments[i],
                                                                      timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
    }
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RWriteMultiPositionTable(SRNOS_TL42RDevice *dev,
                                                   const SRNOS_TL42RMultiPositionTable *table,
                                                   uint32_t timeout_ms)
{
    if ((dev == nullptr) || (table == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    for (uint8_t i = 0u; i < SRNOS_TL42R_MULTI_POSITION_SEGMENT_COUNT; ++i) {
        const int ret = SRNOS_TL42RWritePositionSegmentInternal(dev,
                                                                 i,
                                                                 &table->segments[i],
                                                                 false,
                                                                 timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
    }

    return SRNOS_TL42RSaveParameters(dev, timeout_ms);
}

extern "C" int SRNOS_TL42RReadMultiSpeedSegment(SRNOS_TL42RDevice *dev,
                                                 uint8_t segment_index,
                                                 SRNOS_TL42RSpeedSegment *out_segment,
                                                 uint32_t timeout_ms)
{
    return SRNOS_TL42RReadMultiSpeedSegmentInternal(dev, segment_index, out_segment, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteMultiSpeedSegment(SRNOS_TL42RDevice *dev,
                                                  uint8_t segment_index,
                                                  const SRNOS_TL42RSpeedSegment *segment,
                                                  uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteSpeedSegmentInternal(dev, segment_index, segment, true, timeout_ms);
}

extern "C" int SRNOS_TL42RReadMultiSpeedTable(SRNOS_TL42RDevice *dev,
                                               SRNOS_TL42RMultiSpeedTable *out_table,
                                               uint32_t timeout_ms)
{
    if ((dev == nullptr) || (out_table == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[48] = {};
    const int ret = SRNOS_TL42RReadRegisters(dev, 176u, 48u, regs, 48u, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    for (uint8_t i = 0u; i < SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT; ++i) {
        out_table->segments[i].speed_rpm = (int16_t)regs[i * 3u];
        out_table->segments[i].accel_ms = regs[i * 3u + 1u];
        out_table->segments[i].decel_ms = regs[i * 3u + 2u];
    }
    return SRNOS_TL42R_OK;
}

extern "C" int SRNOS_TL42RWriteMultiSpeedTable(SRNOS_TL42RDevice *dev,
                                                const SRNOS_TL42RMultiSpeedTable *table,
                                                uint32_t timeout_ms)
{
    if ((dev == nullptr) || (table == nullptr)) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    uint16_t regs[48] = {};
    for (uint8_t i = 0u; i < SRNOS_TL42R_MULTI_SPEED_SEGMENT_COUNT; ++i) {
        regs[i * 3u] = (uint16_t)table->segments[i].speed_rpm;
        regs[i * 3u + 1u] = table->segments[i].accel_ms;
        regs[i * 3u + 2u] = table->segments[i].decel_ms;
    }

    const int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 176u, 48u, regs, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    SRNOS_TL42RInvalidateConfigCache(SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED);
    return SRNOS_TL42RSaveParameters(dev, timeout_ms);
}

extern "C" int SRNOS_TL42RSetPositionSpeed(SRNOS_TL42RDevice *dev,
                                           uint16_t speed_rpm,
                                           uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteSingleRegister(dev, SRNOS_TL42R_REG_POSITION_SPEED, speed_rpm, timeout_ms);
}

extern "C" int SRNOS_TL42RSetPositionTarget(SRNOS_TL42RDevice *dev,
                                            int32_t target_pulse,
                                            uint32_t timeout_ms)
{
    const uint32_t raw_target = (uint32_t)target_pulse;
    const uint16_t regs[2] = {
        (uint16_t)(raw_target >> 16),
        (uint16_t)(raw_target & 0xFFFFu)
    };
    return SRNOS_TL42RWriteMultipleRegisters(dev, SRNOS_TL42R_REG_POSITION_TARGET_H, 2u, regs, timeout_ms);
}

extern "C" int SRNOS_TL42RWriteControlWord(SRNOS_TL42RDevice *dev,
                                           uint16_t control_word,
                                           uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteSingleRegister(dev, SRNOS_TL42R_REG_CONTROL_WORD, control_word, timeout_ms);
}

extern "C" int SRNOS_TL42RStartRelativeMove(SRNOS_TL42RDevice *dev,
                                            int32_t target_pulse,
                                            uint16_t speed_rpm,
                                            bool interrupt_current,
                                            uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    int ret = SRNOS_TL42RSetPositionTarget(dev, target_pulse, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    ret = SRNOS_TL42RSetPositionSpeed(dev, speed_rpm, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    uint16_t control_word = SRNOS_TL42R_CTRL_POSITION_TRIGGER;
    if (interrupt_current) {
        control_word |= SRNOS_TL42R_CTRL_INTERRUPT_MODE;
    }
    return SRNOS_TL42RWriteControlWord(dev, control_word, timeout_ms);
}

extern "C" int SRNOS_TL42RStartAbsoluteMove(SRNOS_TL42RDevice *dev,
                                            int32_t target_pulse,
                                            uint16_t speed_rpm,
                                            bool interrupt_current,
                                            uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    int ret = SRNOS_TL42RSetPositionTarget(dev, target_pulse, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    ret = SRNOS_TL42RSetPositionSpeed(dev, speed_rpm, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }

    uint16_t control_word = (uint16_t)(SRNOS_TL42R_CTRL_POSITION_TRIGGER | SRNOS_TL42R_CTRL_ABSOLUTE_MODE);
    if (interrupt_current) {
        control_word |= SRNOS_TL42R_CTRL_INTERRUPT_MODE;
    }
    return SRNOS_TL42RWriteControlWord(dev, control_word, timeout_ms);
}

extern "C" int SRNOS_TL42RStartJog(SRNOS_TL42RDevice *dev,
                                  int16_t speed_rpm,
                                  uint16_t accel_ms,
                                  uint16_t decel_ms,
                                  uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    const uint16_t regs[3] = {
        (uint16_t)speed_rpm,
        accel_ms,
        decel_ms
    };

    int ret = SRNOS_TL42RWriteMultipleRegisters(dev, 48u, 3u, regs, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    return SRNOS_TL42RWriteControlWord(dev, SRNOS_TL42R_CTRL_JOG_TRIGGER, timeout_ms);
}

extern "C" int SRNOS_TL42RStartHoming(SRNOS_TL42RDevice *dev,
                                     SRNOS_TL42RHomingMode homing_mode,
                                     uint16_t approach_speed_rpm,
                                     uint16_t crawl_speed_rpm,
                                     uint16_t accel_ms,
                                     int32_t offset_pulse,
                                     uint32_t timeout_ms)
{
    SRNOS_TL42RHomingConfig config = {};
    config.homing_mode = homing_mode;
    config.homing_approach_speed_rpm = approach_speed_rpm;
    config.homing_crawl_speed_rpm = crawl_speed_rpm;
    config.homing_accel_ms = accel_ms;
    config.homing_offset_pulse = offset_pulse;
    config.positive_soft_limit_pulse = 0;
    config.negative_soft_limit_pulse = 0;

    int ret = SRNOS_TL42RWriteHomingConfigInternal(dev, &config, false, timeout_ms);
    if (ret != SRNOS_TL42R_OK) {
        return ret;
    }
    return SRNOS_TL42RWriteControlWord(dev, SRNOS_TL42R_CTRL_HOMING_TRIGGER, timeout_ms);
}

extern "C" int SRNOS_TL42RStop(SRNOS_TL42RDevice *dev,
                             bool emergency_stop,
                             uint32_t timeout_ms)
{
    return SRNOS_TL42RWriteControlWord(dev,
                                       emergency_stop ? SRNOS_TL42R_CTRL_ESTOP : SRNOS_TL42R_CTRL_STOP,
                                       timeout_ms);
}

extern "C" int SRNOS_TL42REstop(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RStop(dev, true, timeout_ms);
}

extern "C" int SRNOS_TL42RRestoreFactoryDefaults(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RExecuteAuxCommand(dev, SRNOS_TL42R_AUX_RESTORE_FACTORY, timeout_ms);
}

extern "C" int SRNOS_TL42RClearAlarm(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RExecuteAuxCommand(dev, SRNOS_TL42R_AUX_CLEAR_ALARM, timeout_ms);
}

extern "C" int SRNOS_TL42RClearPosition(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RExecuteAuxCommand(dev, SRNOS_TL42R_AUX_CLEAR_POSITION, timeout_ms);
}

extern "C" int SRNOS_TL42RMotorEnable(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RExecuteAuxCommand(dev, SRNOS_TL42R_AUX_MOTOR_ENABLE, timeout_ms);
}

extern "C" int SRNOS_TL42RMotorRelease(SRNOS_TL42RDevice *dev, uint32_t timeout_ms)
{
    return SRNOS_TL42RExecuteAuxCommand(dev, SRNOS_TL42R_AUX_MOTOR_RELEASE, timeout_ms);
}

extern "C" void SRNOS_TL42RWatchReset(uint8_t port, uint8_t unit_id, int32_t pulses_per_rev)
{
    SRNOS_TL42RWatch watch = {};
    watch.online = false;
    watch.port = port;
    watch.unit_id = unit_id;
    watch.last_error = SRNOS_TL42R_OK;
    watch.pulses_per_rev = pulses_per_rev;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchSetVersions(uint16_t software_version, uint16_t hardware_version)
{
    SRNOS_TL42RWatch watch = {};
    (void)SRNOS_TL42RLoadWatch(&watch);
    watch.software_version = software_version;
    watch.hardware_version = hardware_version;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchSetDriveInfo(uint16_t run_mode, uint16_t encoder_resolution)
{
    SRNOS_TL42RWatch watch = {};
    (void)SRNOS_TL42RLoadWatch(&watch);
    watch.run_mode = run_mode;
    watch.encoder_resolution = encoder_resolution;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchSetPulsesPerRev(int32_t pulses_per_rev)
{
    SRNOS_TL42RWatch watch = {};
    (void)SRNOS_TL42RLoadWatch(&watch);
    watch.pulses_per_rev = pulses_per_rev;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchSetZeroCurrent(void)
{
    SRNOS_TL42RWatch watch = {};
    if (!SRNOS_TL42RLoadWatch(&watch) || !watch.initialized) {
        return;
    }

    watch.zero_offset_pulse = watch.tracked_position_pulse;
    watch.relative_position_pulse = 0;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchMarkError(int error_code, uint32_t now_ms)
{
    SRNOS_TL42RWatch watch = {};
    (void)SRNOS_TL42RLoadWatch(&watch);
    watch.online = false;
    watch.last_update_ms = now_ms;
    watch.last_error = error_code;
    watch.error_count++;
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" void SRNOS_TL42RWatchUpdate(const SRNOS_TL42RMonitor *monitor, uint32_t now_ms)
{
    if (monitor == nullptr) {
        return;
    }

    SRNOS_TL42RWatch watch = {};
    (void)SRNOS_TL42RLoadWatch(&watch);

    const uint32_t raw_now = ((uint32_t)monitor->raw_position_high << 16) |
                             (uint32_t)monitor->raw_position_low;

    watch.online = true;
    watch.last_update_ms = now_ms;
    watch.ok_count++;
    watch.last_error = SRNOS_TL42R_OK;
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
    SRNOS_TL42RStoreWatch(&watch);
}

extern "C" bool SRNOS_TL42RGetWatch(SRNOS_TL42RWatch *out_watch)
{
    if (out_watch == nullptr) {
        return false;
    }
    (void)SRNOS_TL42RLoadWatch(out_watch);
    return out_watch->initialized;
}

extern "C" void SRNOS_TL42RConfigCacheReset(void)
{
    SRNOS_TL42RConfigCache cache = {};
    SRNOS_TL42RStoreConfigCache(&cache);
}

extern "C" int SRNOS_TL42RRefreshConfigCache(SRNOS_TL42RDevice *dev,
                                             uint32_t refresh_mask,
                                             uint32_t timeout_ms)
{
    if (dev == nullptr) {
        return SRNOS_TL42R_ERR_PARAM;
    }
    if ((refresh_mask & SRNOS_TL42R_CONFIG_CACHE_ALL) == 0u) {
        return SRNOS_TL42R_ERR_PARAM;
    }

    SRNOS_TL42RConfigCache cache = {};
    (void)SRNOS_TL42RLoadConfigCache(&cache);

    int ret = SRNOS_TL42R_OK;
    if ((refresh_mask & SRNOS_TL42R_CONFIG_CACHE_BASIC) != 0u) {
        ret = SRNOS_TL42RReadBasicConfig(dev, &cache.basic, timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
        cache.basic_valid = true;
    }
    if ((refresh_mask & SRNOS_TL42R_CONFIG_CACHE_MULTI_POSITION) != 0u) {
        ret = SRNOS_TL42RReadMultiPositionTable(dev, &cache.position_table, timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
        cache.position_table_valid = true;
    }
    if ((refresh_mask & SRNOS_TL42R_CONFIG_CACHE_MULTI_SPEED) != 0u) {
        ret = SRNOS_TL42RReadMultiSpeedTable(dev, &cache.speed_table, timeout_ms);
        if (ret != SRNOS_TL42R_OK) {
            return ret;
        }
        cache.speed_table_valid = true;
    }

    cache.initialized = true;
    cache.last_refresh_ms = tl42r_now_ms();
    SRNOS_TL42RStoreConfigCache(&cache);
    return SRNOS_TL42R_OK;
}

extern "C" bool SRNOS_TL42RGetConfigCache(SRNOS_TL42RConfigCache *out_cache)
{
    if (out_cache == nullptr) {
        return false;
    }
    (void)SRNOS_TL42RLoadConfigCache(out_cache);
    return out_cache->initialized;
}

extern "C" void SRNOS_TL42RDecodeRunStatus(uint16_t raw_status, SRNOS_TL42RRunStatusBits *out_status)
{
    if (out_status == nullptr) {
        return;
    }

    out_status->in_position = (raw_status & SRNOS_TL42R_STATUS_IN_POSITION) != 0u;
    out_status->homing_done = (raw_status & SRNOS_TL42R_STATUS_HOMING_DONE) != 0u;
    out_status->motor_running = (raw_status & SRNOS_TL42R_STATUS_MOTOR_RUNNING) != 0u;
    out_status->fault = (raw_status & SRNOS_TL42R_STATUS_FAULT) != 0u;
    out_status->enabled = (raw_status & SRNOS_TL42R_STATUS_ENABLED) != 0u;
    out_status->positive_soft_limit = (raw_status & SRNOS_TL42R_STATUS_POSITIVE_SOFT_LIM) != 0u;
    out_status->negative_soft_limit = (raw_status & SRNOS_TL42R_STATUS_NEGATIVE_SOFT_LIM) != 0u;
}

extern "C" void SRNOS_TL42RDecodeDiState(uint16_t raw_di_state, SRNOS_TL42RDiStateBits *out_state)
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

extern "C" void SRNOS_TL42RDecodeDoState(uint16_t raw_do_state, SRNOS_TL42RDoStateBits *out_state)
{
    if (out_state == nullptr) {
        return;
    }

    out_state->do0 = (raw_do_state & (1u << 0)) != 0u;
    out_state->do1 = (raw_do_state & (1u << 1)) != 0u;
    out_state->do2 = (raw_do_state & (1u << 2)) != 0u;
}

extern "C" int32_t SRNOS_TL42RComposePosition(uint16_t position_high, uint16_t position_low)
{
    return (int32_t)(((uint32_t)position_high << 16) | (uint32_t)position_low);
}

extern "C" int16_t SRNOS_TL42RDecodeSpeed(uint16_t raw_speed)
{
    return (int16_t)raw_speed;
}

extern "C" const char *SRNOS_TL42RAlarmToString(uint16_t alarm_code)
{
    switch (alarm_code) {
        case SRNOS_TL42R_ALARM_NONE:
            return "none";
        case SRNOS_TL42R_ALARM_OVERCURRENT:
            return "overcurrent";
        case SRNOS_TL42R_ALARM_OVERVOLTAGE:
            return "overvoltage";
        case SRNOS_TL42R_ALARM_UNDERVOLTAGE:
            return "undervoltage";
        default:
            return "unknown";
    }
}

extern "C" const char *SRNOS_TL42RRunModeToString(uint16_t run_mode)
{
    switch (run_mode) {
        case SRNOS_TL42R_RUN_MODE_OPEN_LOOP:
            return "open_loop";
        case SRNOS_TL42R_RUN_MODE_CLOSED_LOOP:
            return "closed_loop";
        default:
            return "unknown";
    }
}

extern "C" const char *SRNOS_TL42RErrorToString(int error_code)
{
    switch (error_code) {
        case SRNOS_TL42R_OK:
            return "ok";
        case SRNOS_TL42R_ERR_PARAM:
            return "tl42r_param";
        case SRNOS_TL42R_ERR_STATE:
            return "tl42r_state";
        case SRNOS_TL42R_ERR_LENGTH:
            return "tl42r_length";
        case SRNOS_TL42R_ERR_ACCESS:
            return "tl42r_access";
        case SRNOS_TL42R_ERR_TYPE:
            return "tl42r_type";
        case SRNOS_TL42R_ERR_RANGE:
            return "tl42r_range";
        case SRNOS_TL42R_ERR_NOT_FOUND:
            return "tl42r_not_found";
        case SRNOS_MODBUS_RTU_ERR_PARAM:
            return "SRNOS_ModbusParam";
        case SRNOS_MODBUS_RTU_ERR_STATE:
            return "SRNOS_ModbusState";
        case SRNOS_MODBUS_RTU_ERR_FULL:
            return "SRNOS_ModbusFull";
        case SRNOS_MODBUS_RTU_ERR_TIMEOUT:
            return "SRNOS_ModbusTimeout";
        case SRNOS_MODBUS_RTU_ERR_CRC:
            return "SRNOS_ModbusCRC";
        case SRNOS_MODBUS_RTU_ERR_LENGTH:
            return "SRNOS_ModbusLength";
        case SRNOS_MODBUS_RTU_ERR_EXCEPTION:
            return "SRNOS_ModbusException";
        case SRNOS_MODBUS_RTU_ERR_FUNCTION:
            return "SRNOS_ModbusFunction";
        default:
            return "unknown";
    }
}
