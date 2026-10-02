#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -fsanitize=undefined -I"$root/Tests/Devices/MCP2518FD/stubs" -I"$root/Board/Support" -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source")
cc -std=c11 "${flags[@]}" -c "$root/Source/Devices/MCP2518FD/SPIAdapter.c" -o "$build/driver.o"
cc -std=c11 "${flags[@]}" -c "$root/Tests/Devices/MCP2518FD/test_binding.c" -o "$build/test.o"
c++ -std=c++17 "${flags[@]}" -c "$root/Board/Support/IO/can_board.cpp" -o "$build/board.o"
c++ -fsanitize=undefined "$build/driver.o" "$build/test.o" "$build/board.o" -o "$build/test"
"$build/test"
