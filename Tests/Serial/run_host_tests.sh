#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/host-serial"
mkdir -p "$BUILD"
cc -std=c11 -Wall -Wextra -Werror -I"$ROOT/Tests/Serial/Fakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Config" -I"$ROOT/Source/IO/Serial" "$ROOT/Tests/Serial/test_serial.c" "$ROOT/Source/Services/RemoteControl/DR16/DR16.c" "$ROOT/Source/Services/RemoteControl/VT03/VT03.c" "$ROOT/Source/Devices/H12H/H12H.c" "$ROOT/Source/Services/Referee/referee.c" "$ROOT/Source/Kernel/system_config.c" "$ROOT/Source/Kernel/thread_config.c" -o "$BUILD/test_serial"
"$BUILD/test_serial"
cc -std=c11 -Wall -Wextra -Werror -D__arm__ -I"$ROOT/Tests/Serial/BootFakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" "$ROOT/Tests/Serial/test_remote_boot.c" "$ROOT/Source/Services/RemoteControl/RemoteControl.c" -o "$BUILD/test_remote_boot"
"$BUILD/test_remote_boot"
c++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/Tests/Serial/Fakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" "$ROOT/Tests/Serial/test_rs485.cpp" -o "$BUILD/test_rs485"
"$BUILD/test_rs485"
