#!/usr/bin/env bash
# Compila y ejecuta la comparacion OpenMP vs secuencial (S6).
# No requiere CMake ni MPI: solo g++ con soporte OpenMP (libgomp).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-openmp.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++20 -O2 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/openmp_vs_sequential.cpp" \
    "$root/src/core/engines/openmp.cpp" \
    "$root/src/core/engines/sequential.cpp" \
    "$root/src/core/bs23.cpp" \
    "$root/src/input/input.cpp" \
    -o "$work/openmp_vs_sequential"

"$work/openmp_vs_sequential" "$root"
