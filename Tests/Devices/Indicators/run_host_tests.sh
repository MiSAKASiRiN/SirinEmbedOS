#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Devices/Indicators/stubs" "$ROOT/Tests/Devices/Indicators/test_indicators.c" -o "$OUT/test"
"$OUT/test"
