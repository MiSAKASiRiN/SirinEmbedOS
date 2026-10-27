# Explicit fixed-board buses and devices.
set(SRNOS_DEVICES_SOURCES
    Source/Devices/Sensors/sensors.c
    Source/Devices/Sensors/sensor_codec.c
    Source/Devices/Sensors/sensor_registers.c
    Source/IO/SPI/SPI.c
    Source/IO/I2C/I2C.c
    Source/IO/CAN/External/exfdcan_device.cpp
    Source/IO/CAN/External/exfdcan_rx_router.cpp
    Source/IO/CAN/External/exfdcan_tx_router.cpp
    Source/IO/CAN/can_controller.c
    Source/IO/CAN/FD/can_fd_core.cpp
    Source/IO/CAN/Native/FDCAN.c
    Source/IO/CAN/Router/can_router_core.cpp
    Source/IO/CAN/Router/native_can_rx.cpp
    Source/IO/CAN/Router/native_can_tx.cpp
    Source/Devices/IMU/BMI088/BMI088.c
    Source/Devices/IMU/IMUProtocol.cpp
    Source/Devices/IMU/mahony_filter.c
    Source/Devices/IMU/quaternion_ekf.cpp
    Source/Devices/EEPROM/ft24c128a.c
    Source/Devices/NOR/nor.c
    Source/Devices/MCP2518FD/drv_canfdspi_api.c
    Source/Devices/MCP2518FD/fifo.c
    Source/Devices/MCP2518FD/SPIAdapter.c
    Source/Devices/Motors/C610/C610.cpp
    Source/Devices/Motors/C620/C620.cpp
    Source/Devices/Motors/DM4310/DM4310.cpp
    Source/Devices/Motors/DM4340/DM4340.cpp
    Source/Devices/Motors/GM6020/GM6020.cpp
    Source/Devices/Motors/LK8016E/LK8016E.cpp
    Source/Devices/Buzzer/Buzzer.c
    Source/Devices/WS2812/WS2812.c
    Source/Devices/WS2812/ws2812_critical.c
    Source/Devices/NaviVision/NaviVision.cpp
    Source/Devices/Supercapacitor/Supercapacitor.cpp
)
set(SRNOS_DEVICES_PRIVATE_INCLUDES
    Source/IO/CAN/Router
    Source/IO/CAN/External
)

# The communication hot path prioritizes bounded latency over minimum Flash
# size. Keep Debug stepping and vendor compilation policy unchanged; -O2 does
# not enable fast-math or move code/data into different memory banks.
foreach(_srnos_communication_source IN ITEMS
    Source/IO/CAN/can_controller.c
    Source/IO/CAN/FD/can_fd_core.cpp
    Source/IO/CAN/Native/FDCAN.c
    Source/IO/CAN/Router/can_router_core.cpp
    Source/IO/CAN/Router/native_can_rx.cpp
    Source/IO/CAN/Router/native_can_tx.cpp
    Source/IO/CAN/External/exfdcan_device.cpp
    Source/IO/CAN/External/exfdcan_rx_router.cpp
    Source/IO/CAN/External/exfdcan_tx_router.cpp
    Source/Devices/MCP2518FD/fifo.c
    Source/IO/SPI/SPI.c)
    set_property(SOURCE "${SIRINEMBEDOS_ROOT}/${_srnos_communication_source}"
        APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>")
endforeach()
