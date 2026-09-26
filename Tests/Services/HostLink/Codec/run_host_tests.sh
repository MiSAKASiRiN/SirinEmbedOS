#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-HostLink-Codec-tests"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined  -I"$ROOT/Include" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs_prot.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_link.c" "$ROOT/Tests/Services/HostLink/Codec/test_srn_link.c" -o "$OUT/test"
"$OUT/test"
