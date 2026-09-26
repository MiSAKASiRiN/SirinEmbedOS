#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/host-serial"
mkdir -p "$BUILD"
cc -std=c11 -Wall -Wextra -Werror -I"$ROOT/Tests/Serial/Fakes" -I"$ROOT/Include" -I"$ROOT/Config" -I"$ROOT/Source/IO/Serial" "$ROOT/Tests/Serial/test_serial.c" "$ROOT/Source/Services/RemoteControl/DR16/bsp_dr16.c" "$ROOT/Source/Services/RemoteControl/VT03/bsp_vt03.c" "$ROOT/Source/Devices/H12H/lib_serial_servo_h12h.c" "$ROOT/Source/Services/Referee/referee.c" "$ROOT/Source/Kernel/system_config.c" -o "$BUILD/test_serial"
"$BUILD/test_serial"
cc -std=c11 -Wall -Wextra -Werror -D__arm__ -I"$ROOT/Tests/Serial/BootFakes" -I"$ROOT/Include" "$ROOT/Tests/Serial/test_remote_boot.c" "$ROOT/Source/Services/RemoteControl/lib_remote_control.c" -o "$BUILD/test_remote_boot"
"$BUILD/test_remote_boot"
c++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/Tests/Serial/Fakes" -I"$ROOT/Include" "$ROOT/Tests/Serial/test_rs485.cpp" -o "$BUILD/test_rs485"
"$BUILD/test_rs485"
