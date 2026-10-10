#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/srnos-canfd.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
COMMON=(-Wall -Wextra -Werror -O1 -g -fsanitize=undefined
 -I"$ROOT/Tests/IO/CAN/FD/stubs" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Source/IO/CAN/Router")
"${CC:-cc}" -std=c11 "${COMMON[@]}" -c "$ROOT/Source/IO/CAN/can_controller.c" -o "$OUT/controller.o"
"${CC:-cc}" -std=c11 "${COMMON[@]}" -c "$ROOT/Source/Kernel/system_config.c" -o "$OUT/system.o"
"${CC:-cc}" -std=c11 "${COMMON[@]}" -c "$ROOT/Source/Kernel/thread_config.c" -o "$OUT/thread_config.o"
"${CXX:-c++}" -std=c++17 "${COMMON[@]}" "$ROOT/Source/IO/CAN/FD/can_fd_core.cpp" \
 "$ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$ROOT/Tests/IO/CAN/FD/test_fd_core.cpp" \
 "$OUT/controller.o" "$OUT/system.o" "$OUT/thread_config.o" -o "$OUT/test_fd"
for scenario in registration lengths invalid queue expiry stress system fd_on_classic classic_on_fd queued_classic adapter_bitrate; do
 "$OUT/test_fd" "$scenario"
done
