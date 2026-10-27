# Fixed-board implementation sources. No globbing and no additional target.
set(SRNOS_SUPPORT_BOARD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/System/kernel_board.c")
set(SRNOS_SUPPORT_PRIVATE_INCLUDES "${CMAKE_CURRENT_LIST_DIR}")
set(SRNOS_SUPPORT_COMPONENT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Devices/monitor_adc_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/Devices/heater_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/Devices/indicators_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/Devices/sensor_frames.c"
    "${CMAKE_CURRENT_LIST_DIR}/Devices/sensors_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/Devices/nor_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/IO/can_board.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/IO/can_native_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/IO/i2c_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/IO/ethernet_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/IO/serial_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/IO/spi_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/fault_handlers.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/boot_identity.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/fault_platform.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/fault_usart1.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/libc_locks.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/libc_memory.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/memory_early.c"
    "${CMAKE_CURRENT_LIST_DIR}/USB/usb_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/USB/usb_device_board.c"
)
