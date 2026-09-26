cmake_minimum_required(VERSION 3.22)

# CubeMX generates this entry once; the SDK maintains it thereafter.
# A direct Board build delegates to the same SDK assembly.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT SIRINEMBEDOS_BUILDING_SDK)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../CMake/starm-clang.cmake"
                CACHE FILEPATH "SirinEmbedOS standalone toolchain")
    endif()
    project(SirinEmbedOS LANGUAGES C CXX ASM)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(SIRINEMBEDOS_BUILD_EXAMPLE ON CACHE BOOL "Build the minimal SDK firmware")
    add_subdirectory(.. "${CMAKE_CURRENT_BINARY_DIR}/sdk")
    return()
endif()

add_library(sirinos_board OBJECT)
add_library(SirinEmbedOS::Board ALIAS sirinos_board)

# The generated file targets CMAKE_PROJECT_NAME. Confine this assumption to a
# function scope, without changing the parent project or the generated file.
function(sirinos_import_cubemx)
    set(CMAKE_PROJECT_NAME sirinos_board)
    add_subdirectory(cmake/stm32cubemx)
endfunction()
sirinos_import_cubemx()
if(NOT TARGET stm32cubemx OR NOT TARGET STM32_Drivers)
    message(FATAL_ERROR "CubeMX target layout changed; review Board/CMakeLists.txt")
endif()
# Keep generated originals intact; SDK owns libc arena and Fault vectors.
get_target_property(_mx_sources sirinos_board SOURCES)
list(FILTER _mx_sources EXCLUDE REGEX "/sysmem[.]c$")
set_property(TARGET sirinos_board PROPERTY SOURCES "${_mx_sources}")
set_source_files_properties(Core/Src/stm32h7xx_it.c PROPERTIES COMPILE_DEFINITIONS
    "NMI_Handler=SRNOS_UnusedMX_NMI;HardFault_Handler=SRNOS_UnusedMX_HardFault;MemManage_Handler=SRNOS_UnusedMX_MemManage;BusFault_Handler=SRNOS_UnusedMX_BusFault;UsageFault_Handler=SRNOS_UnusedMX_UsageFault;DebugMon_Handler=SRNOS_UnusedMX_DebugMon;DMA1_Stream0_IRQHandler=SRNOS_UnusedMX_DMA1_Stream0;DMA1_Stream1_IRQHandler=SRNOS_UnusedMX_DMA1_Stream1")
target_sources(sirinos_board PRIVATE Glue/kernel_board.c)
target_include_directories(sirinos_board PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Glue"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Source/Kernel")
# CubeMX uses the plain signature for sirinos_board.
target_link_libraries(sirinos_board stm32cubemx sirinos_config)
