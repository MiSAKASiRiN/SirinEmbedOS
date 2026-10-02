#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-usb-tests"
mkdir -p "$OUT"
for PAIR in "usb_cdc_log_transport test_log_transport" "usbd_cdc_acm_if test_cdc_dispatch"; do
  set -- $PAIR
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$ROOT/Tests/Services/USB/stubs" -I"$ROOT/Source/Services/USB" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
    "$ROOT/Source/Services/USB/$1.c" "$ROOT/Tests/Services/USB/$2.c" -o "$OUT/$2"
  "$OUT/$2"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Services/USB/transport_stubs" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
 "$ROOT/Source/Services/Logging/DebugPort.c" "$ROOT/Tests/Services/USB/test_debug_port.c" -o "$OUT/test_debug_port"
"$OUT/test_debug_port"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$ROOT/Tests/Services/USB/startup_stubs" -I"$ROOT/Board/Support" \
 "$ROOT/Source/Services/USB/usb_service.c" "$ROOT/Board/Support/USB/usb_board.c" \
 "$ROOT/Tests/Services/USB/test_startup.c" -o "$OUT/test_startup"
"$OUT/test_startup"
