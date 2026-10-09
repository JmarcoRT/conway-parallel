#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-correctness-suite.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/conway_correctness_tests.cpp" \
    "$root/src/input/input.cpp" "$root/src/core/game.cpp" \
    "$root/src/core/engines/sequential.cpp" "$root/src/core/bs23.cpp" \
    -o "$work/conway_correctness_tests"

"$work/conway_correctness_tests" "$root"
