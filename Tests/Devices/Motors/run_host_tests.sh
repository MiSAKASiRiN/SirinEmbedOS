#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/srnos-motor-tests"
mkdir -p "$BUILD"
CXX_BIN="${CXX:-c++}"
SANITIZERS="${SRNOS_MOTOR_SANITIZERS:-undefined}"
SOURCES=()
for model in C610 C620 GM6020 DM4310 DM4340 LK8016E; do
    SOURCES+=("$ROOT/Source/Devices/Motors/$model/$model.cpp")
done
"$CXX_BIN" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize="$SANITIZERS" -fno-omit-frame-pointer -pthread \
    -I"$ROOT/Tests/Devices/Motors/stubs" -I"$ROOT/Include" \
    -I"$ROOT/Source/Devices/Motors" \
    "$ROOT/Tests/Devices/Motors/fake_mask.cpp" \
    "$ROOT/Tests/Devices/Motors/test_feedback.cpp" "${SOURCES[@]}" \
    -o "$BUILD/test_feedback"
"$BUILD/test_feedback"
