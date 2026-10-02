#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -I"$ROOT/Tests/Services/Indicators/stubs" -I"$ROOT/Source" -I"$ROOT/Tests/Support" -I"$ROOT/Board/Support" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Services/Indicators/stubs" "$ROOT/Tests/Services/Indicators/test_indicators.c" -o "$OUT/test"
"$OUT/test"
