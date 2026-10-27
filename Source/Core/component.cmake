# Explicit foundation manifest; paths are SDK-relative, no source globbing.
set(SRNOS_FOUNDATION_SOURCES
    Source/Core/CRC32.c
    Source/Core/Fault/Fault.c
    Source/Core/Memory/MemorySections.c
    Source/Algorithms/Control/pid.cpp
    Source/Algorithms/Control/control_extensions.cpp
    Source/Algorithms/Math/Matrix.c
    Source/Algorithms/Power/PowerControl.cpp
    Source/Diagnostics/Time/Time.c
    Source/Diagnostics/Runtime/cycle_statistics.c
    Source/Diagnostics/Runtime/runtime_trace.c
    Source/Diagnostics/Runtime/Trace.c
)
set(SRNOS_FOUNDATION_PRIVATE_INCLUDES Source/Diagnostics/Runtime Source/Kernel)
# These bounded hooks run on every managed iteration. Prefer speed over the
# board toolchain's -Oz in Release; retain normal Debug stepping behavior.
set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/../Diagnostics/Runtime/cycle_statistics.c"
    APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>")

set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/../Diagnostics/Time/Time.c"
    APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>")
