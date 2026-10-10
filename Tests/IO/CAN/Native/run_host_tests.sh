#!/bin/sh
set -eu
TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK_ROOT=$(CDPATH= cd -- "$TEST_ROOT/../../../.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/srnos-native-can.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
python3 "$TEST_ROOT/make_hal_dispatch.py" "$SDK_ROOT/Board/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_fdcan.c" "$OUT/hal_dispatch.c"
for UNIT in fdcan board controller registry hal dispatch; do
 case "$UNIT" in
 fdcan) SRC="$SDK_ROOT/Source/IO/CAN/Native/FDCAN.c";;
 board) SRC="$SDK_ROOT/Board/Support/IO/can_native_board.c";;
 controller) SRC="$SDK_ROOT/Source/IO/CAN/can_controller.c";;
 registry) SRC="$TEST_ROOT/controller_registration_stubs.c";;
 hal) SRC="$TEST_ROOT/fake_hal.c";;
 dispatch) SRC="$OUT/hal_dispatch.c";;
 esac
 "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source" \
  -c "$SRC" -o "$OUT/$UNIT.o"
done
for TEST in test_native test_configuration; do
 "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source" \
  "$TEST_ROOT/$TEST.c" "$OUT/"*.o -o "$OUT/$TEST"
done
"$OUT/test_native"
python3 - "$OUT/test_configuration" <<'PY'
import subprocess, sys
binary=sys.argv[1]
for mask in range(8):
    subprocess.run([binary,'matrix',str(mask)],check=True)
for nominal in (125000,250000,500000,1000000):
    subprocess.run([binary,'rate','0',str(nominal),'0','0'],check=True)
    subprocess.run([binary,'rate','7',str(nominal),str(nominal),'0'],check=True)
    for data in (500000,1000000,2000000,4000000,5000000,8000000):
        if data>=nominal:
            subprocess.run([binary,'rate','7',str(nominal),str(data),'1'],check=True)
for failure in ('clock','ccu','unfrozen','init','tdc','filter'):
    subprocess.run([binary,'failure',failure],check=True)
print('native FD: real board timing/RAM, 8 mode combinations, all rate profiles/DLCs, init failure latch and recovery preservation PASS')
PY
for TEST in test_notifications test_fd_notifications; do
 "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$TEST_ROOT/stubs" -I"$SDK_ROOT/Include" -I"$SDK_ROOT/Source" \
  -I"$SDK_ROOT/Tests/IO/CAN/Router/worker_stubs" \
  "$TEST_ROOT/$TEST.cpp" "$OUT/"*.o -o "$OUT/$TEST"
 "$OUT/$TEST"
done
