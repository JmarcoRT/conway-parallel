#!/usr/bin/env bash
# Matriz de ejecuciones del motor MPI (conway_mpi) con 1, 2 y 4 procesos,
# casos borde (3 y 7 procesos, mas procesos que filas) y ambos modos de
# intercambio de halos (blocking y overlap).
#
# Cada ejecucion usa --verify (oraculo: motor secuencial sobre el tablero
# completo). Ademas, el tablero final de cada (np, modo) se compara con el de
# np=1/blocking mediante tests/compare_boards.py: el resultado no depende de
# cuantos procesos ni de que estrategia de halos se usen.
#
# Uso:   tests/run_mpi_engine.sh [directorio_build]
# Vars:  MPIRUN        (por defecto: mpirun)
#        MPIRUN_FLAGS  (por defecto: --oversubscribe, por si hay menos nucleos que procesos)
#        NP_LIST       (por defecto: "1 2 4 3 7")
#        MODES         (por defecto: "blocking overlap")
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build"}
exe="$build/conway_mpi"
mpirun_cmd=${MPIRUN:-mpirun}
read -r -a mpirun_flags <<< "${MPIRUN_FLAGS---oversubscribe}"
read -r -a np_list <<< "${NP_LIST:-1 2 4 3 7}"
read -r -a modes <<< "${MODES:-blocking overlap}"
python=${PYTHON:-python3}

if [[ ! -x "$exe" ]]; then
    echo "No existe $exe. Compila antes: cmake -S . -B build && cmake --build build" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
case_id=0

run_case() {
    local np=$1 id=$2 mode=$3; shift 3
    local dump="$tmp/case${id}_np${np}_${mode}.txt"
    echo "--- mpirun -np $np conway_mpi $* --mode $mode ---"
    if ! out=$("$mpirun_cmd" "${mpirun_flags[@]}" -np "$np" "$exe" "$@" --mode "$mode" --verify --dump "$dump" 2>&1); then
        echo "$out"
        echo ">>> FALLO (np=$np $*)" >&2
        fail=1
        return
    fi
    echo "$out" | grep -E '^(Procesos|RESULTADO)'
    local base="$tmp/case${id}_np${np_list[0]}_${modes[0]}.txt"
    if [[ "$dump" != "$base" ]]; then
        if ! "$python" "$root/tests/compare_boards.py" "$base" "$dump" > /dev/null; then
            echo ">>> FALLO: tablero final (np=$np, $mode) distinto de np=${np_list[0]}, ${modes[0]} ($*)" >&2
            fail=1
        fi
    fi
}

cd "$root"
cases=(
    "patterns/glider.txt --generations 40"
    "patterns/blinker.txt --generations 7"
    "patterns/pulsar.txt --generations 30"
    "patterns/gosper_glider_gun.txt --generations 150"
    "--generate 101 64 3500 42 --generations 60"
    "--generate 3 9 5000 7 --generations 8"
)
for spec in "${cases[@]}"; do
    case_id=$((case_id + 1))
    read -r -a args <<< "$spec"
    for mode in "${modes[@]}"; do
        for np in "${np_list[@]}"; do
            run_case "$np" "$case_id" "$mode" "${args[@]}"
        done
    done
done

if [[ $fail -ne 0 ]]; then
    echo "ALGUN CASO FALLO" >&2
    exit 1
fi
echo "TODOS LOS CASOS OK"
