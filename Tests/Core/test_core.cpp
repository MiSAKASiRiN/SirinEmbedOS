#include <cassert>
#include <cstdint>
#include <limits>

#include "SirinEmbedOS/Core/SirinOS.hpp"

int main()
{
    std::uint8_t bytes[16]{};
    static_assert(SRNOS::Array::count(bytes) == 16u);
    SRNOS::Memory::MutableByteSpan mutableBytes(bytes);
    SRNOS::Memory::ByteSpan constBytes(mutableBytes);
    assert(mutableBytes.size() == 16u);
    assert(mutableBytes.get(16u) == nullptr);
    const auto middle = mutableBytes.subspan(3u, 5u);
    assert(middle.data() == &bytes[3] && middle.size() == 5u);
    SRNOS::Memory::MutableByteSpan empty;
    assert(empty.empty());
    assert(empty.subspan(0u, 0u).empty());

    std::size_t result = 0u;
    assert(SRNOS::Numeric::checkedAdd(1u, 2u, result) && result == 3u);
    assert(!SRNOS::Numeric::checkedAdd(
        std::numeric_limits<std::size_t>::max(), 1u, result));
    assert(SRNOS::Numeric::clamp(5, 0, 4) == 4);

    assert(SRNOS::Endian::storeLE64(mutableBytes, 1u,
                                 UINT64_C(0x0123456789ABCDEF)));
    std::uint64_t value = 0u;
    assert(SRNOS::Endian::loadLE64(constBytes, 1u, value));
    assert(value == UINT64_C(0x0123456789ABCDEF));
    return 0;
}
