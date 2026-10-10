#!/bin/sh
set -eu
TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK_ROOT=$(CDPATH= cd -- "$TEST_ROOT/../../../.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/srnos-external-can.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
case $(uname -s) in Darwin) GC_FLAG=-Wl,-dead_strip ;; *) GC_FLAG=-Wl,--gc-sections ;; esac
# Uncalled hardware-initialization/vendor paths are excluded, while the actual
# device ISR/notification and RX worker helpers are compiled without copies.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=undefined \
 -ffunction-sections -fdata-sections "$GC_FLAG" \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Tests/Support" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Board/Support" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 -I"$SDK_ROOT/Tests/IO/CAN/Router/worker_stubs" \
 "$TEST_ROOT/test_external.cpp" -o "$OUT/test_external"
"$OUT/test_external"

# Retain the supplier implementation unchanged; its legacy unused parameters
# are outside the SDK's warning-clean adapter contract.
"${CC:-cc}" -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable -fsanitize=undefined \
 -ffunction-sections -fdata-sections \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Tests/Support" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Board/Support" \
 -c "$SDK_ROOT/Source/Devices/MCP2518FD/drv_canfdspi_api.c" -o "$OUT/vendor.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Tests/Support" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Board/Support" \
 -c "$SDK_ROOT/Source/Devices/MCP2518FD/fifo.c" -o "$OUT/fifo.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=undefined \
 -ffunction-sections -fdata-sections "$GC_FLAG" \
 -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Tests/Support" -I"$SDK_ROOT/Include" \
 -I"$SDK_ROOT/Source" -I"$SDK_ROOT/Board/Support" -I"$SDK_ROOT/Source/IO/CAN/Router" \
 -I"$SDK_ROOT/Tests/IO/CAN/Router/worker_stubs" \
 "$TEST_ROOT/test_external_fd.cpp" "$OUT/vendor.o" "$OUT/fifo.o" -o "$OUT/test_external_fd"
"$OUT/test_external_fd"
