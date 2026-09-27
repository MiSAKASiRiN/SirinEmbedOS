#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -O1 -g -fsanitize=undefined -I"$root/Include" -I"$root/Source/Services/INS" -I"$root/Board/Glue")
for source in Source/Services/INS/ins_core.c Source/Devices/IMU/mahony_filter.c Board/Glue/sensor_frames.c; do
  cc -std=c11 "${flags[@]}" -c "$root/$source" -o "$build/$(basename "$source").o"
done
c++ -std=c++17 "${flags[@]}" "$root/Tests/Services/INS/test_ins.cpp" \
  "$root/Source/Services/INS/ins_heater.cpp" "$root/Source/Algorithms/Control/pid.cpp" "$build/"*.o -o "$build/test_ins"
"$build/test_ins"
cc -std=c11 "${flags[@]}" -pthread -I"$root/Tests/Services/INS/stubs" \
 -I"$root/Tests/Services/IMUTelemetry/stubs" \
 "$root/Source/Services/INS/ins_snapshot.c" "$root/Tests/Services/INS/test_snapshot.c" -o "$build/test_snapshot"
"$build/test_snapshot"
