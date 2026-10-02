#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined \
 -I"$ROOT/Include" -I"$ROOT/Tests/Services/HostLink/Fakes" \
 -c "$ROOT/Source/Services/Telemetry/Telemetry.c" -o "$OUT/telemetry.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined \
 -I"$ROOT/Include" "$ROOT/Tests/Services/Telemetry/test_telemetry.cpp" "$OUT/telemetry.o" -o "$OUT/test"
"$OUT/test"
