#!/usr/bin/env sh
set -eu

TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK_ROOT=$(CDPATH= cd -- "$TEST_ROOT/../../.." && pwd)
IMU_ROOT="$SDK_ROOT/Source/Devices/IMU"
TEST_BUILD_DIR="${TMPDIR:-/tmp}/lib-imu-attitude-tests"
mkdir -p "$TEST_BUILD_DIR"

CC_BIN=${CC:-cc}
CXX_BIN=${CXX:-c++}
SANITIZERS="-fsanitize=undefined"
if [ "$(uname -s)" != "Darwin" ]; then
  SANITIZERS="-fsanitize=address,undefined"
fi

"$CC_BIN" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  $SANITIZERS -D__GNUC_PYTHON__ \
  -isystem "$SDK_ROOT/ThirdParty/CMSISDSP/Include" \
  -c "$SDK_ROOT/Source/Algorithms/Math/cmsis_matrix_backend.c" \
  -o "$TEST_BUILD_DIR/cmsis_matrix_backend.o"

"$CXX_BIN" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  $SANITIZERS -fno-omit-frame-pointer \
  -D__GNUC_PYTHON__ -isystem "$SDK_ROOT/ThirdParty/CMSISDSP/Include" \
  -I"$SDK_ROOT/Include" \
  "$SDK_ROOT/Source/Algorithms/Control/pid.cpp" \
  "$IMU_ROOT/quaternion_ekf.cpp" \
  "$TEST_ROOT/test_imu_components.cpp" \
  "$TEST_BUILD_DIR/cmsis_matrix_backend.o" \
  -o "$TEST_BUILD_DIR/test_imu_components"

ASAN_OPTIONS=detect_leaks=0 "$TEST_BUILD_DIR/test_imu_components"

echo "Independent quaternion EKF component tests passed"
