#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
 -I"$ROOT/Tests/Services/IMUTelemetry/stubs" -I"$ROOT/Tests/Services/HostLink/Fakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
 "$ROOT/Tests/Services/IMUTelemetry/test_telemetry.c" -o "$OUT/test"
"$OUT/test" "$ROOT/Tests/Services/IMUTelemetry/golden_record.hex"
python3 "$ROOT/Tests/Services/IMUTelemetry/test_cli.py"
python3 "$ROOT/Tests/Services/IMUTelemetry/test_capture.py"

cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
 -I"$ROOT/Tests/Services/IMUTelemetry/stubs" -I"$ROOT/Tests/Serial/Fakes" -I"$ROOT/Tests/Services/HostLink/Fakes" -I"$ROOT/Tests/Support" -I"$ROOT/Include" -I"$ROOT/Source" \
 "$ROOT/Tests/Services/IMUTelemetry/test_ins_telemetry.c" -o "$OUT/test_ins"
"$OUT/test_ins" "$ROOT/Tests/Services/IMUTelemetry"
python3 "$ROOT/Tests/Services/IMUTelemetry/test_ins_cli.py"
