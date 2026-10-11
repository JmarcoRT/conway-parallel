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
checks=0
check() {
    local label="$1"
    local needle="$2"
    local file="$3"
    checks=$((checks + 1))
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
check benchmark_reports_both_medians "Secuencial     " "$work/clean.txt"
check benchmark_reports_openmp_median "OpenMP         " "$work/clean.txt"
check benchmark_reports_speedup "Speedup:" "$work/clean.txt"
check benchmark_reports_effective_threads "Hilos OpenMP efectivos:" "$work/clean.txt"
check benchmark_reports_efficiency "Eficiencia:" "$work/clean.txt"
check benchmark_checks_equivalence "Estados equivalentes: SI" "$work/clean.txt"
if [[ "$status" -eq 0 ]]; then
    echo "PASS: navigation_returns_and_exits_cleanly"
else
    echo "FAIL: navigation_returns_and_exits_cleanly (rc=$status)"
    failures=$((failures + 1))
fi
checks=$((checks + 1))

preserve_status=0
printf 'C\npatterns/blinker.txt\nC\npatterns/does-not-exist.txt\n0\n' \
    | "$binary" > "$work/preserve.txt" 2>&1 || preserve_status=$?
strip_ansi < "$work/preserve.txt" > "$work/preserve-clean.txt"
check failed_pattern_load_preserves_selected_pattern \
    "Patrón actual: patterns/blinker.txt" "$work/preserve-clean.txt"
check failed_pattern_load_reports_preservation \
    "(se conserva la entrada anterior)" "$work/preserve-clean.txt"
if [[ "$preserve_status" -ne 0 ]]; then
    echo "FAIL: failed_pattern_load_flow_exits_cleanly (rc=$preserve_status)"
    failures=$((failures + 1))
else
    echo "PASS: failed_pattern_load_flow_exits_cleanly"
fi
checks=$((checks + 1))

eof_status=0
"$binary" </dev/null > "$work/eof.txt" 2>&1 || eof_status=$?
if [[ "$eof_status" -eq 0 ]]; then
    echo "PASS: eof_at_main_menu_exits_cleanly"
else
    echo "FAIL: eof_at_main_menu_exits_cleanly (rc=$eof_status)"
    failures=$((failures + 1))
fi
checks=$((checks + 1))

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
checks=$((checks + 1))

simulation_eof_status=0
printf 'C\npatterns/blinker.txt\n2\n' \
    | "$binary" > "$work/simulation-eof.txt" 2>&1 || simulation_eof_status=$?
if [[ "$simulation_eof_status" -eq 0 ]]; then
    echo "PASS: eof_during_visualization_exits_cleanly"
else
    echo "FAIL: eof_during_visualization_exits_cleanly (rc=$simulation_eof_status)"
    failures=$((failures + 1))
fi
checks=$((checks + 1))

if command -v script >/dev/null 2>&1; then
    pty_status=0
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf '\033[B\r'
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
        'initial=$(stty -g); NO_COLOR=1 ./build/conway; app_status=$?; final=$(stty -g); if [[ "$initial" == "$final" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit "$app_status"' \
        /dev/null > "$work/pty.txt" 2>&1 || pty_status=$?
    strip_ansi < "$work/pty.txt" > "$work/pty-clean.txt"
    check interactive_catalog_selects_discovered_blinker \
        "Patrón: patterns/blinker.txt" "$work/pty-clean.txt"
    check interactive_board_renders_live_cells_without_ansi "██" "$work/pty-clean.txt"
    check interactive_board_renders_dead_cells_without_ansi "░░" "$work/pty-clean.txt"
    main_menu_count=$(grep -cF "[3]" "$work/pty-clean.txt" || true)
    if [[ "$main_menu_count" -ge 4 ]]; then
        echo "PASS: interactive_escape_returns_to_menu ($main_menu_count main-menu renders)"
    else
        echo "FAIL: interactive_escape_returns_to_menu ($main_menu_count main-menu renders, expected >= 4)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    check interactive_terminal_state_restored "TERMINAL_RESTORED" "$work/pty-clean.txt"
    if grep -qF $'\033[?1049h' "$work/pty.txt" &&
        grep -qF $'\033[?1049l' "$work/pty.txt"; then
        echo "PASS: interactive_session_uses_and_restores_alternate_screen"
    else
        echo "FAIL: interactive_session_uses_and_restores_alternate_screen"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if [[ "$pty_status" -eq 0 ]]; then
        echo "PASS: interactive_terminal_session_exits_cleanly"
    else
        echo "FAIL: interactive_terminal_session_exits_cleanly (rc=$pty_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    menu_resize_status=0
    {
        sleep 1
        printf '0'
    } | timeout 8 script -qfec \
        'stty cols 80 rows 24; initial=$(stty -g); { sleep 0.5; stty cols 120 rows 40 </dev/tty; } & resize_pid=$!; ./build/conway; app_status=$?; wait "$resize_pid"; final=$(stty -g); if [[ "$initial" == "$final" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit "$app_status"' \
        /dev/null > "$work/menu-resize-pty.txt" 2>&1 || menu_resize_status=$?
    strip_ansi < "$work/menu-resize-pty.txt" > "$work/menu-resize-pty-clean.txt"
    resized_menus=$(grep -cF "CONWAY PARALLEL" "$work/menu-resize-pty-clean.txt" || true)
    if [[ "$resized_menus" -eq 2 ]]; then
        echo "PASS: main_menu_refreshes_after_resize"
    else
        echo "FAIL: main_menu_refreshes_after_resize ($resized_menus renders, expected 2)"
        failures=$((failures + 1))
    fi
    check menu_resize_terminal_state_restored "TERMINAL_RESTORED" \
        "$work/menu-resize-pty-clean.txt"
    if [[ "$menu_resize_status" -eq 0 ]]; then
        echo "PASS: resized_menu_session_exits_cleanly"
    else
        echo "FAIL: resized_menu_session_exits_cleanly (rc=$menu_resize_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 2))

    paused_status=0
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf '\033[B\r'
        sleep 0.2
        printf '2'
        sleep 1
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 10 script -qfec './build/conway' /dev/null \
        > "$work/paused-pty.txt" 2>&1 || paused_status=$?
    strip_ansi < "$work/paused-pty.txt" > "$work/paused-pty-clean.txt"
    simulation_renders=$(grep -cF "JUEGO DE LA VIDA" "$work/paused-pty-clean.txt" || true)
    if [[ "$simulation_renders" -eq 1 ]]; then
        echo "PASS: paused_state_is_not_redrawn"
    else
        echo "FAIL: paused_state_is_not_redrawn ($simulation_renders renders, expected 1)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if [[ "$paused_status" -eq 0 ]]; then
        echo "PASS: paused_terminal_session_exits_cleanly"
    else
        echo "FAIL: paused_terminal_session_exits_cleanly (rc=$paused_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    resize_status=0
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf '\033[B\r'
        sleep 0.2
        printf '2'
        sleep 1.5
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 10 script -qfec \
        'initial=$(stty -g); { sleep 0.9; stty cols 100 rows 30 </dev/tty; } & resize_pid=$!; ./build/conway; app_status=$?; wait "$resize_pid"; final=$(stty -g); if [[ "$initial" == "$final" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit "$app_status"' \
        /dev/null > "$work/resize-pty.txt" 2>&1 || resize_status=$?
    strip_ansi < "$work/resize-pty.txt" > "$work/resize-pty-clean.txt"
    resized_renders=$(grep -cF "JUEGO DE LA VIDA" "$work/resize-pty-clean.txt" || true)
    if [[ "$resized_renders" -eq 2 ]]; then
        echo "PASS: paused_screen_refreshes_after_resize"
    else
        echo "FAIL: paused_screen_refreshes_after_resize ($resized_renders renders, expected 2)"
        failures=$((failures + 1))
    fi
    check resize_terminal_state_restored "TERMINAL_RESTORED" "$work/resize-pty-clean.txt"
    if [[ "$resize_status" -eq 0 ]]; then
        echo "PASS: resize_terminal_session_exits_cleanly"
    else
        echo "FAIL: resize_terminal_session_exits_cleanly (rc=$resize_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 2))
else
    echo "SKIP: interactive_pty_checks (script utility unavailable)"
fi

thread_config_status=0
printf 'C\npatterns/blinker.txt\n4\nC\n65\nC\n12\nX\n3\nO\n2\nn\nx\n1\n\n0\n' \
    | "$binary" > "$work/thread-config.txt" 2>&1 || thread_config_status=$?
strip_ansi < "$work/thread-config.txt" > "$work/thread-config-clean.txt"
check thread_config_rejects_invalid_input \
    "Error: introduce un entero entre 1 y 64." "$work/thread-config-clean.txt"
check thread_config_applies_to_visualization "solicitados: 12" \
    "$work/thread-config-clean.txt"
check thread_config_reports_effective_count "efectivos observados:" \
    "$work/thread-config-clean.txt"
check thread_config_applies_to_benchmark "OpenMP solicitados: 12" \
    "$work/thread-config-clean.txt"
if [[ "$thread_config_status" -eq 0 ]]; then
    echo "PASS: thread_configuration_flow_exits_cleanly"
else
    echo "FAIL: thread_configuration_flow_exits_cleanly (rc=$thread_config_status)"
    failures=$((failures + 1))
fi
checks=$((checks + 1))

echo "Resultado: $((checks - failures)) PASS, $failures FAIL"
if [[ "$failures" -ne 0 ]]; then
    exit 1
fi
