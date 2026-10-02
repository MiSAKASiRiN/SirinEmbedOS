set(_freertos "${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/FreeRTOS")
add_library(sirinos_kernel OBJECT
        kernel.c system_config.c hooks.c thread.c thread_config.c sync.c object_pool.c
        "${_freertos}/Source/tasks.c"
        "${_freertos}/Source/queue.c"
        "${_freertos}/Source/list.c"
        "${_freertos}/Source/timers.c"
        "${_freertos}/Source/event_groups.c"
        "${_freertos}/Source/stream_buffer.c"
        "${_freertos}/Source/portable/GCC/ARM_CM4F/port.c"
        "${_freertos}/Source/portable/MemMang/heap_5.c"
        "${_freertos}/Source/CMSIS_RTOS_V2/cmsis_os2.c")
add_library(SirinEmbedOS::Kernel ALIAS sirinos_kernel)
target_link_libraries(sirinos_kernel PRIVATE sirinos_build_internal)

target_include_directories(sirinos_kernel PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Diagnostics/Runtime")
