#!/usr/bin/env bash
# Ejecuta el prototipo MPI de reparto de filas con varios numeros de procesos
# y tableros (incluyendo filas no divisibles y mas procesos que filas).
#
# Uso:   tests/run_mpi_partition.sh [directorio_build]
# Vars:  MPIRUN        (por defecto: mpirun)
#        MPIRUN_FLAGS  (por defecto: --oversubscribe, por si hay menos nucleos que procesos)
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build"}
exe="$build/mpi_partition_prototype"
mpirun_cmd=${MPIRUN:-mpirun}
read -r -a mpirun_flags <<< "${MPIRUN_FLAGS---oversubscribe}"

if [[ ! -x "$exe" ]]; then
    echo "No existe $exe. Compila antes: cmake -S . -B build && cmake --build build" >&2
    exit 2
fi

fail=0
run_case() {
    local np=$1; shift
    echo "--- mpirun -np $np $* ---"
    if ! out=$("$mpirun_cmd" "${mpirun_flags[@]}" -np "$np" "$exe" "$@" 2>&1); then
        echo "$out"
        echo ">>> FALLO (np=$np $*)" >&2
        fail=1
        return
    fi
    echo "$out" | grep -E '^(Procesos|RESULTADO)'
}

cd "$root"
for np in 1 2 3 4 7; do
    run_case "$np" patterns/glider.txt          # 6 filas: 6/4 y 6/7 no divisibles
    run_case "$np" patterns/blinker.txt         # 5 filas
done
for np in 1 3 4 8; do
    run_case "$np" --synthetic 10 13            # 10/4 -> 3,3,2,2
    run_case "$np" --synthetic 3 5              # menos filas que procesos (np=4, 8)
    run_case "$np" --synthetic 101 64
done

if [[ $fail -ne 0 ]]; then
    echo "ALGUN CASO FALLO" >&2
    exit 1
fi
echo "TODOS LOS CASOS OK"
