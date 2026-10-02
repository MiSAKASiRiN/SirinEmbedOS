#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out="${TMPDIR:-/tmp}/lib-controls-host-tests"
mkdir -p "$out"

cxx_bin=${CXX:-c++}
"$cxx_bin" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined \
    -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source" \
    "$root/Source/Algorithms/Control/pid.cpp" "$root/Source/Algorithms/Control/control_extensions.cpp" \
    "$root/Tests/Algorithms/Control/test_pid.cpp" -o "$out/test_pid"
"$out/test_pid"

echo "lib_controls host tests passed"
