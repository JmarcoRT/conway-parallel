#!/usr/bin/env bash
# Medicion exploratoria del motor MPI: 1, 2 y 4 procesos, modos blocking y
# overlap. NO es una campana experimental oficial (ver docs/BENCHMARK_PROTOCOL.md):
# sirve para decidir optimizaciones del intercambio de halos.
#
# Cada ejecucion usa --verify (el oraculo secuencial corre FUERA del
# cronometro) y se registra "Tiempo del lote de generaciones (max entre
# ranks)", que conway_mpi mide entre MPI_Barrier+MPI_Wtime y el final del
# bucle, sin reparto, reensamblado ni E/S. Se resume con la mediana por
# (np, modo) y speedup = mediana(np=1, mismo modo) / mediana(np, modo).
#
# Uso:   bash scripts/measure_mpi.sh [directorio_build] [archivo_salida]
# Vars:  MPIRUN (mpirun), MPIRUN_FLAGS (--oversubscribe), NP_LIST ("1 2 4"),
#        MODES ("blocking overlap"), REPEATS (5), ROWS (2048), COLS (2048),
#        DENSITY (3000 = 30 %), SEED (2026), GENERATIONS (100)
# Sin archivo_salida solo escribe en stdout (no genera evidencia).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build"}
out=${2:-}
exe="$build/conway_mpi"
mpirun_cmd=${MPIRUN:-mpirun}
read -r -a mpirun_flags <<< "${MPIRUN_FLAGS---oversubscribe}"
read -r -a np_list <<< "${NP_LIST:-1 2 4}"
read -r -a modes <<< "${MODES:-blocking overlap}"
repeats=${REPEATS:-5}
rows=${ROWS:-2048}; cols=${COLS:-2048}; density=${DENSITY:-3000}
seed=${SEED:-2026}; generations=${GENERATIONS:-100}

if [[ ! -x "$exe" ]]; then
    echo "No existe $exe. Compila antes (preferible: cmake --preset experimental-release)" >&2
    exit 2
fi

measure() {
    echo "# Fecha: $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "# Host: $(uname -srm) | CPUs logicas: $(nproc 2>/dev/null || echo '?')"
    echo "# MPI: $("$mpirun_cmd" --version 2>&1 | head -n 1)"
    echo "# Tablero generado ${rows}x${cols}, densidad ${density}/10000, semilla ${seed}, ${generations} generaciones"
    echo "# Rondas alternadas: en cada repeticion se recorren todos los (modo, np)"
    echo "np,mode,repeat,seconds"
    for ((rep = 1; rep <= repeats; ++rep)); do
        for mode in "${modes[@]}"; do
            for np in "${np_list[@]}"; do
                local text secs
                text=$("$mpirun_cmd" "${mpirun_flags[@]}" -np "$np" "$exe" \
                       --generate "$rows" "$cols" "$density" "$seed" \
                       --generations "$generations" --mode "$mode" --verify)
                if ! grep -q '^RESULTADO: OK' <<< "$text"; then
                    echo "FALLO de verificacion (np=$np, $mode)" >&2
                    echo "$text" >&2
                    exit 1
                fi
                secs=$(sed -n 's/^Tiempo del lote de generaciones (max entre ranks): \([0-9.]*\) s$/\1/p' <<< "$text")
                echo "$np,$mode,$rep,$secs"
            done
        done
    done
}

summarize() {
    awk -F, '
        /^#/ || $1 == "np" { next }
        { key = $2 "," $1; n[key]++; v[key, n[key]] = $4; modes[$2] = 1; nps[$1] = 1 }
        function median(key,   i, j, t, m, a) {
            m = n[key]
            for (i = 1; i <= m; i++) a[i] = v[key, i]
            for (i = 2; i <= m; i++) { t = a[i]; for (j = i - 1; j >= 1 && a[j] > t; j--) a[j + 1] = a[j]; a[j + 1] = t }
            return (m % 2) ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
        }
        END {
            print "# Resumen: modo, np, muestras, mediana(s), speedup vs np=1 del mismo modo"
            for (mode in modes) for (np in nps) {
                key = mode "," np
                if (!(key in n)) continue
                med = median(key); base = mode ",1"
                sp = (base in n && med > 0) ? sprintf("%.3f", median(base) / med) : "N/D"
                printf "# %s, %s, %d, %.6f, %s\n", mode, np, n[key], med, sp | "sort -t, -k1,1 -k2,2n"
            }
        }'
}

if [[ -n "$out" ]]; then
    mkdir -p -- "$(dirname -- "$out")"
    measure | tee "$out.tmp"
    summarize < "$out.tmp" | tee -a "$out.tmp"
    mv -- "$out.tmp" "$out"
else
    data=$(measure)
    echo "$data"
    summarize <<< "$data"
fi
