#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS=halt_on_error=1
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
lwip="$root/ThirdParty/LwIP/src"
sources=()
for name in init def inet_chksum ip mem memp netif pbuf sys timeouts udp; do sources+=("$lwip/core/$name.c"); done
for name in etharp icmp ip4 ip4_addr; do sources+=("$lwip/core/ipv4/$name.c"); done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -O1 \
  -I"$root/Tests/IO/Ethernet/stubs" -I"$root/Include" -I"$root/Source" \
  -I"$root/Source/Services/Network" -I"$lwip/include" \
  "$root/Tests/Services/Network/test_network.c" "$root/Source/Services/Network/network_config.c" \
  "${sources[@]}" "$lwip/netif/ethernet.c" -o "$build/test_network"
for scenario in default custom disabled ping_only policy; do "$build/test_network" "$scenario"; done
