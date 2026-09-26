#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-usb-tests"
mkdir -p "$OUT"
for PAIR in "omx_usb_cdc_log_transport test_log_transport" "omx_usbd_cdc_acm_if test_cdc_dispatch"; do
  set -- $PAIR
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$ROOT/Tests/Services/USB/stubs" -I"$ROOT/Source/Services/USB" -I"$ROOT/Include" \
    "$ROOT/Source/Services/USB/$1.c" "$ROOT/Tests/Services/USB/$2.c" -o "$OUT/$2"
  "$OUT/$2"
done
