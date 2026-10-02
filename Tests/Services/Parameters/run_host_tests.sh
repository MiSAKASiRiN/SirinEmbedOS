#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-Parameters-tests"
mkdir -p "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined  -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" "$ROOT/Source/Services/Parameters/ParameterBinary.cpp" "$ROOT/Tests/Services/Parameters/test_unaligned_binary.cpp" -o "$OUT/test"
"$OUT/test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -pthread \
  -I"$ROOT/Tests/Services/Parameters/Stubs" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
  "$ROOT/Source/Services/Parameters/Parameters.cpp" \
  "$ROOT/Source/Services/Parameters/ParameterBinary.cpp" \
  "$ROOT/Source/Services/Parameters/ParameterLock.cpp" \
  "$ROOT/Tests/Services/Parameters/test_registry.cpp" -o "$OUT/registry"
"$OUT/registry"

"$OUT/registry" definitions
"$OUT/registry" capacity
"$OUT/registry" empty

"$OUT/registry" zero
"$OUT/registry" null
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined \
  -I"$ROOT/Tests/Services/Parameters/Stubs" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
  "$ROOT/Source/Services/Parameters/Parameters.cpp" \
  "$ROOT/Source/Services/Parameters/ParameterBinary.cpp" \
  "$ROOT/Tests/Services/Parameters/test_context.cpp" -o "$OUT/context"
"$OUT/context"
