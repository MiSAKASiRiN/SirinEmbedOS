#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out="${TMPDIR:-/tmp}/srnos-power-host-tests"
mkdir -p "$out"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -I"$root/Include" \
    "$root/Source/Algorithms/Power/lib_power_control.cpp" \
    "$root/Tests/Algorithms/Power/test_power.cpp" -o "$out/test_power"
"$out/test_power"
echo "Power model host tests passed"
