set(SRNOS_SERIAL_SOURCES
  "${SIRINEMBEDOS_ROOT}/Board/Glue/serial_ports.c"
  "${SIRINEMBEDOS_ROOT}/Source/Devices/H12H/lib_serial_servo_h12h.c"
  "${SIRINEMBEDOS_ROOT}/Source/Devices/TL42R/lib_rs485_lc_tl42r.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/RS485/Modbus/lib_modbus_rtu.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/RS485/tskptt_rs485_router.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/Serial/serial.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/Receiver/tskptt_referee.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/Transmitter/tskptt_referee_tx.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/UI/ui_interface.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/referee.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/startup.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/DR16/bsp_dr16.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/VT03/bsp_vt03.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/lib_remote_control.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/remote_worker.c"
)
set(SRNOS_SERIAL_PRIVATE_INCLUDES "${SIRINEMBEDOS_ROOT}/Source/IO/Serial")
