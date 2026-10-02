#ifndef SRNOS_MEMORY_HPP
#define SRNOS_MEMORY_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Core/C/Memory.h"

namespace SRNOS::Memory {

/** @brief Non-owning contiguous view with checked indexing and subviews. */
template <typename T>
class Span {
public:
    using element_type = T;
    using size_type = std::size_t;

    /** @brief Construct an empty span. */
    constexpr Span() noexcept = default;
    /** @brief Construct from pointer and size; a nonempty null span Panics. */
    constexpr Span(T *data, size_type size) noexcept : data_(data), size_(size)
    {
        SRNOS_ASSERT((data != nullptr) || (size == 0u));
    }

    /** @brief Construct a span covering a native array. */
    template <std::size_t N>
    constexpr Span(T (&array)[N]) noexcept : data_(array), size_(N) {}

    /** @brief Convert from a span with a compatible element type. */
    template <typename U,
              typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
    constexpr Span(const Span<U> &other) noexcept
        : data_(other.data()), size_(other.size()) {}

    /** @brief Return the first element address, or null for a null empty span. */
    [[nodiscard]] constexpr T *data() const noexcept { return data_; }
    /** @brief Return the element count. */
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    /** @brief Return whether the span contains no elements. */
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0u; }

    /** @brief Return an element reference; an invalid index Panics. */
    constexpr T &operator[](size_type index) const noexcept
    {
        SRNOS_ASSERT(index < size_);
        return data_[index];
    }

    /** @brief Return an element pointer, or null when the index is invalid. */
    [[nodiscard]] constexpr T *get(size_type index) const noexcept
    {
        return (index < size_) ? &data_[index] : nullptr;
    }

    /** @brief Return a checked subview; an invalid range Panics. */
    [[nodiscard]] constexpr Span subspan(size_type offset,
                                         size_type length) const noexcept
    {
        SRNOS_ASSERT(SRNOS_Bounds_RangeValid(offset, length, size_));
        return (data_ == nullptr) ? Span{} : Span{data_ + offset, length};
    }

private:
    T *data_ = nullptr;
    size_type size_ = 0u;
};

using ByteSpan = Span<const std::uint8_t>;
using MutableByteSpan = Span<std::uint8_t>;

/** @brief Byte-wide non-overlapping copy with capacity validation. */
[[nodiscard]] inline bool copy(MutableByteSpan destination,
                               ByteSpan source) noexcept
{
    return SRNOS_Memory_Copy(SRNOS_Memory_MutableBytes(destination.data(), destination.size()),
                          SRNOS_Memory_Bytes(source.data(), source.size()));
}

/** @brief Byte-wide overlap-safe move with capacity validation. */
[[nodiscard]] inline bool move(MutableByteSpan destination,
                               ByteSpan source) noexcept
{
    return SRNOS_Memory_Move(SRNOS_Memory_MutableBytes(destination.data(), destination.size()),
                          SRNOS_Memory_Bytes(source.data(), source.size()));
}

/** @brief Fill a writable byte span. */
[[nodiscard]] inline bool fill(MutableByteSpan destination,
                               std::uint8_t value) noexcept
{
    return SRNOS_Memory_Fill(SRNOS_Memory_MutableBytes(destination.data(), destination.size()), value);
}

} // namespace SRNOS::Memory

#endif
