#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
binary="$root/build/conway"
work=$(mktemp -d "${TMPDIR:-/tmp}/conway-functional-app.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

if [[ ! -x "$binary" ]]; then
    echo "FAIL: binario no encontrado o no ejecutable: $binary" >&2
    exit 1
fi

strip_ansi() {
    sed -e $'s/\x1b\\[[0-9;]*m//g' -e $'s/\x1b\\[[0-9]*[A-Za-z]//g'
}

failures=0
check() {
    local label="$1"
    local needle="$2"
    local file="$3"
    if grep -qF -- "$needle" "$file"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label (missing: $needle)"
        failures=$((failures + 1))
    fi
}

status=0
printf 'C\npatterns/blinker.txt\n3\nO\n2\nn\np\np\nr\nx\n3\n0\n1\n\n0\n' \
    | "$binary" > "$work/output.txt" 2>&1 || status=$?
strip_ansi < "$work/output.txt" > "$work/clean.txt"

check visualization_uses_current_and_next "CURRENT" "$work/clean.txt"
check visualization_uses_real_next_panel "NEXT" "$work/clean.txt"
check visualization_shows_generation_zero "Generación: 0" "$work/clean.txt"
check visualization_advances_generation "Generación: 1" "$work/clean.txt"
check visualization_autoplay_advances "Generación: 2" "$work/clean.txt"
check visualization_reports_playing_state "Estado: En ejecución" "$work/clean.txt"
check visualization_reports_paused_state "Estado: Pausado" "$work/clean.txt"
check visualization_reports_selected_engine "Motor: OpenMP" "$work/clean.txt"
check visualization_returns_to_menu "Visualizacion finalizada." "$work/clean.txt"
check engine_selection_survives_visualization "Motor actual: OpenMP" "$work/clean.txt"
check benchmark_reports_both_medians "Mediana Secuencial:" "$work/clean.txt"
check benchmark_reports_openmp_median "Mediana OpenMP:" "$work/clean.txt"
check benchmark_reports_speedup "Speedup:" "$work/clean.txt"
check benchmark_reports_efficiency "Eficiencia OpenMP:" "$work/clean.txt"
check benchmark_checks_equivalence "Estados equivalentes: SI" "$work/clean.txt"
if [[ "$status" -eq 0 ]]; then
    echo "PASS: navigation_returns_and_exits_cleanly"
else
    echo "FAIL: navigation_returns_and_exits_cleanly (rc=$status)"
    failures=$((failures + 1))
fi

eof_status=0
"$binary" </dev/null > "$work/eof.txt" 2>&1 || eof_status=$?
if [[ "$eof_status" -eq 0 ]]; then
    echo "PASS: eof_at_main_menu_exits_cleanly"
else
    echo "FAIL: eof_at_main_menu_exits_cleanly (rc=$eof_status)"
    failures=$((failures + 1))
fi

invalid_status=0
printf 'C\npatterns/does-not-exist.txt\n0\n' \
    | "$binary" > "$work/invalid.txt" 2>&1 || invalid_status=$?
strip_ansi < "$work/invalid.txt" > "$work/invalid-clean.txt"
check invalid_input_is_reported "Error al cargar:" "$work/invalid-clean.txt"
if [[ "$invalid_status" -eq 0 ]]; then
    echo "PASS: invalid_input_returns_to_menu"
else
    echo "FAIL: invalid_input_returns_to_menu (rc=$invalid_status)"
    failures=$((failures + 1))
fi

simulation_eof_status=0
printf 'C\npatterns/blinker.txt\n2\n' \
    | "$binary" > "$work/simulation-eof.txt" 2>&1 || simulation_eof_status=$?
if [[ "$simulation_eof_status" -eq 0 ]]; then
    echo "PASS: eof_during_visualization_exits_cleanly"
else
    echo "FAIL: eof_during_visualization_exits_cleanly (rc=$simulation_eof_status)"
    failures=$((failures + 1))
fi

echo "Resultado: $((20 - failures)) PASS, $failures FAIL"
if [[ "$failures" -ne 0 ]]; then
    exit 1
fi
