#!/usr/bin/env bash
# Medicion inicial (exploratoria) del motor OpenMP via conway::Game (S7).
# Compila tests/openmp_timing.cpp con g++ -O2 -fopenmp (sin CMake ni MPI), la
# ejecuta y guarda la salida junto con datos del entorno en
# docs/evidence/openmp_timing.txt (o en el archivo indicado como primer argumento).
#
# Uso:   bash scripts/measure_openmp.sh [archivo_salida] [opciones de openmp_timing]
# Ej.:   bash scripts/measure_openmp.sh docs/evidence/openmp_timing.txt --repeats 7
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=${1:-"$root/docs/evidence/openmp_timing.txt"}
[[ $# -gt 0 ]] && shift
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-timing.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++20 -O2 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/openmp_timing.cpp" \
    "$root/src/core/game.cpp" \
    "$root/src/core/engines/sequential.cpp" \
    "$root/src/core/engines/openmp.cpp" \
    "$root/src/core/bs23.cpp" \
    "$root/src/input/input.cpp" \
    -o "$work/openmp_timing"

mkdir -p -- "$(dirname -- "$out")"
{
    echo "Fecha: $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "Compilador: $("$compiler" --version | head -n 1)"
    echo "Flags: ${flags[*]}"
    echo "Nucleos logicos (nproc): $(nproc 2>/dev/null || echo desconocido)"
    echo "CPU: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //' || true)"
    echo "SO: $(uname -sr)"
    echo
    "$work/openmp_timing" "$@"
} | tee "$out"
