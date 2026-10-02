#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Wenum-conversion -O1 -g -fsanitize=undefined \
 -I"$ROOT/Tests/Services/Logging/stubs" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Source/Services/USB" \
 "$ROOT/Source/Services/Logging/Log.c" "$ROOT/Tests/Services/Logging/test_logging.c" -o "$OUT/test"
"$OUT/test"
