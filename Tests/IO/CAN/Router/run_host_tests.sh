#!/bin/sh
set -eu
TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK_ROOT=$(CDPATH= cd -- "$TEST_ROOT/../../../.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/srnos-can.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/can_router_core_host_test.cpp" -o "$OUT/test"
"$OUT/test"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/worker_stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$TEST_ROOT/test_native_rx.cpp" -o "$OUT/test_rx"
"$OUT/test_rx"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/worker_stubs" -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/test_external_tx.cpp" -o "$OUT/test_external_tx"
"$OUT/test_external_tx"
