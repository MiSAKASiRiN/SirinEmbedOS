#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-Parameters-HostLink-tests"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined  -I"$ROOT/Include" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs_prot.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_link.c" "$ROOT/Source/Services/Parameters/HostLink/tskptt_param_console.c" "$ROOT/Tests/Services/Parameters/HostLink/mock_lib_params.c" "$ROOT/Tests/Services/Parameters/HostLink/test_param_service.c" -o "$OUT/test"
"$OUT/test"
