# Explicit service source list.
get_filename_component(_srnos_services_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_srnos_usb "${_srnos_services_root}/ThirdParty/USBDevice")
set(_srnos_usb_glue "${CMAKE_CURRENT_LIST_DIR}/USB")
set(_srnos_usb_build "${CMAKE_CURRENT_BINARY_DIR}/srnos_usb")
file(MAKE_DIRECTORY "${_srnos_usb_build}")
file(READ "${_srnos_usb}/Class/COMPOSITE/Src/usbd_composite.c" _srnos_composite)
foreach(_anchor IN ITEMS "#include \"usbd_ctlreq.h\"" "char str_buffer[16] = \"\";" "snprintf(str_buffer, sizeof(str_buffer), CDC_ACM_STR_DESC, i);")
  string(FIND "${_srnos_composite}" "${_anchor}" _position)
  if(_position EQUAL -1)
    message(FATAL_ERROR "USB snapshot patch anchor missing: ${_anchor}")
  endif()
endforeach()
string(REPLACE "#include \"usbd_ctlreq.h\"" "#include \"usbd_ctlreq.h\"\n#include \"usb_cdc_channels.h\"" _srnos_composite "${_srnos_composite}")
string(REPLACE "char str_buffer[16] = \"\";" "const char *str_buffer = NULL;" _srnos_composite "${_srnos_composite}")
string(REPLACE "snprintf(str_buffer, sizeof(str_buffer), CDC_ACM_STR_DESC, i);" "str_buffer = SRNOS_USBCdcInterfaceLabel(i);" _srnos_composite "${_srnos_composite}")
file(WRITE "${_srnos_usb_build}/usbd_composite.c" "${_srnos_composite}")
file(READ "${_srnos_usb}/Class/CDC_ACM/Src/usbd_cdc_acm.c" _srnos_cdc)
foreach(_anchor IN ITEMS "desc[2] = cmd_itf;" "desc[45] = com_itf;")
  string(FIND "${_srnos_cdc}" "${_anchor}" _position)
  if(_position EQUAL -1)
    message(FATAL_ERROR "USB snapshot patch anchor missing: ${_anchor}")
  endif()
endforeach()
string(REPLACE "desc[2] = cmd_itf;" "desc[2] = cmd_itf;\n    desc[7] = str_idx;" _srnos_cdc "${_srnos_cdc}")
string(REPLACE "desc[45] = com_itf;" "desc[45] = com_itf;\n    desc[51] = str_idx;" _srnos_cdc "${_srnos_cdc}")
file(WRITE "${_srnos_usb_build}/usbd_cdc_acm.c" "${_srnos_cdc}")
set_source_files_properties("${_srnos_usb_build}/usbd_composite.c" PROPERTIES
  COMPILE_OPTIONS "-include;${_srnos_usb_glue}/usb_composite_memcpy.h")
set(SRNOS_SERVICES_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/Clock/clock.c"
  "${CMAKE_CURRENT_LIST_DIR}/Input/input.c"
  "${CMAKE_CURRENT_LIST_DIR}/Input/sirin_rmt.c"
  "${CMAKE_CURRENT_LIST_DIR}/Message/message.c"
  "${CMAKE_CURRENT_LIST_DIR}/Message/message_transport.c"
  "${CMAKE_CURRENT_LIST_DIR}/Storage/storage.c"
  "${CMAKE_CURRENT_LIST_DIR}/Blackbox/blackbox_config.c"
  "${CMAKE_CURRENT_LIST_DIR}/Blackbox/blackbox.c"
  "${CMAKE_CURRENT_LIST_DIR}/Blackbox/blackbox_ring.c"
  "${CMAKE_CURRENT_LIST_DIR}/Telemetry/Telemetry.c"
  "${CMAKE_CURRENT_LIST_DIR}/VideoLink/video_link.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/imu_calibration.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/imu_static_calibration.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/imu_calibration_protocol.c"
  "${CMAKE_CURRENT_LIST_DIR}/SystemMonitor/monitor_core.c"
  "${CMAKE_CURRENT_LIST_DIR}/SystemMonitor/monitor_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/SystemMonitor/monitor_snapshot.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/system_monitor_telemetry.c"
  "${CMAKE_CURRENT_LIST_DIR}/Indicators/indicators.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/ins_core.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/ins_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/ins_snapshot.c"
  "${CMAKE_CURRENT_LIST_DIR}/INS/ins_heater.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/ins_telemetry.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/Log.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/DebugPort.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/Worker/LogWorker.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/logging_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/SirinLink.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/sirin_link_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/imu_telemetry.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/Codec/COBS.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/Codec/Protocol.c"
  "${CMAKE_CURRENT_LIST_DIR}/SirinLink/Codec/Link.c"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/Parameters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/ParameterLock.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/SirinLink/ParameterProtocol.c"
  "${_srnos_usb_glue}/usb_cdc_channels.c"
  "${_srnos_usb_glue}/usb_cdc_log_transport.c"
  "${_srnos_usb_glue}/usbd_cdc_acm_if.c"
  "${_srnos_usb_glue}/usb_boot_replay.c"
  "${_srnos_usb_glue}/usb_service.c"
  "${_srnos_usb_glue}/usbd_desc.c"
  "${_srnos_usb}/Core/Src/usbd_core.c"
  "${_srnos_usb}/Core/Src/usbd_ctlreq.c"
  "${_srnos_usb}/Core/Src/usbd_ioreq.c"
  "${_srnos_usb_build}/usbd_composite.c"
  "${_srnos_usb_build}/usbd_cdc_acm.c")
set(SRNOS_SERVICES_PRIVATE_INCLUDES
  "${CMAKE_CURRENT_LIST_DIR}/INS"
  "${_srnos_usb_glue}"
  "${_srnos_usb}/Core/Inc"
  "${_srnos_usb}/Class/COMPOSITE/Inc"
  "${_srnos_usb}/Class/CDC_ACM/Inc")

include("${CMAKE_CURRENT_LIST_DIR}/Network/component.cmake")
