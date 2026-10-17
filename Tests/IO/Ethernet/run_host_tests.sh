#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -O1 \
  -I"$root/Tests/IO/Ethernet/stubs" -I"$root/Include" -I"$root/Source" -I"$root/Board/Support" \
  "$root/Tests/IO/Ethernet/test_ethernet.c" "$root/Source/IO/Ethernet/ethernet_config.c" -o "$build/test_ethernet"
for scenario in disabled recovery queue timeout disconnect expiry dma_error wrap; do
  "$build/test_ethernet" "$scenario"
done
