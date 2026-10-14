#!/usr/bin/env bash
# Medicion EXPLORATORIA del overhead del camino instrumentado de los
# motores (StepTraceSink no-op). NO es un test de rendimiento, no
# forma parte de CTest y no incluye umbrales arbitrarios.
#
# Compila scripts/measure_trace_overhead.cpp con las mismas opciones
# que scripts/measure_openmp.sh (estandar del proyecto) y lo ejecuta
# una vez. La salida son milisegundos medianos y ratios orientativos;
# los resultados son validos solo para el hardware/entorno concreto
# en el que se ejecuta.
#
# Uso:
#   bash scripts/measure_trace_overhead.sh
#   CXX=clang++ bash scripts/measure_trace_overhead.sh
#
# Requisitos: g++ (o $CXX) con OpenMP, los fuentes del proyecto, y
# que ya se haya configurado el repositorio (cmake -B build no es
# necesario para esta herramienta: compila directamente con g++).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-trace-overhead.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

compiler=${CXX:-g++}
flags=(-std=c++20 -O2 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

# Compilacion explicita y reproducible: misma raiz de includes y
# mismos modulos del core que la herramienta equivalente de timing.
"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/scripts/measure_trace_overhead.cpp" \
    "$root/src/core/game.cpp" \
    "$root/src/core/engines/sequential.cpp" \
    "$root/src/core/engines/openmp.cpp" \
    "$root/src/core/bs23.cpp" \
    "$root/src/input/input.cpp" \
    -o "$work/measure_trace_overhead"

echo "Fecha: $(date -u '+%Y-%m-%d %H:%M UTC')"
echo "Compilador: $("$compiler" --version | head -n 1)"
echo "Flags: ${flags[*]}"
echo "CPU: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //' || true)"
echo "Nucleos logicos (nproc): $(nproc 2>/dev/null || echo desconocido)"
echo
"$work/measure_trace_overhead"
