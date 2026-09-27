#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -g -O1 \
  -I"$root/Tests/IO/SPI/stubs" -I"$root/Include" -I"$root/Board/Glue" \
  "$root/Tests/IO/SPI/test_spi.c" -o "$build/test_spi"
"$build/test_spi"
