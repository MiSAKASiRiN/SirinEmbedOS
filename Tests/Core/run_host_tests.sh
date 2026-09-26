#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out="${TMPDIR:-/tmp}/sirinos-host-tests"
mkdir -p "$out"

cc_bin=${CC:-cc}
cxx_bin=${CXX:-c++}
sanitizer_set=${SIRINOS_SANITIZERS:-address,undefined}
if [ "$(uname -s)" = "Darwin" ] && [ "${SIRINOS_SANITIZERS+x}" != "x" ]; then
    case "$(uname -r)" in
        25.*)
            echo "SirinOS: Apple ASan startup deadlocks on this Darwin 25 host; running UBSan."
            echo "Set SIRINOS_SANITIZERS=address,undefined to force the full suite."
            sanitizer_set=undefined
            ;;
    esac
fi
sanitizers="-fsanitize=$sanitizer_set"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" "$root/Tests/Core/test_core.c" -o "$out/test_core_c"
"$out/test_core_c"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" -c "$root/Source/Core/Fault/Fault.c" -o "$out/fault.o"
"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" -c "$root/Tests/Core/platform_stubs.c" -o "$out/platform_stubs.o"

"$cxx_bin" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" "$root/Tests/Core/test_core.cpp" "$out/fault.o" \
    "$out/platform_stubs.o" -o "$out/test_core_cpp"
"$out/test_core_cpp"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" "$root/Source/Core/Fault/Fault.c" "$root/Tests/Core/test_fault.c" \
    -o "$out/test_fault"
"$out/test_fault"

"$cxx_bin" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    -I"$root/Include" "$root/Tests/Core/test_span_fault.cpp" "$out/fault.o" \
    -o "$out/test_span_fault"
"$out/test_span_fault"

PYTHONDONTWRITEBYTECODE=1 python3 "$root/Tests/Core/test_decode_panic.py"

if "$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$root/Include" -c "$root/Tests/Core/pointer_must_fail.c" \
    -o "$out/pointer_must_fail.o" 2>"$out/pointer_must_fail.log"; then
    echo "SRNOS_ARRAY_COUNT unexpectedly accepted a pointer" >&2
    exit 1
fi

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizers \
    "$root/Tests/Core/test_libc_arena.c" -o "$out/test_libc_arena"
"$out/test_libc_arena"

echo "SirinOS host tests passed"
