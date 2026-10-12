#!/usr/bin/env bash
# Semana 7 (continuacion) - Verificacion automatizada de la seleccion
# de motor Secuencial/OpenMP desde el menu de la aplicacion.
#
# Conducta esperada:
#   - Por defecto, AppState.engine == Engine::Sequential.
#   - La opcion [3] Motor abre un submenu que muestra el motor actual
#     y permite elegir [S] Secuencial, [O] OpenMP, [X] Volver.
#   - La seleccion se conserva aunque el usuario cargue otro patron.
#   - "Secuencial" y "OpenMP" aparecen en la salida del submenu.
#
# Uso:  bash tests/test_engine_selection.sh
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
binary="${1:-$root/build/conway}"
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-engine-selection.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
clean="$work/actual_clean.txt"

# Eliminar codigos ANSI y secuencias de control para comparar texto
# plano con grep sin depender de colores ni posicion en pantalla.
strip_ansi() {
    sed -e $'s/\x1b\\[[0-9;]*m//g' -e $'s/\x1b\\[[0-9]*[A-Za-z]//g'
}

failures=0
expect_pass() {
    local name="$1"
    local needle="$2"
    local haystack="$3"
    if grep -qF -- "$needle" "$haystack"; then
        echo "PASS: $name"
    else
        echo "FAIL: $name (no encontre: $needle)"
        failures=$((failures + 1))
    fi
}

if [[ ! -x "$binary" ]]; then
    echo "FAIL: binario no encontrado o no ejecutable: $binary" >&2
    echo "      compila primero con: cmake --build build" >&2
    exit 1
fi

# ------------------------------------------------------------------
# Test 1: abrir el submenu, comprobar el motor inicial, seleccionarlo
# como OpenMP y salir.
# ------------------------------------------------------------------
status=0
printf '3\nO\n0\n0\n' | "$binary" > "$work/selection.out" 2>&1 || status=$?
strip_ansi < "$work/selection.out" > "$clean"

expect_pass "engine_submenu_appears" "MOTOR DE EJECUCION" "$clean"
expect_pass "engine_default_is_sequential" "Motor actual: Secuencial" "$clean"
expect_pass "engine_selection_confirms_openmp" "Motor seleccionado: OpenMP" "$clean"
if [[ "$status" -eq 0 ]]; then
    echo "PASS: app_exits_cleanly_after_engine_selection (rc=0)"
else
    echo "FAIL: app_exits_cleanly_after_engine_selection (rc=$status)"
    failures=$((failures + 1))
fi

# ------------------------------------------------------------------
# Test 2: persistencia real. Tras seleccionar OpenMP, verificamos el
# motor mostrado al reabrir el submenu y despues de cambiar el patron.
# ------------------------------------------------------------------
status=0
printf 'C\npatterns/blinker.txt\n3\nO\n3\n0\nC\npatterns/block.txt\n3\n0\n0\n' \
    | "$binary" > "$work/persistence.out" 2>&1 || status=$?
strip_ansi < "$work/persistence.out" > "$clean"

expect_pass "first_pattern_reloaded" "Entrada cargada: patterns/blinker.txt" "$clean"
expect_pass "second_pattern_reloaded" "Entrada cargada: patterns/block.txt" "$clean"
openmp_current_count=$(grep -cF "Motor actual: OpenMP" "$clean" || true)
if [[ "$status" -eq 0 && "$openmp_current_count" -ge 2 ]]; then
    echo "PASS: engine_persists_across_pattern_changes ($openmp_current_count vistas como OpenMP)"
else
    echo "FAIL: engine_persists_across_pattern_changes (rc=$status, vistas como OpenMP=$openmp_current_count, esperaba >= 2)"
    failures=$((failures + 1))
fi

# ------------------------------------------------------------------
# Test 3: cambiar de OpenMP a Secuencial desde el submenu.
# ------------------------------------------------------------------
status=0
printf '3\nO\n3\nS\n3\n0\n0\n' | "$binary" > "$work/switch.out" 2>&1 || status=$?
strip_ansi < "$work/switch.out" > "$clean"
expect_pass "engine_switches_back_to_sequential" "Motor seleccionado: Secuencial" "$clean"
expect_pass "sequential_is_current_after_switch" "Motor actual: Secuencial" "$clean"
if [[ "$status" -ne 0 ]]; then
    echo "FAIL: app_exits_cleanly_after_engine_switch (rc=$status)"
    failures=$((failures + 1))
else
    echo "PASS: app_exits_cleanly_after_engine_switch (rc=0)"
fi

# ------------------------------------------------------------------
# Test 4: atajo de salida del menu principal.
# ------------------------------------------------------------------
rc=$(printf '0\n' | "$binary" >/dev/null 2>&1; echo $?)
if [[ "$rc" -eq 0 ]]; then
    echo "PASS: main_menu_exit_shortcut (rc=0)"
else
    echo "FAIL: main_menu_exit_shortcut (rc=$rc)"
    failures=$((failures + 1))
fi

# ------------------------------------------------------------------
# Resumen
# ------------------------------------------------------------------
if [[ "$failures" -eq 0 ]]; then
    echo "engine_selection: todos los checks pasaron"
    exit 0
else
    echo "engine_selection: $failures check(s) fallaron"
    exit 1
fi
