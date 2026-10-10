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

if command -v script >/dev/null 2>&1; then
    checks=23
    pty_status=0
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf 'patterns/blinker.txt\n'
        sleep 0.2
        printf '3'
        sleep 0.2
        printf 'O'
        sleep 0.2
        printf '2'
        sleep 0.2
        printf 'n'
        sleep 0.2
        printf 'p'
        sleep 0.4
        printf 'p'
        sleep 0.2
        printf 'r'
        sleep 0.2
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 15 script -qfec \
        'initial=$(stty -g); ./build/conway; app_status=$?; final=$(stty -g); if [[ "$initial" == "$final" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit "$app_status"' \
        /dev/null > "$work/pty.txt" 2>&1 || pty_status=$?
    strip_ansi < "$work/pty.txt" > "$work/pty-clean.txt"
    main_menu_count=$(grep -cF "[3]" "$work/pty-clean.txt" || true)
    if [[ "$main_menu_count" -ge 4 ]]; then
        echo "PASS: interactive_escape_returns_to_menu ($main_menu_count main-menu renders)"
    else
        echo "FAIL: interactive_escape_returns_to_menu ($main_menu_count main-menu renders, expected >= 4)"
        failures=$((failures + 1))
    fi
    check interactive_terminal_state_restored "TERMINAL_RESTORED" "$work/pty-clean.txt"
    if [[ "$pty_status" -eq 0 ]]; then
        echo "PASS: interactive_terminal_session_exits_cleanly"
    else
        echo "FAIL: interactive_terminal_session_exits_cleanly (rc=$pty_status)"
        failures=$((failures + 1))
    fi
else
    checks=20
    echo "SKIP: interactive_pty_checks (script utility unavailable)"
fi

echo "Resultado: $((checks - failures)) PASS, $failures FAIL"
if [[ "$failures" -ne 0 ]]; then
    exit 1
fi
