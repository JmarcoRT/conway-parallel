#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-input-suite.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
# -fopenmp es necesario porque game.cpp ahora referencia core::step_openmp
# desde Game::step(); sin el flag hay undefined reference en el enlazado.
flags=(-std=c++20 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

"$compiler" "${flags[@]}" -I"$root/include" \
    "$root/tests/input_tests.cpp" "$root/src/input/input.cpp" "$root/src/core/game.cpp" \
    "$root/src/core/engines/sequential.cpp" "$root/src/core/engines/openmp.cpp" "$root/src/core/bs23.cpp" \
    -o "$work/input_tests"
"$work/input_tests" "$root" "$@"

cmake -S "$root" -B "$work/build" \
    -DBUILD_TESTING=OFF \
    -DCMAKE_CXX_COMPILER="$compiler"
cmake --build "$work/build" --target conway --parallel 2
binary="$work/build/conway"
mkdir -p "$work/app/patterns"

# Quita los codigos ANSI del output para poder comparar texto plano
# con grep sin depender de colores ni posicion en pantalla.
strip_ansi() {
    sed -e $'s/\x1b\\[[0-9;]*m//g' -e $'s/\x1b\\[[0-9]*[A-Za-z]//g'
}
clean="$work/actual_clean.txt"
exe_failures=0

# El menu de la app lee lineas por stdin. Secuencia usada por las
# verificaciones:
#   - "C"           -> Cambiar entrada (pide ruta)
#   - "<ruta>"      -> se carga el patron (o falla con diagnostico)
#   - "0"           -> Salir
# En modo no interactivo el runloop termina al recibir EOF o "0".
# Tras cargar/fallar, la app imprime el menu otra vez con un mensaje
#     (verde si exito, rojo si error) y el patron activo.
run_conway() {
    local status=0
    (cd "$work/app" && "$binary") > "$work/actual.txt" 2> "$work/stderr.txt" || status=$?
    strip_ansi < "$work/actual.txt" > "$clean"
    return "$status"
}

# === Test 1: la aplicacion carga un patron valido y muestra la
#     entrada activa con sus dimensiones ===
cp "$root/patterns/block.txt" "$work/app/patterns/test.txt"
status=0
run_conway <<'STDIN' || status=$?
C
patterns/test.txt
0
STDIN
strip_ansi < "$work/actual.txt" > "$clean"
if [[ "$status" -eq 0 ]] \
   && grep -q 'Patrón actual:.*test\.txt' "$clean" \
   && grep -q '4 × 4' "$clean" \
   && grep -q 'Entrada cargada:.*test\.txt' "$clean"; then
    echo 'PASS executable_loads_valid_pattern (rc=0, patron activo, 4 x 4, mensaje de carga)'
else
    echo "FAIL executable_loads_valid_pattern (rc=$status)"
    exe_failures=$((exe_failures + 1))
fi

# === Test 2: la aplicacion rechaza un patron con caracter invalido
#     (mismo archivo test.txt, ahora con un caracter no binario) ===
cp "$root/tests/fixtures/input/invalid/invalid_character.txt" "$work/app/patterns/test.txt"
status=0
run_conway <<'STDIN' || status=$?
C
patterns/test.txt
0
STDIN
strip_ansi < "$work/actual.txt" > "$clean"
if [[ "$status" -eq 0 ]] \
   && grep -q 'Error al cargar:.*solo puede contener 0 y 1' "$clean"; then
    echo 'PASS executable_invalid_pattern (rc=0, mensaje de error de entrada invalida)'
else
    echo "FAIL executable_invalid_pattern (rc=$status)"
    exe_failures=$((exe_failures + 1))
fi

# === Test 3: la aplicacion maneja una ruta inexistente
#     (movemos test.txt para que la ruta no exista) ===
mv "$work/app/patterns/test.txt" "$work/app/patterns/unused.txt" 2>/dev/null || true
status=0
run_conway <<'STDIN' || status=$?
C
does_not_exist.txt
0
STDIN
strip_ansi < "$work/actual.txt" > "$clean"
if [[ "$status" -eq 0 ]] \
   && grep -q 'Error al cargar:.*No se pudo abrir el archivo:.*does_not_exist' "$clean"; then
    echo 'PASS executable_missing_file (rc=0, mensaje de archivo no encontrado)'
else
    echo "FAIL executable_missing_file (rc=$status)"
    exe_failures=$((exe_failures + 1))
fi

# Resumen: si alguna verificacion fallo, el script sale con codigo != 0
# para que CI detecte el fallo sin abortar a la primera (los 3 PASS/FAIL
# ya se imprimieron arriba).
echo "Resultado ejecutable: $((3 - exe_failures)) PASS, ${exe_failures} FAIL"
if [[ "$exe_failures" -ne 0 ]]; then
    exit 1
fi
