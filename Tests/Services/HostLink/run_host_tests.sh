#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-HostLink-tests"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -DTSKPTT_HOST_COMM_TESTING -I"$ROOT/Tests/Services/HostLink/Fakes" -I"$ROOT/Include" "$ROOT/Source/Core/Fault/Fault.c" "$ROOT/Tests/Core/platform_stubs.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_cobs_prot.c" "$ROOT/Source/Services/HostLink/Codec/lib_srn_link.c" "$ROOT/Source/Services/HostLink/tskptt_host_comm.c" "$ROOT/Tests/Services/HostLink/test_host_comm.c" -o "$OUT/test"
"$OUT/test"
