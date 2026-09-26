#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-Parameters-tests"
mkdir -p "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined  -I"$ROOT/Include" "$ROOT/Source/Services/Parameters/lib_params_binary.cpp" "$ROOT/Tests/Services/Parameters/test_unaligned_binary.cpp" -o "$OUT/test"
"$OUT/test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -pthread \
  -I"$ROOT/Tests/Services/Parameters/Stubs" -I"$ROOT/Include" \
  "$ROOT/Source/Services/Parameters/lib_params.cpp" \
  "$ROOT/Source/Services/Parameters/lib_params_binary.cpp" \
  "$ROOT/Source/Services/Parameters/parameter_lock.cpp" \
  "$ROOT/Tests/Services/Parameters/test_registry.cpp" -o "$OUT/registry"
"$OUT/registry"
