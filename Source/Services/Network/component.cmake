set(_srnos_lwip "${SIRINEMBEDOS_ROOT}/ThirdParty/LwIP/src")
list(APPEND SRNOS_SERVICES_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/network.c"
  "${CMAKE_CURRENT_LIST_DIR}/network_config.c"
  "${CMAKE_CURRENT_LIST_DIR}/network_ntp.c"
  "${_srnos_lwip}/core/init.c"
  "${_srnos_lwip}/core/def.c"
  "${_srnos_lwip}/core/inet_chksum.c"
  "${_srnos_lwip}/core/ip.c"
  "${_srnos_lwip}/core/mem.c"
  "${_srnos_lwip}/core/memp.c"
  "${_srnos_lwip}/core/netif.c"
  "${_srnos_lwip}/core/pbuf.c"
  "${_srnos_lwip}/core/sys.c"
  "${_srnos_lwip}/core/timeouts.c"
  "${_srnos_lwip}/core/udp.c"
  "${_srnos_lwip}/core/ipv4/etharp.c"
  "${_srnos_lwip}/core/ipv4/icmp.c"
  "${_srnos_lwip}/core/ipv4/ip4.c"
  "${_srnos_lwip}/core/ipv4/ip4_addr.c"
  "${_srnos_lwip}/netif/ethernet.c"
)
list(APPEND SRNOS_SERVICES_PRIVATE_INCLUDES
  "${CMAKE_CURRENT_LIST_DIR}" "${_srnos_lwip}/include")
