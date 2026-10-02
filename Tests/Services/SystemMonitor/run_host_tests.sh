#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Services/SystemMonitor/stubs" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Board/Support" \
 "$ROOT/Tests/Services/SystemMonitor/test_monitor.c" "$ROOT/Source/Services/SystemMonitor/monitor_core.c" -o "$OUT/test"
"$OUT/test" "$OUT"
python3 "$ROOT/Tests/Services/SystemMonitor/test_cli.py" "$OUT"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Services/SystemMonitor/stubs" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Board/Support" \
 "$ROOT/Tests/Services/SystemMonitor/test_adc_board.c" -o "$OUT/test_board"
"$OUT/test_board"
