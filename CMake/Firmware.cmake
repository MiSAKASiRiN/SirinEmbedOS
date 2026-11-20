# Source arguments resolve relative to the caller, not the SDK directory.
function(sirinos_add_firmware target)
    if(NOT ARGN)
        message(FATAL_ERROR "sirinos_add_firmware requires application sources")
    endif()
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE SirinEmbedOS::Platform)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_options(${target} PRIVATE "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    set_property(TARGET ${target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
endfunction()
