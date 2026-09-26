# Explicit service sources: no source-tree scan and no prototype dependencies.
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
string(REPLACE "#include \"usbd_ctlreq.h\"" "#include \"usbd_ctlreq.h\"\n#include \"omx_usb_cdc_channels.h\"" _srnos_composite "${_srnos_composite}")
string(REPLACE "char str_buffer[16] = \"\";" "const char *str_buffer = NULL;" _srnos_composite "${_srnos_composite}")
string(REPLACE "snprintf(str_buffer, sizeof(str_buffer), CDC_ACM_STR_DESC, i);" "str_buffer = omx_usb_cdc_interface_label(i);" _srnos_composite "${_srnos_composite}")
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
  COMPILE_OPTIONS "-include;${_srnos_usb_glue}/omx_usb_composite_memcpy.h")
set(SRNOS_SERVICES_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/Logging/bsp_srn_log.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/bsp_srn_debug_port.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/Worker/tskptt_log.c"
  "${CMAKE_CURRENT_LIST_DIR}/Logging/logging_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/HostLink/tskptt_host_comm.c"
  "${CMAKE_CURRENT_LIST_DIR}/HostLink/host_link_service.c"
  "${CMAKE_CURRENT_LIST_DIR}/HostLink/Codec/lib_srn_cobs.c"
  "${CMAKE_CURRENT_LIST_DIR}/HostLink/Codec/lib_srn_cobs_prot.c"
  "${CMAKE_CURRENT_LIST_DIR}/HostLink/Codec/lib_srn_link.c"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/lib_params.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/lib_params_binary.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/parameter_lock.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/Parameters/HostLink/tskptt_param_console.c"
  "${_srnos_usb_glue}/omx_usb_cdc_channels.c"
  "${_srnos_usb_glue}/omx_usb_cdc_log_transport.c"
  "${_srnos_usb_glue}/omx_usbd_cdc_acm_if.c"
  "${_srnos_usb_glue}/usb_boot_replay.c"
  "${_srnos_usb_glue}/usb_service.c"
  "${_srnos_usb_glue}/usbd_desc.c"
  "${_srnos_services_root}/Board/Glue/usb_device_board.c"
  "${_srnos_usb}/Core/Src/usbd_core.c"
  "${_srnos_usb}/Core/Src/usbd_ctlreq.c"
  "${_srnos_usb}/Core/Src/usbd_ioreq.c"
  "${_srnos_usb_build}/usbd_composite.c"
  "${_srnos_usb_build}/usbd_cdc_acm.c")
set(SRNOS_SERVICES_PRIVATE_INCLUDES
  "${_srnos_usb_glue}"
  "${_srnos_usb}/Core/Inc"
  "${_srnos_usb}/Class/COMPOSITE/Inc"
  "${_srnos_usb}/Class/CDC_ACM/Inc")
