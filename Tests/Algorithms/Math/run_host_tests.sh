#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out="${TMPDIR:-/tmp}/lib-algos-host-tests"
mkdir -p "$out"

cxx_bin=${CXX:-c++}
cc_bin=${CC:-cc}
# Use the pinned CMSIS header's host branch, not inline test implementations.
# Compiling the actual C backend also verifies the public C linkage contract.
"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -D__GNUC_PYTHON__ \
    -isystem "$root/ThirdParty/CMSISDSP/Include" \
    -c "$root/Source/Algorithms/Math/cmsis_matrix_backend.c" \
    -o "$out/cmsis_matrix_backend.o"
"$cxx_bin" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -D__GNUC_PYTHON__ -I"$root/Include" \
    -isystem "$root/ThirdParty/CMSISDSP/Include" \
    "$root/Tests/Algorithms/Math/test_algos.cpp" "$out/cmsis_matrix_backend.o" \
    -o "$out/test_algos"
"$out/test_algos"

echo "lib_algos host tests passed"
