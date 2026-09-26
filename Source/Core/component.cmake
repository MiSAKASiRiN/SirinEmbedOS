# Explicit foundation manifest; paths are SDK-relative, no source globbing.
set(SRNOS_FOUNDATION_SOURCES
    Source/Core/Fault/Fault.c
    Source/Core/Memory/mem_sections.c
    Source/Algorithms/Control/pid.cpp
    Source/Algorithms/Control/control_extensions.cpp
    Source/Algorithms/Math/cmsis_matrix_backend.c
    Source/Algorithms/Power/lib_power_control.cpp
    Source/Diagnostics/Time/bsp_dwt.c
    Source/Diagnostics/Watch/omx_debug_watch.c
    Source/Diagnostics/Runtime/tskptt_hskp.c
    Source/Diagnostics/Runtime/tskptt_hskp_trace.c
    Source/Diagnostics/Runtime/Trace.c
    Source/Diagnostics/Runtime/Startup.c
    Board/Glue/Platform.c
    Board/Glue/fault_handlers.c
    Board/Glue/memory_early.c
    Board/Glue/libc_memory.c
    Board/Glue/libc_locks.c
)
set(SRNOS_FOUNDATION_PRIVATE_INCLUDES Board/Glue Source/Diagnostics/Runtime)
