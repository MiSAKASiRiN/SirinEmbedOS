#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -O1 \
  -I"$root/Tests/Devices/EEPROM/stubs" -I"$root/Include" \
  "$root/Tests/Devices/EEPROM/test_eeprom.c" -o "$build/test_eeprom"
"$build/test_eeprom"
