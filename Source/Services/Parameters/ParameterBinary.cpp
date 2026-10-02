#include "parameters_internal.h"

namespace SRNOS::Parameters::detail
{

void CopyBytesUnalignedSafe(void *destination,
                            const void *source,
                            size_t length)
{
    auto *out = static_cast<volatile uint8_t *>(destination);
    const auto *in = static_cast<const volatile uint8_t *>(source);
    for (size_t i = 0U; i < length; ++i)
    {
        out[i] = in[i];
    }
}

} // namespace SRNOS::Parameters::detail
