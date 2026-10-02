# Source arguments resolve relative to the caller, not the SDK directory.
function(sirinos_add_firmware target)
    if(NOT ARGN)
        message(FATAL_ERROR "sirinos_add_firmware requires sources implementing SRNOS_ApplicationMain")
    endif()
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE SirinEmbedOS::Platform)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_options(${target} PRIVATE "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    set_property(TARGET ${target} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
endfunction()

# SDK validation only. Keep native-kernel test code in a separate translation
# unit/target so application source files never inherit internal include paths.
function(_srnos_attach_kernel_test_adapter target source)
    set(_adapter "${target}_kernel_test_adapter")
    add_library(${_adapter} OBJECT "${source}")
    target_link_libraries(${_adapter} PRIVATE sirinos_build_internal)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${_adapter}>)
endfunction()
