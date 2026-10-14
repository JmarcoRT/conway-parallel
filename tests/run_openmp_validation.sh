#!/usr/bin/env bash
# Semana 7 - Validacion independiente secuencial vs OpenMP (a traves de Game).
#
# Para cada (patron, generaciones) genera UN volcado de referencia con el
# motor secuencial y, por cada numero de hilos (1, 2, 4, 8), un volcado con
# el motor OpenMP integrado; los compara con tests/compare_boards.py
# (sin cambios) exigiendo la generacion alcanzada. Mismos criterios que
# docs/TESTING.md seccion de equivalencia: dimensiones, generacion, celdas
# y celulas vivas.
#
# Es complementaria a tests/game_engine_tests.cpp: aquella compara en memoria
# y fija los hilos con omp_set_num_threads; esta compara archivos volcados
# por un proceso aparte y fija los hilos con la variable de entorno
# OMP_NUM_THREADS (el otro camino documentado en docs/OPENMP_INTEGRATION.md).
#
# Uso:
#   bash tests/run_openmp_validation.sh [--bin RUTA_DUMP_BOARD]
#
# Variables opcionales:
#   CXX            compilador (por defecto g++); ignorado con --bin
#   PYTHON         interprete de Python (por defecto python3)
#   REPEATS        repeticiones de cada caso OpenMP (por defecto 1). Con mas de
#                  un nucleo conviene repetir (p. ej. REPEATS=20) para dar
#                  oportunidad a que aparezca una carrera de datos.
#   EVIDENCE_OUT   si se define, ademas de stdout se escribe la evidencia en
#                  ese archivo (con cabecera de entorno).
#
# Entorno OpenMP: se fija OMP_DYNAMIC=FALSE y se eliminan OMP_THREAD_LIMIT,
# OMP_SCHEDULE, OMP_PROC_BIND y OMP_PLACES heredados, para que "N hilos" sea
# realmente N. Ademas dump_board --require-threads N aborta si el equipo real
# no es N, de modo que un caso nunca pasa "con 8 hilos" corriendo con menos.
#
# Codigo de salida: 0 si todos los casos son equivalentes; 1 si alguno no lo
# es o falla una herramienta; 2 por uso incorrecto o entradas ausentes.

set -uo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python=${PYTHON:-python3}
repeats=${REPEATS:-1}
threads_list=(1 2 4 8)

# patron:generaciones (separadas por coma). Los patrones pequenos con borde
# muerto interactuan con el borde del tablero en las generaciones largas
# (p. ej. glider); no importa: ambos motores deben coincidir igualmente.
cases=(
    "block:1,2,10"
    "blinker:1,2,25"
    "toad:1,2,25"
    "beacon:1,2,25"
    "glider:4,12,40"
    "pulsar:1,3,30"
    "gosper_glider_gun:1,30,120"
)

dump_bin=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin)
            [[ $# -ge 2 ]] || { echo "Falta el valor de --bin" >&2; exit 2; }
            dump_bin=$2
            shift 2
            ;;
        *)
            echo "Argumento desconocido: $1" >&2
            echo "Uso: bash tests/run_openmp_validation.sh [--bin RUTA_DUMP_BOARD]" >&2
            exit 2
            ;;
    esac
done

[[ $repeats =~ ^[1-9][0-9]*$ ]] || { echo "REPEATS debe ser un entero positivo" >&2; exit 2; }
command -v "$python" >/dev/null 2>&1 || { echo "No se encontro $python" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/conway-openmp-validation.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

if [[ -z $dump_bin ]]; then
    compiler=${CXX:-g++}
    "$compiler" -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -fopenmp -I"$root/include" \
        "$root/tests/dump_board.cpp" "$root/src/input/input.cpp" \
        "$root/src/core/game.cpp" "$root/src/core/engines/sequential.cpp" \
        "$root/src/core/engines/openmp.cpp" "$root/src/core/bs23.cpp" \
        -o "$work/dump_board" || { echo "Fallo la compilacion de dump_board" >&2; exit 1; }
    dump_bin="$work/dump_board"
fi
[[ -x $dump_bin ]] || { echo "dump_board no es ejecutable: $dump_bin" >&2; exit 2; }

for entry in "${cases[@]}"; do
    [[ -f "$root/patterns/${entry%%:*}.txt" ]] || {
        echo "Falta patterns/${entry%%:*}.txt" >&2; exit 2; }
done

export OMP_DYNAMIC=FALSE
unset OMP_THREAD_LIMIT OMP_SCHEDULE OMP_PROC_BIND OMP_PLACES OMP_NUM_THREADS

out_file=${EVIDENCE_OUT:-}
emit() {
    echo "$@"
    if [[ -n $out_file ]]; then echo "$@" >> "$out_file"; fi
}
if [[ -n $out_file ]]; then : > "$out_file"; fi

cpu_model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' || true)
emit "Validacion secuencial vs OpenMP (Game, Engine::OpenMP)"
emit "Fecha: $(date -u '+%Y-%m-%d %H:%M UTC')"
if git -C "$root" rev-parse --short HEAD >/dev/null 2>&1; then
    if [[ -z $(git -C "$root" status --porcelain --untracked-files=no 2>/dev/null) ]]; then
        tree_state="arbol de trabajo limpio"
    else
        tree_state="CON CAMBIOS SIN COMMITEAR"
    fi
    emit "Commit: $(git -C "$root" rev-parse --short HEAD) ($tree_state)"
else
    emit "Commit: desconocido (no es un repositorio git)"
fi
emit "Sistema: $(uname -sr)"
emit "CPU: ${cpu_model:-desconocida}"
emit "Nucleos logicos (nproc): $(nproc 2>/dev/null || echo desconocido)"
emit "Compilador: $(${CXX:-g++} --version 2>/dev/null | head -1)"
emit "Hilos: ${threads_list[*]}   Repeticiones por caso OpenMP: $repeats"
emit "Entorno OpenMP fijado: OMP_DYNAMIC=FALSE (OMP_THREAD_LIMIT/OMP_SCHEDULE/OMP_PROC_BIND/OMP_PLACES eliminados); OMP_NUM_THREADS=N por caso"
emit ""

pass=0
fail=0
for entry in "${cases[@]}"; do
    name=${entry%%:*}
    input="$root/patterns/$name.txt"
    sha=$(sha256sum "$input" | cut -c1-12)
    IFS=',' read -r -a gens_list <<< "${entry#*:}"
    for gens in "${gens_list[@]}"; do
        ref="$work/ref_${name}_${gens}.txt"
        if ! "$dump_bin" "$input" "$gens" --engine seq > "$ref" 2> "$work/ref.err"; then
            emit "ERROR  referencia secuencial fallo: patron=$name gens=$gens"
            sed 's/^/       /' "$work/ref.err" | while IFS= read -r l; do emit "$l"; done
            fail=$((fail + 1))
            continue
        fi
        read -r w h g alive < "$ref"
        for threads in "${threads_list[@]}"; do
            for ((rep = 1; rep <= repeats; ++rep)); do
                cand="$work/omp_${name}_${gens}_${threads}_${rep}.txt"
                label=$(printf 'patron=%-18s gens=%-4s hilos=%-2s rep=%s/%s tablero=%sx%s vivas=%-4s entrada_sha256=%s' \
                    "$name" "$gens" "$threads" "$rep" "$repeats" "$w" "$h" "$alive" "$sha")
                OMP_NUM_THREADS=$threads "$dump_bin" "$input" "$gens" \
                    --engine openmp --require-threads "$threads" \
                    > "$cand" 2> "$work/cand.err"
                rc=$?
                if [[ $rc -ne 0 ]]; then
                    emit "FAIL   $label"
                    emit "       dump_board OpenMP termino con codigo $rc:"
                    while IFS= read -r l; do emit "         $l"; done < "$work/cand.err"
                    fail=$((fail + 1))
                    continue
                fi
                if verdict=$("$python" "$root/tests/compare_boards.py" \
                        --require-generation "$gens" "$ref" "$cand" 2>&1); then
                    emit "PASS   $label"
                    pass=$((pass + 1))
                else
                    emit "FAIL   $label"
                    while IFS= read -r l; do emit "         $l"; done <<< "$verdict"
                    fail=$((fail + 1))
                fi
            done
        done
    done
done

emit ""
emit "Resultado: $pass EQUIVALENTE, $fail NO EQUIVALENTE/ERROR"
if [[ $fail -eq 0 ]]; then
    emit "OpenMP es equivalente al secuencial en todos los casos ejecutados"
    exit 0
fi
exit 1
