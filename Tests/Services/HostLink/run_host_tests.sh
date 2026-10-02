#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="${TMPDIR:-/tmp}/srnos-HostLink-tests"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -DSRNOS_HOST_LINK_TESTING -I"$ROOT/Tests/Services/HostLink/Fakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Source/Services/HostLink" -I"$ROOT/Source/Services/HostLink/Codec" "$ROOT/Source/Core/Fault/Fault.c" "$ROOT/Tests/Core/platform_stubs.c" "$ROOT/Source/Services/HostLink/Codec/COBS.c" "$ROOT/Source/Services/HostLink/Codec/Protocol.c" "$ROOT/Source/Core/CRC32.c" "$ROOT/Source/Services/HostLink/Codec/Link.c" "$ROOT/Source/Services/Telemetry/Telemetry.c" "$ROOT/Source/Services/HostLink/HostLink.c" "$ROOT/Tests/Services/HostLink/test_host_comm.c" -o "$OUT/test"
"$OUT/test"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined \
 -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source/Services/HostLink" \
 "$ROOT/Source/Core/Fault/Fault.c" "$ROOT/Tests/Core/platform_stubs.c" \
 "$ROOT/Source/Services/HostLink/imu_calibration_protocol.c" \
 "$ROOT/Tests/Services/HostLink/test_imu_calibration_protocol.c" -o "$OUT/test_imu_protocol"
"$OUT/test_imu_protocol"
