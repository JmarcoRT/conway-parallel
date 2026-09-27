#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-input-suite.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/input_tests.cpp" "$root/src/input/input.cpp" "$root/src/core/game.cpp" \
    -o "$work/input_tests"
"$work/input_tests" "$root" "$@"

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/src/main.cpp" "$root/src/input/input.cpp" "$root/src/core/game.cpp" \
    -o "$work/conway"
mkdir -p "$work/app/patterns"
# main currently uses a fixed relative path. Only the isolated copy is replaced.
cp "$root/patterns/block.txt" "$work/app/patterns/test.txt"
(cd "$work/app" && "$work/conway") > "$work/actual.txt" 2> "$work/stderr.txt"
cat > "$work/expected.txt" <<'EXPECTED'
Generacion 0:
0000
0110
0110
0000

Generacion 1:
0000
0110
0110
0000

Generacion 2:
0000
0110
0110
0000
EXPECTED
diff -u "$work/expected.txt" "$work/actual.txt"
test ! -s "$work/stderr.txt"
echo 'PASS executable_valid_block (exit 0, exact output, empty stderr)'

check_app_error() {
    local expected=$1 status=0
    (cd "$work/app" && "$work/conway") > "$work/actual.txt" 2> "$work/stderr.txt" || status=$?
    test "$status" -eq 1
    test ! -s "$work/actual.txt"
    printf '%s\n' "$expected" > "$work/expected.txt"
    diff -u "$work/expected.txt" "$work/stderr.txt"
}
cp "$root/tests/fixtures/input/invalid/invalid_character.txt" "$work/app/patterns/test.txt"
check_app_error 'Error: El tablero solo puede contener 0 y 1.'
echo 'PASS executable_invalid_file (exit 1, exact diagnostic, empty stdout)'
mv "$work/app/patterns/test.txt" "$work/app/patterns/unused.txt"
check_app_error 'Error: No se pudo abrir el archivo: patterns/test.txt'
echo 'PASS executable_missing_file (exit 1, exact diagnostic, empty stdout)'
echo 'Resultado ejecutable: 3 PASS, 0 FAIL'
