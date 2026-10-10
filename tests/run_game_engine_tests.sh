#!/usr/bin/env bash
# Semana 7 - Compilacion y ejecucion de la validacion OpenMP vs secuencial
# a traves de conway::Game (tests/game_engine_tests.cpp).
#
# Compila con g++ -O2 -fopenmp (sin CMake ni MPI) y corre el binario pasandole
# la raiz del repositorio como unico argumento. La raiz es necesaria porque
# tests/game_engine_tests.cpp carga patterns/block.txt, blinker.txt y
# glider.txt desde alli.
#
# Uso:   bash tests/run_game_engine_tests.sh
# Salida: 38 PASS / 0 FAIL (segun docs/OPENMP_INTEGRATION.md).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-game-engine.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++20 -O2 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/game_engine_tests.cpp" \
    "$root/src/core/game.cpp" \
    "$root/src/core/engines/sequential.cpp" \
    "$root/src/core/engines/openmp.cpp" \
    "$root/src/core/bs23.cpp" \
    "$root/src/input/input.cpp" \
    -o "$work/game_engine_tests"

"$work/game_engine_tests" "$root"
