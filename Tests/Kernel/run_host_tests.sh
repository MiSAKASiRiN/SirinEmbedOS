#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/host-kernel"
mkdir -p "$BUILD"
cc -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT/Source/Kernel" -I"$ROOT/ThirdParty/FreeRTOS/CMSIS/Include" \
    "$ROOT/Tests/Kernel/test_startup_barrier.c" -o "$BUILD/test_startup_barrier"
"$BUILD/test_startup_barrier"
COMMON=(-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-misleading-indentation -O1 -g -fsanitize=undefined -pthread
 -I"$ROOT/Tests/Kernel/fakes" -I"$ROOT/Include" -I"$ROOT/Source" -I"$ROOT/Config" -I"$ROOT/Source/Kernel" -I"$ROOT/Source/Diagnostics/Runtime")
cc "${COMMON[@]}" "$ROOT/Tests/Kernel/fake_runtime.c" "$ROOT/Tests/Kernel/test_managed.c" \
 "$ROOT/Source/Kernel/thread.c" "$ROOT/Source/Kernel/thread_config.c" "$ROOT/Source/Kernel/object_pool.c" "$ROOT/Source/Kernel/sync.c" \
 "$ROOT/Source/Diagnostics/Runtime/cycle_statistics.c" -o "$BUILD/test_managed"
"$BUILD/test_managed"
cc "${COMMON[@]}" "$ROOT/Tests/Kernel/fake_runtime.c" "$ROOT/Tests/Kernel/test_cycles.c" \
 "$ROOT/Source/Diagnostics/Runtime/cycle_statistics.c" -o "$BUILD/test_cycles"
"$BUILD/test_cycles"
