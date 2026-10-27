set(SRNOS_SERIAL_SOURCES
  "${SIRINEMBEDOS_ROOT}/Source/Devices/H12H/H12H.c"
  "${SIRINEMBEDOS_ROOT}/Source/Devices/TL42R/TL42R.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/RS485/Modbus/ModbusRTU.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/RS485/rs485_router.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/IO/Serial/serial.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/Receiver/RefereeReceiver.cpp"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/Transmitter/RefereeTransmitter.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/UI/UI.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/referee.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/Referee/startup.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/DR16/DR16.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/VT03/VT03.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/WBus/WBus.c"
  "${SIRINEMBEDOS_ROOT}/Source/Services/RemoteControl/remote_worker.c"
)
set(SRNOS_SERIAL_PRIVATE_INCLUDES "${SIRINEMBEDOS_ROOT}/Source/IO/Serial")
