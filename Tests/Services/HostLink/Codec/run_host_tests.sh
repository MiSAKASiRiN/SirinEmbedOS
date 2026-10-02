#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-HostLink-Codec-tests"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined  -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Source/Services/HostLink" -I"$ROOT/Source/Services/HostLink/Codec" "$ROOT/Source/Services/HostLink/Codec/COBS.c" "$ROOT/Source/Services/HostLink/Codec/Protocol.c" "$ROOT/Source/Core/CRC32.c" "$ROOT/Source/Services/HostLink/Codec/Link.c" "$ROOT/Tests/Services/HostLink/Codec/test_link.c" -o "$OUT/test"
"$OUT/test"
