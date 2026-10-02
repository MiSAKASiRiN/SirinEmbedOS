#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out="${TMPDIR:-/tmp}/lib-algos-host-tests"
mkdir -p "$out"

cxx_bin=${CXX:-c++}
cc_bin=${CC:-cc}
# Compile the actual SDK matrix implementation.
# Compiling the actual C backend also verifies the public C linkage contract.
"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined \
    -I"$root/Include" -c "$root/Source/Algorithms/Math/Matrix.c" \
    -o "$out/Matrix.o"
"$cxx_bin" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source" \
    "$root/Tests/Algorithms/Math/test_algos.cpp" "$out/Matrix.o" \
    -o "$out/test_algos"
"$out/test_algos"

echo "lib_algos host tests passed"
