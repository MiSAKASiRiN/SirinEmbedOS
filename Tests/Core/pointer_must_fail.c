#include "SirinEmbedOS/Core/SirinOS.h"

size_t pointer_must_fail(int *pointer)
{
    return SRNOS_ARRAY_COUNT(pointer);
}

