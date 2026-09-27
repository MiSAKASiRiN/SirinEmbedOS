#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -O1 \
  -I"$root/Tests/IO/I2C/stubs" -I"$root/Tests/IO/SPI/stubs" -I"$root/Include" -I"$root/Board/Glue" \
  "$root/Tests/IO/I2C/test_i2c.c" -o "$build/test_i2c"
"$build/test_i2c"
