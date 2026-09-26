#include <cassert>
#include <csetjmp>
#include <cstdint>

#include "SirinEmbedOS/Core/Memory.hpp"

static std::jmp_buf s_resetJump;
static unsigned s_resets;

extern "C" void SRNOS_Platform_FaultCapture(SRNOS_Fault_Context *) {}
extern "C" void SRNOS_Platform_FaultReport(const SRNOS_Fault_Record *) {}

extern "C" __attribute__((noreturn)) void SRNOS_Platform_PanicTerminate(void)
{
    ++s_resets;
    std::longjmp(s_resetJump, 1);
}

int main()
{
    SRNOS_Fault_Initialize();
    std::uint8_t bytes[2]{};
    SRNOS::Memory::MutableByteSpan span(bytes);
    if (setjmp(s_resetJump) == 0) {
        (void)span.subspan(1u, 2u);
    }
    assert(s_resets == 1u);
    return 0;
}
