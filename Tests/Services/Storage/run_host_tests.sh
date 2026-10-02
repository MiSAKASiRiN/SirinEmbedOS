#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -O1 \
  -I"$root/Tests/Devices/EEPROM/stubs" -I"$root/Tests/Support" -I"$root/Include" -I"$root/Source" \
  "$root/Tests/Services/Storage/test_storage.c" "$root/Source/Core/CRC32.c" -o "$build/test_storage"
"$build/test_storage"
