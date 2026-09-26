#pragma once

#include "SirinEmbedOS/Services/Parameters/lib_params.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace omx::params
{

uint32_t HashBytes(uint32_t hash, const void *data, size_t len);
uint32_t HashU32(uint32_t hash, uint32_t value);
uint32_t HashName(const char *name);

namespace detail
{
uint32_t LockValues();
void UnlockValues(uint32_t state);
class ValueGuard {
public:
    ValueGuard() : state_(LockValues()) {}
    ~ValueGuard() { UnlockValues(state_); }
    ValueGuard(const ValueGuard &) = delete;
    ValueGuard &operator=(const ValueGuard &) = delete;
private:
    uint32_t state_;
};

/*
 * Wire/EEPROM payloads are byte streams and are not guaranteed to be aligned.
 * Volatile byte accesses prevent GCC from folding a fixed-size copy back into
 * LDR/STR instructions, which fault when CCR.UNALIGN_TRP is enabled.
 */
void CopyBytesUnalignedSafe(void *destination,
                            const void *source,
                            size_t length);

} // namespace detail

template <typename T>
struct ParamSupported : std::false_type
{
};

template <>
struct ParamSupported<float> : std::true_type
{
};

template <>
struct ParamSupported<int32_t> : std::true_type
{
};

template <>
struct ParamSupported<uint32_t> : std::true_type
{
};

template <>
struct ParamSupported<bool> : std::true_type
{
};

template <typename T>
struct ParamTraits;

template <>
struct ParamTraits<float>
{
    static constexpr params_type_t Type = PARAMS_TYPE_FLOAT;
    static params_status_t Parse(const char *text, float *out);
    static params_status_t Format(float value, char *out, size_t out_len);
};

template <>
struct ParamTraits<int32_t>
{
    static constexpr params_type_t Type = PARAMS_TYPE_INT32;
    static params_status_t Parse(const char *text, int32_t *out);
    static params_status_t Format(int32_t value, char *out, size_t out_len);
};

template <>
struct ParamTraits<uint32_t>
{
    static constexpr params_type_t Type = PARAMS_TYPE_UINT32;
    static params_status_t Parse(const char *text, uint32_t *out);
    static params_status_t Format(uint32_t value, char *out, size_t out_len);
};

template <>
struct ParamTraits<bool>
{
    static constexpr params_type_t Type = PARAMS_TYPE_BOOL;
    static params_status_t Parse(const char *text, bool *out);
    static params_status_t Format(bool value, char *out, size_t out_len);
};

class ParamBase
{
    friend class ParamRegistry;

public:
    ParamBase(const ParamBase &) = delete;
    ParamBase &operator=(const ParamBase &) = delete;
    virtual ~ParamBase() = default;

    const char *Name() const { return name_; }
    params_type_t Type() const { return type_; }
    size_t Size() const { return size_; }
    uint32_t NameHash() const { return name_hash_; }
    uint32_t Index() const { return index_; }
    bool Registered() const { return registered_; }
    bool RemoteWritable() const { return access_ == PARAMS_MUTABLE_BUSINESS; }
    params_status_t RegistrationStatus() const { return registration_status_; }

    void FillInfo(params_entry_info_t *out) const;

    virtual params_status_t SetFromString(const char *text) = 0;
    virtual params_status_t GetValueString(char *out, size_t out_len) const = 0;
    virtual params_status_t GetDefaultString(char *out, size_t out_len) const = 0;
    virtual params_status_t GetMinString(char *out, size_t out_len) const = 0;
    virtual params_status_t GetMaxString(char *out, size_t out_len) const = 0;
    virtual params_status_t CopyValueTo(uint8_t *out, size_t max_len) const = 0;
    virtual params_status_t CopyDefaultTo(uint8_t *out, size_t max_len) const = 0;
    virtual params_status_t CopyMinTo(uint8_t *out, size_t max_len) const = 0;
    virtual params_status_t CopyMaxTo(uint8_t *out, size_t max_len) const = 0;
    virtual params_status_t ValidateBytes(const uint8_t *data, size_t len) const = 0;
    virtual params_status_t SetFromBytes(const uint8_t *data, size_t len) = 0;
    virtual void ResetDefault() = 0;
    virtual params_status_t ValidateDefinition() const = 0;
    virtual uint32_t UpdateLayoutHash(uint32_t hash) const = 0;

protected:
    ParamBase(const char *name, params_type_t type, size_t size, params_access_t access);

    void MarkRegistered(uint32_t index);
    void MarkRegistrationFailed(params_status_t status);

private:
    const char *name_;
    params_type_t type_;
    size_t size_;
    uint32_t name_hash_;
    uint32_t index_;
    bool registered_;
    params_status_t registration_status_;
    params_access_t access_;
};

class ParamRegistry
{
public:
    ParamRegistry(const ParamRegistry &) = delete;
    ParamRegistry &operator=(const ParamRegistry &) = delete;

    static ParamRegistry &Instance();

    params_status_t Register(ParamBase &param);
    params_status_t Init();
    params_status_t Freeze();
    bool Frozen() const { return frozen_; }
    params_status_t Load();
    params_status_t Save();

    size_t Count() const { return count_; }
    ParamBase *GetByIndex(size_t index) const;
    ParamBase *Find(const char *name) const;
    params_status_t SetFromString(const char *name, const char *text);
    params_status_t GetValueString(const char *name, char *out, size_t out_len) const;
    params_status_t GetDefaultString(const char *name, char *out, size_t out_len) const;
    params_status_t GetMinString(const char *name, char *out, size_t out_len) const;
    params_status_t GetMaxString(const char *name, char *out, size_t out_len) const;
    params_status_t DumpRegistry(params_dump_callback_t cb, void *ctx) const;

    size_t PayloadSize() const;
    uint32_t LayoutHash() const;

    void RecordSetStatus(params_status_t status);
    void SyncDebug(params_status_t status) const;

private:
    ParamRegistry();

    ParamBase *entries_[PARAMS_MAX_COUNT];
    size_t count_;
    uint32_t register_errors_;
    uint32_t duplicate_errors_;
    bool frozen_;
};

template <typename T>
class Param final : public ParamBase
{
    static_assert(ParamSupported<T>::value,
                  "Param<T> supports only float, int32_t, uint32_t, and bool");

public:
    Param(const char *name, T default_value, T min_value, T max_value,
          params_access_t access = PARAMS_READ_ONLY)
        : ParamBase(name, ParamTraits<T>::Type, sizeof(T), access),
          value_(default_value),
          default_(default_value),
          min_(min_value),
          max_(max_value)
    {
        ParamRegistry::Instance().Register(*this);
    }

    T Get() const { detail::ValueGuard guard; return value_; }
    T Default() const { return default_; }
    T Min() const { return min_; }
    T Max() const { return max_; }

    params_status_t Set(T value)
    {
        if (!IsInRange(value))
        {
            ParamRegistry::Instance().RecordSetStatus(PARAMS_STATUS_OUT_OF_RANGE);
            return PARAMS_STATUS_OUT_OF_RANGE;
        }

        { detail::ValueGuard guard; value_ = value; }
        ParamRegistry::Instance().RecordSetStatus(PARAMS_STATUS_OK);
        return PARAMS_STATUS_OK;
    }

    params_status_t SetFromString(const char *text) override
    {
        T parsed {};
        params_status_t st = ParamTraits<T>::Parse(text, &parsed);
        if (st != PARAMS_STATUS_OK)
        {
            ParamRegistry::Instance().RecordSetStatus(st);
            return st;
        }
        return Set(parsed);
    }

    params_status_t GetValueString(char *out, size_t out_len) const override
    {
        return ParamTraits<T>::Format(Get(), out, out_len);
    }

    params_status_t GetDefaultString(char *out, size_t out_len) const override
    {
        return ParamTraits<T>::Format(default_, out, out_len);
    }

    params_status_t GetMinString(char *out, size_t out_len) const override
    {
        return ParamTraits<T>::Format(min_, out, out_len);
    }

    params_status_t GetMaxString(char *out, size_t out_len) const override
    {
        return ParamTraits<T>::Format(max_, out, out_len);
    }

    params_status_t CopyValueTo(uint8_t *out, size_t max_len) const override
    {
        return CopyScalarTo(Get(), out, max_len);
    }

    params_status_t CopyDefaultTo(uint8_t *out, size_t max_len) const override
    {
        return CopyScalarTo(default_, out, max_len);
    }

    params_status_t CopyMinTo(uint8_t *out, size_t max_len) const override
    {
        return CopyScalarTo(min_, out, max_len);
    }

    params_status_t CopyMaxTo(uint8_t *out, size_t max_len) const override
    {
        return CopyScalarTo(max_, out, max_len);
    }

    params_status_t ValidateBytes(const uint8_t *data, size_t len) const override
    {
        T tmp {};
        params_status_t st = DecodeScalar(data, len, &tmp);
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }
        return IsInRange(tmp) ? PARAMS_STATUS_OK : PARAMS_STATUS_OUT_OF_RANGE;
    }

    params_status_t SetFromBytes(const uint8_t *data, size_t len) override
    {
        T tmp {};
        params_status_t st = DecodeScalar(data, len, &tmp);
        if (st != PARAMS_STATUS_OK)
        {
            return st;
        }
        return Set(tmp);
    }

    void ResetDefault() override
    {
        detail::ValueGuard guard;
        value_ = default_;
    }

    params_status_t ValidateDefinition() const override
    {
        if (!IsScalarValid(default_) || !IsScalarValid(min_) || !IsScalarValid(max_))
        {
            return PARAMS_STATUS_OUT_OF_RANGE;
        }
        if (max_ < min_)
        {
            return PARAMS_STATUS_OUT_OF_RANGE;
        }
        if (!IsInRange(default_))
        {
            return PARAMS_STATUS_OUT_OF_RANGE;
        }
        return PARAMS_STATUS_OK;
    }

    uint32_t UpdateLayoutHash(uint32_t hash) const override
    {
        hash = HashU32(hash, NameHash());
        hash = HashU32(hash, static_cast<uint32_t>(Type()));
        hash = HashU32(hash, static_cast<uint32_t>(Size()));
        hash = HashBytes(hash, &default_, sizeof(T));
        hash = HashBytes(hash, &min_, sizeof(T));
        hash = HashBytes(hash, &max_, sizeof(T));
        return hash;
    }

private:
    static params_status_t CopyScalarTo(const T &value, uint8_t *out, size_t max_len)
    {
        if (out == nullptr)
        {
            return PARAMS_STATUS_INVALID_PARAM;
        }
        if (max_len < sizeof(T))
        {
            return PARAMS_STATUS_BUFFER_TOO_SMALL;
        }
        detail::CopyBytesUnalignedSafe(out, &value, sizeof(T));
        return PARAMS_STATUS_OK;
    }

    static params_status_t DecodeScalar(const uint8_t *data, size_t len, T *out)
    {
        if ((data == nullptr) || (out == nullptr))
        {
            return PARAMS_STATUS_INVALID_PARAM;
        }
        if (len != sizeof(T))
        {
            return PARAMS_STATUS_TYPE_MISMATCH;
        }

        detail::CopyBytesUnalignedSafe(out, data, sizeof(T));
        return PARAMS_STATUS_OK;
    }

    static bool IsScalarValid(T value)
    {
        return value == value;
    }

    bool IsInRange(T value) const
    {
        return IsScalarValid(value) && !(value < min_) && !(max_ < value);
    }

    T value_;
    T default_;
    T min_;
    T max_;
};

} // namespace omx::params
