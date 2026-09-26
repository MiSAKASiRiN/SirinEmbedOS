#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/host-kernel"
mkdir -p "$BUILD"
cc -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT/Source/Kernel" -I"$ROOT/ThirdParty/FreeRTOS/CMSIS/Include" \
    "$ROOT/Tests/Kernel/test_startup_barrier.c" -o "$BUILD/test_startup_barrier"
"$BUILD/test_startup_barrier"
