#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/srnos-turntable.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -fno-omit-frame-pointer -pthread \
    -I"$ROOT/Tests/Devices/Motors/stubs" -I"$ROOT/Include" \
    "$ROOT/Tests/Devices/Motors/fake_mask.cpp" \
    "$ROOT/Source/Devices/Motors/GM6020/GM6020.cpp" \
    "$ROOT/Examples/IMUTurntableBringUp/app_main.cpp" \
    "$ROOT/Source/Algorithms/Control/pid.cpp" \
    "$ROOT/Tests/Examples/IMUTurntableBringUp/test_observation.cpp" \
    -o "$BUILD/test_observation"
"$BUILD/test_observation"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=undefined -I"$ROOT/Include" \
    "$ROOT/Tests/Examples/IMUTurntableBringUp/test_control.cpp" \
    "$ROOT/Source/Algorithms/Control/pid.cpp" -o "$BUILD/test_control"
"$BUILD/test_control" "$@"
