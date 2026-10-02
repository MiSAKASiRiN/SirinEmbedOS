#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -g -O1 \
  -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source" -I"$root/Source/Devices/Sensors" \
  "$root/Source/Devices/Sensors/sensor_codec.c" "$root/Source/Devices/Sensors/sensor_registers.c" \
  "$root/Tests/Devices/Sensors/test_sensors.c" -lm -o "$build/test_sensors"
"$build/test_sensors"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -g -O1 \
  -I"$root/Tests/Devices/Sensors/stubs" -I"$root/Tests/IO/SPI/stubs" -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source" -I"$root/Board/Support" \
  "$root/Source/Devices/Sensors/sensor_codec.c" "$root/Source/Devices/Sensors/sensor_registers.c" \
  "$root/Tests/Devices/Sensors/test_workers.c" -lm -o "$build/test_workers"
"$build/test_workers"

