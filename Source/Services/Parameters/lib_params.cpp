#include "SirinEmbedOS/Services/Parameters/lib_params.hpp"

#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
#include "SirinEmbedOS/Devices/EEPROM/lib_24c02_bl.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "stm32h7xx_hal.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

namespace
{

constexpr uint32_t kFnvOffset = 2166136261UL;
constexpr uint32_t kFnvPrime = 16777619UL;
constexpr uint32_t kEepromMagic = 0x50584D4FUL; /* "OMXP" */
constexpr uint16_t kEepromFormatVersion = 1U;
constexpr size_t kHeaderSize = 20U;
constexpr size_t kMagicOffset = 0U;
constexpr size_t kVersionOffset = 4U;
constexpr size_t kHeaderSizeOffset = 6U;
constexpr size_t kEntryCountOffset = 8U;
constexpr size_t kPayloadSizeOffset = 10U;
constexpr size_t kLayoutHashOffset = 12U;
constexpr size_t kCrcOffset = 16U;
constexpr size_t kReservedOffset = 18U;

static_assert(PARAMS_EEPROM_START_ADDR < LIB_24C02_BL_SIZE_BYTES,
              "PARAMS_EEPROM_START_ADDR must be inside the 24C02 address space");

constexpr size_t kEepromCapacity =
    static_cast<size_t>(LIB_24C02_BL_SIZE_BYTES - PARAMS_EEPROM_START_ADDR);

static_assert(kHeaderSize <= kEepromCapacity,
              "PARAMS EEPROM area must fit the header");

lib_24c02_bl_t s_eeprom {};
bool s_eeprom_ready = false;
bool s_debug_ready = false;

} // namespace

OMX_DEBUG_WATCH_BSS volatile omx_dbg_params_t g_omx_dbg_params;

namespace
{

uint32_t lower_ascii(uint32_t ch)
{
    if ((ch >= static_cast<uint32_t>('A')) && (ch <= static_cast<uint32_t>('Z')))
    {
        return ch + static_cast<uint32_t>('a' - 'A');
    }
    return ch;
}

const char *skip_spaces(const char *s)
{
    if (s == nullptr)
    {
        return nullptr;
    }
    while ((*s == ' ') || (*s == '\t') || (*s == '\r') || (*s == '\n') || (*s == '\f') || (*s == '\v'))
    {
        ++s;
    }
    return s;
}

bool is_tail_clean(const char *s)
{
    s = skip_spaces(s);
    return (s != nullptr) && (*s == '\0');
}

bool text_equals_ci(const char *text, const char *literal)
{
    if ((text == nullptr) || (literal == nullptr))
    {
        return false;
    }

    const char *p = skip_spaces(text);
    const char *q = literal;
    while ((*p != '\0') && (*q != '\0'))
    {
        if (lower_ascii(static_cast<uint32_t>(*p)) != lower_ascii(static_cast<uint32_t>(*q)))
        {
            return false;
        }
        ++p;
        ++q;
    }
    return (*q == '\0') && is_tail_clean(p);
}

params_status_t format_checked(int ret, char *out, size_t out_len)
{
    if ((out == nullptr) || (out_len == 0U))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    if (ret < 0)
    {
        out[0] = '\0';
        return PARAMS_STATUS_ERROR;
    }
    if (static_cast<size_t>(ret) >= out_len)
    {
        out[out_len - 1U] = '\0';
        return PARAMS_STATUS_BUFFER_TOO_SMALL;
    }
    return PARAMS_STATUS_OK;
}

uint16_t crc16_ccitt_update(uint16_t crc, uint8_t byte)
{
    crc ^= static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8U);
    for (uint32_t i = 0U; i < 8U; ++i)
    {
        if ((crc & 0x8000U) != 0U)
        {
            crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
        }
        else
        {
            crc = static_cast<uint16_t>(crc << 1U);
        }
    }
    return crc;
}

uint16_t image_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < len; ++i)
    {
        uint8_t byte = data[i];
        if ((i == kCrcOffset) || (i == (kCrcOffset + 1U)))
        {
            byte = 0U;
        }
        crc = crc16_ccitt_update(crc, byte);
    }
    return crc;
}

void put_u16(uint8_t *buf, size_t off, uint16_t value)
{
    buf[off + 0U] = static_cast<uint8_t>(value & 0xFFU);
    buf[off + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(uint8_t *buf, size_t off, uint32_t value)
{
    buf[off + 0U] = static_cast<uint8_t>(value & 0xFFU);
    buf[off + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    buf[off + 2U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    buf[off + 3U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint16_t get_u16(const uint8_t *buf, size_t off)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(buf[off + 0U]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(buf[off + 1U]) << 8U));
}

uint32_t get_u32(const uint8_t *buf, size_t off)
{
    return (static_cast<uint32_t>(buf[off + 0U]) |
            (static_cast<uint32_t>(buf[off + 1U]) << 8U) |
            (static_cast<uint32_t>(buf[off + 2U]) << 16U) |
            (static_cast<uint32_t>(buf[off + 3U]) << 24U));
}

params_status_t map_eeprom_status(lib_24c02_bl_status_t st)
{
    switch (st)
    {
    case LIB_24C02_BL_OK:
        return PARAMS_STATUS_OK;
    case LIB_24C02_BL_BUSY:
        return PARAMS_STATUS_EEPROM_BUSY;
    case LIB_24C02_BL_INVALID_PARAM:
        return PARAMS_STATUS_INVALID_PARAM;
    case LIB_24C02_BL_ERROR:
    default:
        return PARAMS_STATUS_EEPROM_ERROR;
    }
}

params_status_t ensure_eeprom_ready()
{
    if (s_eeprom_ready)
    {
        return PARAMS_STATUS_OK;
    }

    lib_24c02_bl_status_t st =
        lib_24c02_bl_init(&s_eeprom,
                          static_cast<bsp_i2c_bus_id_t>(PARAMS_EEPROM_I2C_BUS_ID),
                          static_cast<uint16_t>(PARAMS_EEPROM_I2C_ADDR_7BIT));
    params_status_t mapped = map_eeprom_status(st);
    s_eeprom_ready = (mapped == PARAMS_STATUS_OK);
    return mapped;
}

void debug_begin_if_ready()
{
    if (s_debug_ready)
    {
        omx_dbg_begin(&g_omx_dbg_params.h);
    }
}

void debug_end_if_ready(params_status_t status)
{
    if (s_debug_ready)
    {
        omx_dbg_end(&g_omx_dbg_params.h, HAL_GetTick(), static_cast<uint32_t>(status));
    }
}

} // namespace

namespace omx::params
{

uint32_t HashBytes(uint32_t hash, const void *data, size_t len)
{
    if ((data == nullptr) && (len != 0U))
    {
        return hash;
    }

    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0U; i < len; ++i)
    {
        hash ^= static_cast<uint32_t>(p[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

uint32_t HashU32(uint32_t hash, uint32_t value)
{
    uint8_t tmp[4] {
        static_cast<uint8_t>(value & 0xFFU),
        static_cast<uint8_t>((value >> 8U) & 0xFFU),
        static_cast<uint8_t>((value >> 16U) & 0xFFU),
        static_cast<uint8_t>((value >> 24U) & 0xFFU),
    };
    return HashBytes(hash, tmp, sizeof(tmp));
}

uint32_t HashName(const char *name)
{
    if (name == nullptr)
    {
        return 0U;
    }
    return HashBytes(kFnvOffset, name, std::strlen(name));
}

params_status_t ParamTraits<float>::Parse(const char *text, float *out)
{
    if ((text == nullptr) || (out == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    const char *start = skip_spaces(text);
    if ((start == nullptr) || (*start == '\0'))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    errno = 0;
    char *end = nullptr;
    const float value = std::strtof(start, &end);
    if ((end == start) || (errno == ERANGE) || !is_tail_clean(end) || !(value == value))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    *out = value;
    return PARAMS_STATUS_OK;
}

params_status_t ParamTraits<float>::Format(float value, char *out, size_t out_len)
{
    if ((out == nullptr) || (out_len == 0U))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    int ret = std::snprintf(out, out_len, "%.6g", static_cast<double>(value));
    return format_checked(ret, out, out_len);
}

params_status_t ParamTraits<int32_t>::Parse(const char *text, int32_t *out)
{
    if ((text == nullptr) || (out == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    const char *start = skip_spaces(text);
    if ((start == nullptr) || (*start == '\0'))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    errno = 0;
    char *end = nullptr;
    const long value = std::strtol(start, &end, 0);
    if ((end == start) || (errno == ERANGE) || !is_tail_clean(end) ||
        (value < static_cast<long>(INT32_MIN)) ||
        (value > static_cast<long>(INT32_MAX)))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    *out = static_cast<int32_t>(value);
    return PARAMS_STATUS_OK;
}

params_status_t ParamTraits<int32_t>::Format(int32_t value, char *out, size_t out_len)
{
    if ((out == nullptr) || (out_len == 0U))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    int ret = std::snprintf(out, out_len, "%ld", static_cast<long>(value));
    return format_checked(ret, out, out_len);
}

params_status_t ParamTraits<uint32_t>::Parse(const char *text, uint32_t *out)
{
    if ((text == nullptr) || (out == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    const char *start = skip_spaces(text);
    if ((start == nullptr) || (*start == '\0') || (*start == '-'))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long value = std::strtoul(start, &end, 0);
    if ((end == start) || (errno == ERANGE) || !is_tail_clean(end) ||
        (value > static_cast<unsigned long>(UINT32_MAX)))
    {
        return PARAMS_STATUS_PARSE_ERROR;
    }

    *out = static_cast<uint32_t>(value);
    return PARAMS_STATUS_OK;
}

params_status_t ParamTraits<uint32_t>::Format(uint32_t value, char *out, size_t out_len)
{
    if ((out == nullptr) || (out_len == 0U))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    int ret = std::snprintf(out, out_len, "%lu", static_cast<unsigned long>(value));
    return format_checked(ret, out, out_len);
}

params_status_t ParamTraits<bool>::Parse(const char *text, bool *out)
{
    if ((text == nullptr) || (out == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    if (text_equals_ci(text, "true") || text_equals_ci(text, "1") ||
        text_equals_ci(text, "on") || text_equals_ci(text, "yes"))
    {
        *out = true;
        return PARAMS_STATUS_OK;
    }

    if (text_equals_ci(text, "false") || text_equals_ci(text, "0") ||
        text_equals_ci(text, "off") || text_equals_ci(text, "no"))
    {
        *out = false;
        return PARAMS_STATUS_OK;
    }

    return PARAMS_STATUS_PARSE_ERROR;
}

params_status_t ParamTraits<bool>::Format(bool value, char *out, size_t out_len)
{
    if ((out == nullptr) || (out_len == 0U))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    int ret = std::snprintf(out, out_len, "%s", value ? "true" : "false");
    return format_checked(ret, out, out_len);
}

ParamBase::ParamBase(const char *name, params_type_t type, size_t size, params_access_t access)
    : name_(name),
      type_(type),
      size_(size),
      name_hash_(HashName(name)),
      index_(0U),
      registered_(false),
      registration_status_(PARAMS_STATUS_NOT_INITIALIZED),
      access_(access)
{
}

void ParamBase::FillInfo(params_entry_info_t *out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->index = index_;
    out->name = name_;
    out->name_hash = name_hash_;
    out->type = type_;
    out->size = static_cast<uint16_t>(size_);
    out->registered = registered_ ? 1U : 0U;
    out->access = static_cast<uint8_t>(access_);
}

void ParamBase::MarkRegistered(uint32_t index)
{
    index_ = index;
    registered_ = true;
    registration_status_ = PARAMS_STATUS_OK;
}

void ParamBase::MarkRegistrationFailed(params_status_t status)
{
    registered_ = false;
    registration_status_ = status;
}

ParamRegistry::ParamRegistry()
    : entries_{},
      count_(0U),
      register_errors_(0U),
      duplicate_errors_(0U),
      frozen_(false)
{
}

ParamRegistry &ParamRegistry::Instance()
{
    static ParamRegistry registry;
    return registry;
}

params_status_t ParamRegistry::Register(ParamBase &param)
{
    if (frozen_) {
        param.MarkRegistrationFailed(PARAMS_STATUS_LOCKED);
        return PARAMS_STATUS_LOCKED;
    }
    const char *name = param.Name();
    if ((param.access_ != PARAMS_READ_ONLY && param.access_ != PARAMS_MUTABLE_BUSINESS) ||
        (name == nullptr) || (name[0] == '\0'))
    {
        ++register_errors_;
        param.MarkRegistrationFailed(PARAMS_STATUS_INVALID_PARAM);
        SyncDebug(PARAMS_STATUS_INVALID_PARAM);
        return PARAMS_STATUS_INVALID_PARAM;
    }

    if (std::strlen(name) > PARAMS_MAX_NAME_LEN)
    {
        ++register_errors_;
        param.MarkRegistrationFailed(PARAMS_STATUS_INVALID_PARAM);
        SyncDebug(PARAMS_STATUS_INVALID_PARAM);
        return PARAMS_STATUS_INVALID_PARAM;
    }

    params_status_t def_status = param.ValidateDefinition();
    if (def_status != PARAMS_STATUS_OK)
    {
        ++register_errors_;
        param.MarkRegistrationFailed(def_status);
        SyncDebug(def_status);
        return def_status;
    }

    for (size_t i = 0U; i < count_; ++i)
    {
        if ((entries_[i] != nullptr) && (std::strcmp(entries_[i]->Name(), name) == 0))
        {
            ++register_errors_;
            ++duplicate_errors_;
            param.MarkRegistrationFailed(PARAMS_STATUS_DUPLICATE);
            SyncDebug(PARAMS_STATUS_DUPLICATE);
            return PARAMS_STATUS_DUPLICATE;
        }
    }

    if (count_ >= PARAMS_MAX_COUNT)
    {
        ++register_errors_;
        param.MarkRegistrationFailed(PARAMS_STATUS_NO_SPACE);
        SyncDebug(PARAMS_STATUS_NO_SPACE);
        return PARAMS_STATUS_NO_SPACE;
    }

    const uint32_t index = static_cast<uint32_t>(count_);
    entries_[count_] = &param;
    ++count_;
    param.MarkRegistered(index);
    SyncDebug(PARAMS_STATUS_OK);
    return PARAMS_STATUS_OK;
}

params_status_t ParamRegistry::Freeze()
{
    frozen_ = true;
    return (register_errors_ == 0U) ? PARAMS_STATUS_OK : PARAMS_STATUS_INVALID_PARAM;
}

params_status_t ParamRegistry::Init()
{
    if (!s_debug_ready)
    {
        omx_dbg_zero(&g_omx_dbg_params, sizeof(g_omx_dbg_params));
        omx_dbg_init(&g_omx_dbg_params.h,
                     OMX_DBG_MODULE_PARAMS,
                     0U,
                     static_cast<uint16_t>(sizeof(g_omx_dbg_params)));
        s_debug_ready = true;
    }

    debug_begin_if_ready();
    g_omx_dbg_params.init_count++;
    debug_end_if_ready(PARAMS_STATUS_OK);

    params_status_t st = ensure_eeprom_ready();
    SyncDebug(st);
    return st;
}

ParamBase *ParamRegistry::GetByIndex(size_t index) const
{
    if (index >= count_)
    {
        return nullptr;
    }
    return entries_[index];
}

ParamBase *ParamRegistry::Find(const char *name) const
{
    if (name == nullptr)
    {
        return nullptr;
    }

    for (size_t i = 0U; i < count_; ++i)
    {
        if ((entries_[i] != nullptr) && (std::strcmp(entries_[i]->Name(), name) == 0))
        {
            return entries_[i];
        }
    }
    return nullptr;
}

params_status_t ParamRegistry::SetFromString(const char *name, const char *text)
{
    ParamBase *param = Find(name);
    if (param == nullptr)
    {
        RecordSetStatus(PARAMS_STATUS_NOT_FOUND);
        return PARAMS_STATUS_NOT_FOUND;
    }

    params_status_t st = param->SetFromString(text);
    SyncDebug(st);
    return st;
}

params_status_t ParamRegistry::GetValueString(const char *name, char *out, size_t out_len) const
{
    ParamBase *param = Find(name);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetValueString(out, out_len);
}

params_status_t ParamRegistry::GetDefaultString(const char *name, char *out, size_t out_len) const
{
    ParamBase *param = Find(name);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetDefaultString(out, out_len);
}

params_status_t ParamRegistry::GetMinString(const char *name, char *out, size_t out_len) const
{
    ParamBase *param = Find(name);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetMinString(out, out_len);
}

params_status_t ParamRegistry::GetMaxString(const char *name, char *out, size_t out_len) const
{
    ParamBase *param = Find(name);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetMaxString(out, out_len);
}

params_status_t ParamRegistry::DumpRegistry(params_dump_callback_t cb, void *ctx) const
{
    if (cb == nullptr)
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    for (size_t i = 0U; i < count_; ++i)
    {
        ParamBase *param = entries_[i];
        if (param == nullptr)
        {
            continue;
        }

        params_entry_info_t info {};
        char value[PARAMS_VALUE_STRING_MAX_LEN] {};
        char default_value[PARAMS_VALUE_STRING_MAX_LEN] {};
        char min_value[PARAMS_VALUE_STRING_MAX_LEN] {};
        char max_value[PARAMS_VALUE_STRING_MAX_LEN] {};
        param->FillInfo(&info);

        params_status_t st = param->GetValueString(value, sizeof(value));
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }
        st = param->GetDefaultString(default_value, sizeof(default_value));
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }
        st = param->GetMinString(min_value, sizeof(min_value));
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }
        st = param->GetMaxString(max_value, sizeof(max_value));
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }

        cb(&info, value, default_value, min_value, max_value, ctx);
    }
    return PARAMS_STATUS_OK;
}

size_t ParamRegistry::PayloadSize() const
{
    size_t total = 0U;
    for (size_t i = 0U; i < count_; ++i)
    {
        if (entries_[i] != nullptr)
        {
            total += entries_[i]->Size();
        }
    }
    return total;
}

uint32_t ParamRegistry::LayoutHash() const
{
    uint32_t hash = HashU32(kFnvOffset, static_cast<uint32_t>(count_));
    for (size_t i = 0U; i < count_; ++i)
    {
        if (entries_[i] != nullptr)
        {
            hash = entries_[i]->UpdateLayoutHash(hash);
        }
    }
    return hash;
}

params_status_t ParamRegistry::Load()
{
    params_status_t st = Init();
    if (st != PARAMS_STATUS_OK)
    {
        SyncDebug(st);
        return st;
    }

    const size_t expected_payload = PayloadSize();
    if ((kHeaderSize + expected_payload) > kEepromCapacity)
    {
        SyncDebug(PARAMS_STATUS_NO_SPACE);
        return PARAMS_STATUS_NO_SPACE;
    }

    uint8_t image[kEepromCapacity] {};
    lib_24c02_bl_status_t read_st =
        lib_24c02_bl_read(&s_eeprom,
                          static_cast<uint8_t>(PARAMS_EEPROM_START_ADDR),
                          image,
                          sizeof(image),
                          PARAMS_EEPROM_XFER_TIMEOUT_MS);
    st = map_eeprom_status(read_st);
    if (st != PARAMS_STATUS_OK)
    {
        SyncDebug(st);
        return st;
    }

    if ((get_u32(image, kMagicOffset) != kEepromMagic) ||
        (get_u16(image, kVersionOffset) != kEepromFormatVersion) ||
        (get_u16(image, kHeaderSizeOffset) != kHeaderSize))
    {
        SyncDebug(PARAMS_STATUS_BAD_DATA);
        return PARAMS_STATUS_BAD_DATA;
    }

    const uint16_t entry_count = get_u16(image, kEntryCountOffset);
    const uint16_t payload_size = get_u16(image, kPayloadSizeOffset);
    const size_t total_size = kHeaderSize + static_cast<size_t>(payload_size);

    if ((entry_count != count_) || (payload_size != expected_payload) || (total_size > kEepromCapacity))
    {
        SyncDebug(PARAMS_STATUS_LAYOUT_MISMATCH);
        return PARAMS_STATUS_LAYOUT_MISMATCH;
    }

    const uint16_t stored_crc = get_u16(image, kCrcOffset);
    if (stored_crc != image_crc16(image, total_size))
    {
        SyncDebug(PARAMS_STATUS_CRC_ERROR);
        return PARAMS_STATUS_CRC_ERROR;
    }

    if (get_u32(image, kLayoutHashOffset) != LayoutHash())
    {
        SyncDebug(PARAMS_STATUS_LAYOUT_MISMATCH);
        return PARAMS_STATUS_LAYOUT_MISMATCH;
    }

    params_status_t apply_status = PARAMS_STATUS_OK;
    size_t off = kHeaderSize;
    for (size_t i = 0U; i < count_; ++i) {
        ParamBase *param = entries_[i];
        if (param == nullptr) continue;
        st = param->ValidateBytes(&image[off], param->Size());
        if (st != PARAMS_STATUS_OK) return st;
        off += param->Size();
    }
    {
        detail::ValueGuard guard;
        off = kHeaderSize;
        for (size_t i = 0U; i < count_; ++i) {
            ParamBase *param = entries_[i];
            if (param == nullptr) continue;
            if (param->RemoteWritable()) (void)param->SetFromBytes(&image[off], param->Size());
            off += param->Size();
        }
    }

    debug_begin_if_ready();
    g_omx_dbg_params.load_count++;
    if (apply_status == PARAMS_STATUS_OK)
    {
        g_omx_dbg_params.load_ok++;
    }
    debug_end_if_ready(apply_status);
    SyncDebug(apply_status);
    return apply_status;
}

params_status_t ParamRegistry::Save()
{
    params_status_t st = Init();
    if (st != PARAMS_STATUS_OK)
    {
        SyncDebug(st);
        return st;
    }

    const size_t payload_size = PayloadSize();
    const size_t total_size = kHeaderSize + payload_size;
    if (total_size > kEepromCapacity)
    {
        SyncDebug(PARAMS_STATUS_NO_SPACE);
        return PARAMS_STATUS_NO_SPACE;
    }

    uint8_t image[kEepromCapacity] {};
    put_u32(image, kMagicOffset, kEepromMagic);
    put_u16(image, kVersionOffset, kEepromFormatVersion);
    put_u16(image, kHeaderSizeOffset, static_cast<uint16_t>(kHeaderSize));
    put_u16(image, kEntryCountOffset, static_cast<uint16_t>(count_));
    put_u16(image, kPayloadSizeOffset, static_cast<uint16_t>(payload_size));
    put_u32(image, kLayoutHashOffset, LayoutHash());
    put_u16(image, kCrcOffset, 0U);
    put_u16(image, kReservedOffset, 0U);

    {
    detail::ValueGuard guard;
    size_t off = kHeaderSize;
    for (size_t i = 0U; i < count_; ++i)
    {
        ParamBase *param = entries_[i];
        if (param == nullptr)
        {
            continue;
        }

        st = param->CopyValueTo(&image[off], param->Size());
        if (st != PARAMS_STATUS_OK)
        {
            SyncDebug(st);
            return st;
        }
        off += param->Size();
    }

    }

    put_u16(image, kCrcOffset, image_crc16(image, total_size));

    lib_24c02_bl_status_t write_st =
        lib_24c02_bl_write(&s_eeprom,
                           static_cast<uint8_t>(PARAMS_EEPROM_START_ADDR),
                           image,
                           total_size,
                           PARAMS_EEPROM_XFER_TIMEOUT_MS,
                           PARAMS_EEPROM_WRITE_CYCLE_TIMEOUT_MS);
    st = map_eeprom_status(write_st);

    debug_begin_if_ready();
    g_omx_dbg_params.save_count++;
    if (st == PARAMS_STATUS_OK)
    {
        g_omx_dbg_params.save_ok++;
    }
    debug_end_if_ready(st);
    SyncDebug(st);
    return st;
}

void ParamRegistry::RecordSetStatus(params_status_t status)
{
    if (!s_debug_ready)
    {
        return;
    }

    debug_begin_if_ready();
    if (status == PARAMS_STATUS_OK)
    {
        g_omx_dbg_params.set_ok++;
    }
    else if (status == PARAMS_STATUS_NOT_FOUND)
    {
        g_omx_dbg_params.not_found++;
    }
    else if (status == PARAMS_STATUS_PARSE_ERROR)
    {
        g_omx_dbg_params.parse_error++;
    }
    else if (status == PARAMS_STATUS_OUT_OF_RANGE)
    {
        g_omx_dbg_params.range_error++;
    }
    g_omx_dbg_params.last_error = static_cast<uint32_t>(status);
    debug_end_if_ready(status);
}

void ParamRegistry::SyncDebug(params_status_t status) const
{
    if (!s_debug_ready)
    {
        return;
    }

    debug_begin_if_ready();
    g_omx_dbg_params.registered = static_cast<uint32_t>(count_);
    g_omx_dbg_params.max_count = PARAMS_MAX_COUNT;
    g_omx_dbg_params.register_errors = register_errors_;
    g_omx_dbg_params.duplicate_errors = duplicate_errors_;
    g_omx_dbg_params.payload_size = static_cast<uint32_t>(PayloadSize());
    g_omx_dbg_params.eeprom_start_addr = PARAMS_EEPROM_START_ADDR;
    g_omx_dbg_params.eeprom_capacity = static_cast<uint32_t>(kEepromCapacity);
    g_omx_dbg_params.layout_hash = LayoutHash();
    g_omx_dbg_params.last_error = static_cast<uint32_t>(status);

    switch (status)
    {
    case PARAMS_STATUS_CRC_ERROR:
        g_omx_dbg_params.crc_error++;
        break;
    case PARAMS_STATUS_LAYOUT_MISMATCH:
        g_omx_dbg_params.layout_mismatch++;
        break;
    case PARAMS_STATUS_BAD_DATA:
        g_omx_dbg_params.bad_data++;
        break;
    case PARAMS_STATUS_OUT_OF_RANGE:
        g_omx_dbg_params.range_error++;
        break;
    case PARAMS_STATUS_NO_SPACE:
        g_omx_dbg_params.no_space++;
        break;
    case PARAMS_STATUS_EEPROM_ERROR:
    case PARAMS_STATUS_EEPROM_BUSY:
        g_omx_dbg_params.eeprom_error++;
        break;
    default:
        break;
    }

    debug_end_if_ready(status);
}

} // namespace omx::params

namespace
{

struct DebugPortDumpCtx
{
    params_status_t status;
};

omx::params::ParamBase *param_by_index(size_t index)
{
    return omx::params::ParamRegistry::Instance().GetByIndex(index);
}

void debug_port_dump_cb(const params_entry_info_t *info,
                        const char *value,
                        const char *default_value,
                        const char *min_value,
                        const char *max_value,
                        void *ctx)
{
    auto *dump_ctx = static_cast<DebugPortDumpCtx *>(ctx);
    if ((dump_ctx == nullptr) || (dump_ctx->status != PARAMS_STATUS_OK) || (info == nullptr))
    {
        return;
    }

    char line[192] {};
    int ret = std::snprintf(line,
                            sizeof(line),
                            "%lu %s %s value=%s default=%s min=%s max=%s\r\n",
                            static_cast<unsigned long>(info->index),
                            params_type_name(info->type),
                            (info->name != nullptr) ? info->name : "",
                            value,
                            default_value,
                            min_value,
                            max_value);
    if ((ret < 0) || (static_cast<size_t>(ret) >= sizeof(line)))
    {
        dump_ctx->status = PARAMS_STATUS_BUFFER_TOO_SMALL;
        return;
    }

    if (SRN_DebugPort_WriteExact(line, static_cast<size_t>(ret)) != SRN_DEBUG_PORT_WRITE_OK)
    {
        dump_ctx->status = PARAMS_STATUS_ERROR;
    }
}

} // namespace

extern "C" {

params_status_t SRNOS_ParametersFreeze(void)
{
    return omx::params::ParamRegistry::Instance().Freeze();
}

params_status_t params_snapshot(const uint16_t *indices, size_t count,
                                uint8_t *out, size_t capacity, size_t *written)
{
    if (!written || (!indices && count) || (!out && count) || count > PARAMS_MAX_COUNT)
        return PARAMS_STATUS_INVALID_PARAM;
    *written = 0;
    size_t needed = 0;
    auto &registry = omx::params::ParamRegistry::Instance();
    for (size_t i = 0; i < count; ++i) {
        auto *p = registry.GetByIndex(indices[i]);
        if (!p) return PARAMS_STATUS_NOT_FOUND;
        needed += p->Size();
    }
    if (needed > capacity) return PARAMS_STATUS_BUFFER_TOO_SMALL;
    omx::params::detail::ValueGuard guard;
    for (size_t i = 0; i < count; ++i) {
        auto *p = registry.GetByIndex(indices[i]);
        (void)p->CopyValueTo(out + *written, capacity - *written);
        *written += p->Size();
    }
    return PARAMS_STATUS_OK;
}

params_status_t params_init(void)
{
    return omx::params::ParamRegistry::Instance().Init();
}

params_status_t params_load(void)
{
    return omx::params::ParamRegistry::Instance().Load();
}

params_status_t params_save(void)
{
    return omx::params::ParamRegistry::Instance().Save();
}

size_t params_count(void)
{
    return omx::params::ParamRegistry::Instance().Count();
}

size_t params_payload_size(void)
{
    return omx::params::ParamRegistry::Instance().PayloadSize();
}

uint32_t params_layout_hash(void)
{
    return omx::params::ParamRegistry::Instance().LayoutHash();
}

params_status_t params_get_by_index(size_t index, params_entry_info_t *out)
{
    if (out == nullptr)
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    omx::params::ParamBase *param = omx::params::ParamRegistry::Instance().GetByIndex(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }

    param->FillInfo(out);
    return PARAMS_STATUS_OK;
}

params_status_t params_get_by_name(const char *name, params_entry_info_t *out)
{
    if ((name == nullptr) || (out == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }

    omx::params::ParamBase *param = omx::params::ParamRegistry::Instance().Find(name);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }

    param->FillInfo(out);
    return PARAMS_STATUS_OK;
}

params_status_t params_get_value(const char *name, char *out, size_t out_len)
{
    return omx::params::ParamRegistry::Instance().GetValueString(name, out, out_len);
}

params_status_t params_get_value_by_index(size_t index, char *out, size_t out_len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetValueString(out, out_len);
}

params_status_t params_get_default_value(const char *name, char *out, size_t out_len)
{
    return omx::params::ParamRegistry::Instance().GetDefaultString(name, out, out_len);
}

params_status_t params_get_default_value_by_index(size_t index, char *out, size_t out_len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetDefaultString(out, out_len);
}

params_status_t params_get_min_value(const char *name, char *out, size_t out_len)
{
    return omx::params::ParamRegistry::Instance().GetMinString(name, out, out_len);
}

params_status_t params_get_min_value_by_index(size_t index, char *out, size_t out_len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetMinString(out, out_len);
}

params_status_t params_get_max_value(const char *name, char *out, size_t out_len)
{
    return omx::params::ParamRegistry::Instance().GetMaxString(name, out, out_len);
}

params_status_t params_get_max_value_by_index(size_t index, char *out, size_t out_len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    return param->GetMaxString(out, out_len);
}

params_status_t params_set_value(const char *name, const char *text)
{
    auto *param = omx::params::ParamRegistry::Instance().Find(name);
    if (param == nullptr) return PARAMS_STATUS_NOT_FOUND;
    if (!param->RemoteWritable()) return PARAMS_STATUS_READ_ONLY;
    return param->SetFromString(text);
}

params_status_t params_set_value_by_index(size_t index, const char *text)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        omx::params::ParamRegistry::Instance().RecordSetStatus(PARAMS_STATUS_NOT_FOUND);
        return PARAMS_STATUS_NOT_FOUND;
    }
    if (!param->RemoteWritable()) return PARAMS_STATUS_READ_ONLY;
    return param->SetFromString(text);
}

params_status_t params_copy_binary_by_index(size_t index,
                                            params_binary_value_t value_kind,
                                            uint8_t *out,
                                            size_t out_len,
                                            size_t *written)
{
    if ((out == nullptr) || (written == nullptr))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    params_status_t st = PARAMS_STATUS_INVALID_PARAM;
    switch (value_kind)
    {
    case PARAMS_BINARY_VALUE_CURRENT:
        st = param->CopyValueTo(out, out_len);
        break;
    case PARAMS_BINARY_VALUE_DEFAULT:
        st = param->CopyDefaultTo(out, out_len);
        break;
    case PARAMS_BINARY_VALUE_MIN:
        st = param->CopyMinTo(out, out_len);
        break;
    case PARAMS_BINARY_VALUE_MAX:
        st = param->CopyMaxTo(out, out_len);
        break;
    default:
        break;
    }
    if (st == PARAMS_STATUS_OK)
    {
        *written = param->Size();
    }
    return st;
}

params_status_t params_validate_binary_by_index(size_t index,
                                                const uint8_t *data,
                                                size_t len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        return PARAMS_STATUS_NOT_FOUND;
    }
    if (!param->RemoteWritable()) return PARAMS_STATUS_READ_ONLY;
    return param->ValidateBytes(data, len);
}

params_status_t params_set_binary_by_index(size_t index,
                                           const uint8_t *data,
                                           size_t len)
{
    omx::params::ParamBase *param = param_by_index(index);
    if (param == nullptr)
    {
        omx::params::ParamRegistry::Instance().RecordSetStatus(PARAMS_STATUS_NOT_FOUND);
        return PARAMS_STATUS_NOT_FOUND;
    }
    if (!param->RemoteWritable()) return PARAMS_STATUS_READ_ONLY;
    params_status_t st = param->SetFromBytes(data, len);
    omx::params::ParamRegistry::Instance().SyncDebug(st);
    return st;
}

params_status_t params_set_binary_batch(const params_binary_write_t *writes,
                                        size_t count,
                                        size_t *failed_item)
{
    if (((writes == nullptr) && (count != 0U)) || (count > PARAMS_MAX_COUNT))
    {
        return PARAMS_STATUS_INVALID_PARAM;
    }
    if (failed_item != nullptr)
    {
        *failed_item = count;
    }

    omx::params::detail::ValueGuard guard;
    for (size_t i = 0U; i < count; ++i)
    {
        params_status_t st = params_validate_binary_by_index(writes[i].index,
                                                             writes[i].data,
                                                             writes[i].len);
        if (st != PARAMS_STATUS_OK)
        {
            if (failed_item != nullptr)
            {
                *failed_item = i;
            }
            omx::params::ParamRegistry::Instance().RecordSetStatus(st);
            return st;
        }
    }

    for (size_t i = 0U; i < count; ++i)
    {
        params_status_t st = params_set_binary_by_index(writes[i].index,
                                                        writes[i].data,
                                                        writes[i].len);
        if (st != PARAMS_STATUS_OK)
        {
            if (failed_item != nullptr)
            {
                *failed_item = i;
            }
            return st;
        }
    }
    return PARAMS_STATUS_OK;
}

params_status_t params_dump_registry(params_dump_callback_t cb, void *ctx)
{
    return omx::params::ParamRegistry::Instance().DumpRegistry(cb, ctx);
}

params_status_t params_dump_debug_port(void)
{
    DebugPortDumpCtx ctx { PARAMS_STATUS_OK };
    params_status_t st = params_dump_registry(debug_port_dump_cb, &ctx);
    if (st != PARAMS_STATUS_OK)
    {
        return st;
    }
    return ctx.status;
}

const char *params_status_name(params_status_t status)
{
    switch (status)
    {
    case PARAMS_STATUS_OK:
        return "ok";
    case PARAMS_STATUS_ERROR:
        return "error";
    case PARAMS_STATUS_INVALID_PARAM:
        return "invalid_param";
    case PARAMS_STATUS_NOT_FOUND:
        return "not_found";
    case PARAMS_STATUS_DUPLICATE:
        return "duplicate";
    case PARAMS_STATUS_NO_SPACE:
        return "no_space";
    case PARAMS_STATUS_TYPE_MISMATCH:
        return "type_mismatch";
    case PARAMS_STATUS_OUT_OF_RANGE:
        return "out_of_range";
    case PARAMS_STATUS_BUFFER_TOO_SMALL:
        return "buffer_too_small";
    case PARAMS_STATUS_PARSE_ERROR:
        return "parse_error";
    case PARAMS_STATUS_EEPROM_ERROR:
        return "eeprom_error";
    case PARAMS_STATUS_EEPROM_BUSY:
        return "eeprom_busy";
    case PARAMS_STATUS_BAD_DATA:
        return "bad_data";
    case PARAMS_STATUS_LAYOUT_MISMATCH:
        return "layout_mismatch";
    case PARAMS_STATUS_CRC_ERROR:
        return "crc_error";
    case PARAMS_STATUS_NOT_INITIALIZED:
        return "not_initialized";
    case PARAMS_STATUS_LOCKED: return "locked";
    case PARAMS_STATUS_READ_ONLY: return "read_only";
    default:
        return "unknown";
    }
}

const char *params_type_name(params_type_t type)
{
    switch (type)
    {
    case PARAMS_TYPE_FLOAT:
        return "float";
    case PARAMS_TYPE_INT32:
        return "int32";
    case PARAMS_TYPE_UINT32:
        return "uint32";
    case PARAMS_TYPE_BOOL:
        return "bool";
    case PARAMS_TYPE_INVALID:
    default:
        return "invalid";
    }
}

} // extern "C"
