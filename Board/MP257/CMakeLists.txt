set(STM32CUBE_MP2_ROOT "$ENV{STM32CUBE_MP2_ROOT}" CACHE PATH "STM32CubeMP2 V1.3.0 installation")
if(NOT STM32CUBE_MP2_ROOT)
    set(STM32CUBE_MP2_ROOT "$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_MP2_V1.3.0")
endif()
set(drivers "${STM32CUBE_MP2_ROOT}/Drivers")
set(device "${drivers}/CMSIS/Device/ST/STM32MP2xx")
set(templates "${device}/Source/Templates")
set(hal "${drivers}/STM32MP2xx_HAL_Driver")
set(port "${STM32CUBE_MP2_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure")
if(NOT EXISTS "${port}/portasm.c")
    message(FATAL_ERROR "Set STM32CUBE_MP2_ROOT to the STM32CubeMP2 V1.3.0 package")
endif()
file(STRINGS "${STM32CUBE_MP2_ROOT}/package.xml" release REGEX "PackDescription Release=")
if(NOT release MATCHES "Release=\"FW[.]MP2[.]1[.]3[.]0\"")
    message(FATAL_ERROR "This port requires STM32CubeMP2 V1.3.0")
endif()
get_filename_component(sdk "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(rtos "${sdk}/ThirdParty/FreeRTOS/Source")
add_library(sirinos_mp257 OBJECT
    boot.c IPC/peer.c "${templates}/gcc/startup_stm32mp257dxx_m33.s"
    "${templates}/system_stm32mp2xx_m33.c"
    "${rtos}/tasks.c" "${rtos}/queue.c" "${rtos}/list.c" "${rtos}/timers.c"
    "${rtos}/event_groups.c" "${rtos}/stream_buffer.c" "${rtos}/portable/MemMang/heap_5.c"
    "${rtos}/CMSIS_RTOS_V2/cmsis_os2.c" "${port}/port.c" "${port}/portasm.c")
set(kernel "${sdk}/Source/Kernel")
set(can "${sdk}/Source/IO/CAN")
target_sources(sirinos_mp257 PRIVATE can.c "${sdk}/Source/Algorithms/Control/pid.cpp"
    "${sdk}/Board/Support/System/libc_locks.c" "${sdk}/Board/Support/System/libc_memory.c"
    "${kernel}/thread.c" "${kernel}/thread_config.c" "${kernel}/object_pool.c" "${kernel}/sync.c"
    "${can}/Native/FDCAN.c" "${can}/can_controller.c" "${can}/FD/can_fd_core.cpp"
    "${can}/Router/can_router_core.cpp" "${can}/Router/native_can_rx.cpp" "${can}/Router/native_can_tx.cpp"
    "${hal}/Src/stm32mp2xx_hal_fdcan.c" "${hal}/Src/stm32mp2xx_hal_gpio.c" "${hal}/Src/stm32mp2xx_ll_rcc.c")
foreach(motor IN ITEMS C610 C620 DM GM6020 LK8016E)
    target_sources(sirinos_mp257 PRIVATE "${sdk}/Source/Devices/Motors/${motor}/${motor}.cpp")
endforeach()
target_compile_definitions(sirinos_config INTERFACE SRNOS_CAN_CONTROLLER_COUNT=3 SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_COUNT=0)
target_compile_definitions(sirinos_mp257 PRIVATE USE_FULL_LL_DRIVER)
option(SIRINEMBEDOS_MP257_CAN_LOOPBACK "Use FDCAN internal loopback on all three controllers" OFF)
if(SIRINEMBEDOS_MP257_CAN_LOOPBACK)
    target_compile_definitions(sirinos_mp257 PRIVATE SRNOS_MP257_CAN_LOOPBACK)
endif()
target_include_directories(sirinos_mp257 PRIVATE . "${sdk}/Board/Support" "${sdk}/Config" "${sdk}/Source"
    "${sdk}/Source/Kernel" "${sdk}/Source/Diagnostics/Runtime"
    "${rtos}/include" "${rtos}/CMSIS_RTOS_V2" "${sdk}/ThirdParty/FreeRTOS/CMSIS/Include"
    "${port}" "${device}/Include" "${drivers}/CMSIS/Include" "${hal}/Inc")
target_compile_definitions(sirinos_mp257 PRIVATE CORE_CM33 STM32MP257Dxx SRNOS_MP257_M33)
target_link_libraries(sirinos_mp257 PRIVATE sirinos_config)
add_library(sirinos_platform INTERFACE)
add_library(SirinEmbedOS::Platform ALIAS sirinos_platform)
target_link_libraries(sirinos_platform INTERFACE sirinos_config $<TARGET_OBJECTS:sirinos_mp257>)
target_link_options(sirinos_platform INTERFACE "LINKER:--wrap=__libc_init_array" "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/remoteproc.ld")
set_property(TARGET sirinos_platform PROPERTY INTERFACE_LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/remoteproc.ld")
include("${sdk}/CMake/Firmware.cmake")
if(SIRINEMBEDOS_BUILD_EXAMPLE)
    sirinos_add_firmware(SirinEmbedOS-MP257 "${sdk}/Examples/Minimal/app_main.c")
endif()
