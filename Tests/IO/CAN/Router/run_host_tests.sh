#!/bin/sh
set -eu
TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK_ROOT=$(CDPATH= cd -- "$TEST_ROOT/../../../.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/srnos-can.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$SDK_ROOT/Include" \
 -c "$SDK_ROOT/Source/IO/CAN/can_controller.c" -o "$OUT/controller.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$SDK_ROOT/Include" \
 -DSRNOS_TEST_CAN_REAL_CLASSIC_CORE -c "$SDK_ROOT/Tests/Support/can_empty_registry.c" -o "$OUT/fd_registry.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$SDK_ROOT/Include" \
 -c "$SDK_ROOT/Tests/Support/can_empty_registry.c" -o "$OUT/empty_registry.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/can_router_core_host_test.cpp" "$OUT/controller.o" "$OUT/fd_registry.o" -o "$OUT/test"
"$OUT/test"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -DSRNOS_CAN_HOST_COUNT_CRITICAL \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/test_four_channel_load.cpp" "$OUT/controller.o" "$OUT/fd_registry.o" -o "$OUT/test_four_channel_load"
"$OUT/test_four_channel_load"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/worker_stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$TEST_ROOT/test_native_rx.cpp" "$OUT/controller.o" "$OUT/empty_registry.o" -o "$OUT/test_rx"
"$OUT/test_rx"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/worker_stubs" -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/test_external_tx.cpp" "$OUT/controller.o" "$OUT/fd_registry.o" -o "$OUT/test_external_tx"
"$OUT/test_external_tx"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 "$SDK_ROOT/Source/IO/CAN/Router/can_router_core.cpp" "$TEST_ROOT/test_tx_step.cpp" "$OUT/controller.o" "$OUT/fd_registry.o" -o "$OUT/test_step"
"$OUT/test_step"
