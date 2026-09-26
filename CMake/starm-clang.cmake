# Reuse generated ABI/compiler/runtime selection. Scripts and maps belong to
# firmware targets, so this also works from a parent repository.
get_filename_component(SIRINEMBEDOS_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/../Board" ABSOLUTE)
include("${SIRINEMBEDOS_BOARD_DIR}/cmake/starm-clang.cmake")
foreach(_sirinos_arg IN ITEMS
        "-T \"${CMAKE_SOURCE_DIR}/STM32H723xG_flash.ld\""
        "-Wl,-Map=${CMAKE_PROJECT_NAME}.map")
    string(FIND "${CMAKE_EXE_LINKER_FLAGS}" "${_sirinos_arg}" _sirinos_index)
    if(_sirinos_index EQUAL -1)
        message(FATAL_ERROR "CubeMX linker configuration changed; review CMake/starm-clang.cmake")
    endif()
    string(REPLACE "${_sirinos_arg}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
endforeach()
unset(_sirinos_arg)
unset(_sirinos_index)
