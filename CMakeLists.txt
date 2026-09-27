cmake_minimum_required(VERSION 3.22)

# A parent project owns project(), the toolchain and its firmware target.
set(_sirinos_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_sirinos_standalone ON)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/CMake/starm-clang.cmake"
                CACHE FILEPATH "SirinEmbedOS standalone toolchain")
    endif()
    project(SirinEmbedOS LANGUAGES C CXX ASM)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Generic" OR NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Use the ST Arm Clang/Picolibc toolchain before project(); see README.md")
endif()
if(CMAKE_EXE_LINKER_FLAGS MATCHES "STM32H723xG_flash[.]ld")
    message(FATAL_ERROR "Use CMake/starm-clang.cmake, not Board/cmake/starm-clang.cmake. After changing toolchains, clear the CMake cache (cmake --fresh --preset Debug).")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_language(C CXX ASM)

option(SIRINEMBEDOS_BUILD_EXAMPLE "Build the selected SDK example firmware" ${_sirinos_standalone})
set(SIRINEMBEDOS_EXAMPLE "Minimal" CACHE STRING "SDK example: Minimal, BringUp, SensorBringUp, INSBringUp or EEPROMBringUp")
set_property(CACHE SIRINEMBEDOS_EXAMPLE PROPERTY STRINGS Minimal BringUp SensorBringUp INSBringUp EEPROMBringUp)
set(SIRINEMBEDOS_CONFIG_HEADER "" CACHE FILEPATH "Absolute path to application SDK configuration")
add_library(sirinos_config INTERFACE)
target_include_directories(sirinos_config INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include" "${CMAKE_CURRENT_SOURCE_DIR}/Config")
if(SIRINEMBEDOS_CONFIG_HEADER)
    if(NOT IS_ABSOLUTE "${SIRINEMBEDOS_CONFIG_HEADER}" OR NOT EXISTS "${SIRINEMBEDOS_CONFIG_HEADER}")
        message(FATAL_ERROR "SIRINEMBEDOS_CONFIG_HEADER must name an existing absolute file")
    endif()
    target_compile_definitions(sirinos_config INTERFACE
            SIRINEMBEDOS_CONFIG_HEADER="${SIRINEMBEDOS_CONFIG_HEADER}")
endif()

target_compile_definitions(sirinos_config INTERFACE SRNOS_MEMORY_SECTIONS=1)

set(SIRINEMBEDOS_BUILDING_SDK TRUE)
add_subdirectory(Board)
add_subdirectory(Source/Kernel)
target_link_libraries(sirinos_board sirinos_kernel)

set(SIRINEMBEDOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(Source/Core/component.cmake)
include(Source/Devices/component.cmake)
include(Source/IO/Serial/component.cmake)
include(Source/Services/component.cmake)
add_library(sirinos_components OBJECT
    ${SRNOS_FOUNDATION_SOURCES} ${SRNOS_DEVICES_SOURCES}
    ${SRNOS_SERIAL_SOURCES} ${SRNOS_SERVICES_SOURCES})
target_link_libraries(sirinos_components PUBLIC sirinos_config sirinos_kernel stm32cubemx)
target_include_directories(sirinos_components PRIVATE
    ${SRNOS_FOUNDATION_PRIVATE_INCLUDES} ${SRNOS_DEVICES_PRIVATE_INCLUDES}
    ${SRNOS_SERIAL_PRIVATE_INCLUDES} ${SRNOS_SERVICES_PRIVATE_INCLUDES}
    )
target_include_directories(sirinos_components SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/CMSISDSP/Include")
target_compile_definitions(sirinos_components PRIVATE ${SRNOS_SERVICES_DEFINITIONS})
target_compile_options(sirinos_components PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>)

# Transitive OBJECT dependencies only propagate usage requirements. Explicitly
# forward objects so startup/ISR code reaches every firmware without extraction.
add_library(sirinos_platform INTERFACE)
add_library(SirinEmbedOS::Platform ALIAS sirinos_platform)
target_link_libraries(sirinos_platform INTERFACE
        sirinos_board STM32_Drivers sirinos_kernel sirinos_components
        $<TARGET_OBJECTS:sirinos_board>
        $<TARGET_OBJECTS:STM32_Drivers>
        $<TARGET_OBJECTS:sirinos_kernel>
        $<TARGET_OBJECTS:sirinos_components>)
set(SRNOS_MSP_BYTES 8192 CACHE STRING "Main/interrupt stack reservation, bytes")
set(SRNOS_LIBC_BYTES 8192 CACHE STRING "Independent Picolibc arena reservation, bytes")
foreach(_capacity IN ITEMS SRNOS_MSP_BYTES SRNOS_LIBC_BYTES)
    if(NOT "${${_capacity}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${_capacity} must be a positive byte count")
    endif()
endforeach()
target_link_options(sirinos_platform INTERFACE
        "LINKER:--defsym,SRNOS_MSP_BYTES=${SRNOS_MSP_BYTES}"
        "LINKER:--defsym,SRNOS_LIBC_BYTES=${SRNOS_LIBC_BYTES}"
        "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/Board/Linker/SirinEmbedOS.ld")
set_property(TARGET sirinos_platform PROPERTY INTERFACE_LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Linker/SirinEmbedOS.ld")
include(CMake/Firmware.cmake)

if(SIRINEMBEDOS_BUILD_EXAMPLE)
    if(NOT SIRINEMBEDOS_EXAMPLE STREQUAL "Minimal" AND NOT SIRINEMBEDOS_EXAMPLE STREQUAL "BringUp" AND NOT SIRINEMBEDOS_EXAMPLE STREQUAL "SensorBringUp" AND NOT SIRINEMBEDOS_EXAMPLE STREQUAL "INSBringUp" AND NOT SIRINEMBEDOS_EXAMPLE STREQUAL "EEPROMBringUp")
        message(FATAL_ERROR "Unknown SIRINEMBEDOS_EXAMPLE: ${SIRINEMBEDOS_EXAMPLE}; use Minimal, BringUp, SensorBringUp, INSBringUp or EEPROMBringUp")
    endif()
    sirinos_add_firmware(SirinEmbedOS "Examples/${SIRINEMBEDOS_EXAMPLE}/app_main.c")
endif()
