#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
binary="${1:-$root/build/conway}"
binary_cmd=$(printf '%q' "$binary")
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

check_menu_highlight() {
    local label="$1"
    local option="$2"
    local file="$3"
    local reverse_video=$'\x1b\\[(?:[0-9]+;)*7(?:;[0-9]+)*m'
    checks=$((checks + 1))
    if grep -aPq "${reverse_video}> ${option}" "$file"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label (selected row lacks focused reverse-video styling)"
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
if [[ "$preserve_status" -eq 0 ]]; then
    echo "PASS: failed_pattern_load_flow_exits_cleanly"
else
    echo "FAIL: failed_pattern_load_flow_exits_cleanly (rc=$preserve_status)"
    failures=$((failures + 1))
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
        printf '2'
        sleep 0.3
        printf 'n'
        sleep 0.2
        printf 'p'
        sleep 0.4
        printf 'p'
        sleep 0.3
        wc -c < "$work/pty.txt" > "$work/paused-before.txt"
        sleep 0.6
        wc -c < "$work/pty.txt" > "$work/paused-after.txt"
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 15 script -qfec \
        "initial=\$(stty -g); NO_COLOR=1 $binary_cmd; app_status=\$?; final=\$(stty -g); if [[ \"\$initial\" == \"\$final\" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit \"\$app_status\"" \
        /dev/null > "$work/pty.txt" 2>&1 || pty_status=$?
    strip_ansi < "$work/pty.txt" > "$work/pty-clean.txt"
    check interactive_catalog_is_rendered "CATÁLOGO DE PATRONES" "$work/pty-clean.txt"
    check interactive_simulation_is_rendered "SIMULACIÓN" "$work/pty-clean.txt"
    check interactive_current_and_next_are_rendered "CURRENT" "$work/pty-clean.txt"
    check interactive_next_generation_is_rendered "NEXT" "$work/pty-clean.txt"
    check interactive_step_advances_generation "Generación 1" "$work/pty-clean.txt"
    check interactive_board_uses_live_cell_blocks "▓▓" "$work/pty-clean.txt"
    check interactive_board_uses_dead_cell_blocks "░░" "$work/pty-clean.txt"
    check interactive_board_has_horizontal_grid_lines "─" "$work/pty-clean.txt"
    check interactive_board_has_crossed_cell_junctions "┼" "$work/pty-clean.txt"
    check interactive_board_has_vertical_grid_lines "│" "$work/pty-clean.txt"
    check_menu_highlight main_menu_selection_is_visually_highlighted \
        "Benchmark" "$work/pty.txt"
    header_writes=$(grep -ao "CONWAY PARALLEL" "$work/pty.txt" | wc -l)
    if [[ "$header_writes" -eq 1 ]]; then
        echo "PASS: unchanged_header_is_not_rewritten"
    else
        echo "FAIL: unchanged_header_is_not_rewritten ($header_writes writes)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if [[ "$(cat "$work/paused-before.txt")" == "$(cat "$work/paused-after.txt")" ]]; then
        echo "PASS: paused_state_emits_no_periodic_screen_updates"
    else
        echo "FAIL: paused_state_emits_no_periodic_screen_updates"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    check interactive_escape_returns_to_main_menu "MENÚ PRINCIPAL" "$work/pty-clean.txt"
    check interactive_terminal_state_restored "TERMINAL_RESTORED" "$work/pty-clean.txt"
    if grep -qF $'\033[?1049h' "$work/pty.txt" &&
        grep -qF $'\033[?1049l' "$work/pty.txt"; then
        echo "PASS: interactive_session_uses_and_restores_alternate_screen"
    else
        echo "FAIL: interactive_session_uses_and_restores_alternate_screen"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if ! grep -qF $'\033[2J' "$work/pty.txt"; then
        echo "PASS: interactive_session_does_not_clear_entire_screen"
    else
        echo "FAIL: interactive_session_does_not_clear_entire_screen"
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

    color_status=0
    {
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.4
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.5
        printf 'q'
        sleep 0.2
        printf '0'
    } | timeout 8 script -qfec \
        "TERM=xterm-256color COLORTERM=truecolor $binary_cmd" /dev/null \
        > "$work/color-pty.txt" 2>&1 || color_status=$?
    if grep -aPq '\x1b\[[0-9;]*48;2;98;232;137m {2}' "$work/color-pty.txt" &&
        [[ "$(grep -aoP '\x1b\[[0-9;]*48;2;98;232;137m {2}' \
            "$work/color-pty.txt" | wc -l)" -ge 2 ]]; then
        echo "PASS: current_and_next_cells_share_full_rgb_fill"
    else
        echo "FAIL: current_and_next_cells_share_full_rgb_fill"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if grep -aPq '\x1b\[[0-9;]*48;2;38;49;56m {2}' "$work/color-pty.txt"; then
        echo "PASS: dead_cells_use_uniform_muted_fill"
    else
        echo "FAIL: dead_cells_use_uniform_muted_fill"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if grep -aPq '\x1b\[[0-9;]*38;2;88;103;111m' "$work/color-pty.txt"; then
        echo "PASS: colored_grid_uses_muted_rgb_lines"
    else
        echo "FAIL: colored_grid_uses_muted_rgb_lines"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if [[ "$color_status" -eq 0 ]]; then
        echo "PASS: colored_simulation_session_exits_cleanly"
    else
        echo "FAIL: colored_simulation_session_exits_cleanly (rc=$color_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    large_status=0
    {
        sleep 0.3
        printf '\033[B'
        sleep 0.2
        printf '\r'
        sleep 0.3
        printf '\033[B\033[B\033[B\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.5
        printf 'q'
        sleep 0.2
        printf '0'
    } | timeout 8 script -qfec \
        "TERM=xterm-256color COLORTERM=truecolor $binary_cmd" /dev/null \
        > "$work/large-pty.txt" 2>&1 || large_status=$?
    strip_ansi < "$work/large-pty.txt" > "$work/large-pty-clean.txt"
    check large_pattern_keeps_dimensions_visible "50 × 18" \
        "$work/large-pty-clean.txt"
    check large_pattern_uses_viewport "Viewport:" "$work/large-pty-clean.txt"
    if [[ "$large_status" -eq 0 ]]; then
        echo "PASS: large_pattern_simulation_session_exits_cleanly"
    else
        echo "FAIL: large_pattern_simulation_session_exits_cleanly (rc=$large_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    navigation_status=0
    {
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[A'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[A'
        sleep 0.3
        printf '\033'
        sleep 0.3
        printf '3'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[A'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.3
        printf '4'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[A'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.3
        printf 'C'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\033[A'
        sleep 0.3
        printf '\033[B'
        sleep 0.3
        printf '\r'
        sleep 0.4
        printf '2'
        sleep 0.5
        printf 'n'
        sleep 0.3
        printf '\033'
        sleep 0.3
        printf '1'
        sleep 2
        printf '\033'
        sleep 0.3
        printf 'q'
    } | timeout 12 script -qfec \
        "NO_COLOR=1 $binary_cmd" /dev/null \
        > "$work/navigation-pty.txt" 2>&1 || navigation_status=$?
    strip_ansi < "$work/navigation-pty.txt" > "$work/navigation-pty-clean.txt"
    check_menu_highlight main_menu_arrow_selection_is_highlighted \
        "Seleccionar motor" "$work/navigation-pty.txt"
    check engine_submenu_escape_keeps_current_engine \
        "Motor actual: Secuencial" "$work/navigation-pty-clean.txt"
    check engine_submenu_arrow_selects_openmp \
        "Motor: OpenMP" "$work/navigation-pty-clean.txt"
    check_menu_highlight engine_submenu_highlights_arrow_selection \
        "OpenMP" "$work/navigation-pty.txt"
    check thread_submenu_arrow_selects_two_threads \
        "OpenMP solicitados: 2" "$work/navigation-pty-clean.txt"
    check_menu_highlight thread_submenu_highlights_arrow_selection \
        "2 hilos" "$work/navigation-pty.txt"
    check pattern_catalog_arrow_selects_blinker \
        "patterns/blinker.txt  ·  5 × 5" "$work/navigation-pty-clean.txt"
    check_menu_highlight pattern_catalog_highlights_arrow_selection \
        "blinker" "$work/navigation-pty.txt"
    check submenu_returns_to_main_menu "MENÚ PRINCIPAL" "$work/navigation-pty-clean.txt"
    check enter_selects_engine_and_returns_to_main \
        "Motor: OpenMP" "$work/navigation-pty-clean.txt"
    if ! grep -qF $'\033[2J' "$work/navigation-pty.txt"; then
        echo "PASS: submenu_navigation_does_not_clear_full_screen"
    else
        echo "FAIL: submenu_navigation_does_not_clear_full_screen"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    check submenu_flow_runs_simulation_and_benchmark "Mediana secuencial:" \
        "$work/navigation-pty-clean.txt"
    if [[ "$navigation_status" -eq 0 ]]; then
        echo "PASS: ftxui_submenu_navigation_session_exits_cleanly"
    else
        echo "FAIL: ftxui_submenu_navigation_session_exits_cleanly (rc=$navigation_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    benchmark_status=0
    {
        sleep 0.2
        printf '4'
        sleep 0.2
        printf '\033[B\033[B\033[B\r'
        sleep 0.2
        printf '3'
        sleep 0.2
        printf 'O'
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf '\r'
        sleep 0.2
        printf '1'
        sleep 2
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 10 script -qfec \
        "OMP_THREAD_LIMIT=1 $binary_cmd" /dev/null \
        > "$work/benchmark-pty.txt" 2>&1 || benchmark_status=$?
    strip_ansi < "$work/benchmark-pty.txt" > "$work/benchmark-pty-clean.txt"
    check interactive_thread_selection_applies_to_openmp \
        "OpenMP solicitado: 8" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_shows_sequential_median \
        "Mediana secuencial:" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_shows_openmp_median \
        "Mediana OpenMP:" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_reports_effective_threads \
        "Hilos efectivos:" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_uses_actual_thread_limit \
        "Hilos efectivos:     1" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_reports_equivalence \
        "Equivalencia:" "$work/benchmark-pty-clean.txt"
    check interactive_benchmark_warns_about_small_patterns \
        "Nota: tablero pequeño" "$work/benchmark-pty-clean.txt"
    if [[ "$benchmark_status" -eq 0 ]]; then
        echo "PASS: interactive_benchmark_session_exits_cleanly"
    else
        echo "FAIL: interactive_benchmark_session_exits_cleanly (rc=$benchmark_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    cp "$root/patterns/blinker.txt" "$work/external-pattern.txt"
    manual_path_status=0
    manual_path="$work/external-pattern.txt"
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf 'm'
        sleep 0.8
        for ((index = 0; index < ${#manual_path}; ++index)); do
            printf '%s' "${manual_path:index:1}"
            sleep 0.03
        done
        sleep 0.8
        printf '\r'
        sleep 0.8
        printf 'q'
    } | timeout 10 script -qfec \
        "NO_COLOR=1 $binary_cmd" /dev/null \
        > "$work/manual-path-pty.txt" 2>&1 || manual_path_status=$?
    strip_ansi < "$work/manual-path-pty.txt" > "$work/manual-path-pty-clean.txt"
    check interactive_manual_path_loads_external_pattern \
        "$work/external-pattern.txt  ·  5 × 5" "$work/manual-path-pty-clean.txt"
    if [[ "$manual_path_status" -eq 0 ]]; then
        echo "PASS: interactive_manual_path_session_exits_cleanly"
    else
        echo "FAIL: interactive_manual_path_session_exits_cleanly (rc=$manual_path_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))

    resize_status=0
    {
        sleep 0.2
        printf 'C'
        sleep 0.2
        printf '\r'
        sleep 0.3
        printf '2'
        sleep 2.5
        printf '\033'
        sleep 0.2
        printf '0'
    } | timeout 10 script -qfec \
        "stty cols 80 rows 24; initial=\$(stty -g); { sleep 0.9; stty cols 120 rows 40 </dev/tty; sleep 0.8; stty cols 50 rows 30 </dev/tty; } & resize_pid=\$!; $binary_cmd; app_status=\$?; wait \"\$resize_pid\"; final=\$(stty -g); if [[ \"\$initial\" == \"\$final\" ]]; then echo TERMINAL_RESTORED; else echo TERMINAL_NOT_RESTORED; fi; exit \"\$app_status\"" \
        /dev/null > "$work/resize-pty.txt" 2>&1 || resize_status=$?
    strip_ansi < "$work/resize-pty.txt" > "$work/resize-pty-clean.txt"
    check simulation_resize_keeps_screen_rendered "CURRENT" "$work/resize-pty-clean.txt"
    check simulation_resize_keeps_next_panel_rendered "NEXT" "$work/resize-pty-clean.txt"
    for corner in '┌' '┐' '└' '┘'; do
        check "simulation_resize_keeps_panel_corner_$corner" "$corner" \
            "$work/resize-pty-clean.txt"
    done
    if ! grep -qF $'\033[K' "$work/resize-pty.txt"; then
        echo "PASS: differential_rows_do_not_erase_terminal_edge"
    else
        echo "FAIL: differential_rows_do_not_erase_terminal_edge"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    check resize_terminal_state_restored "TERMINAL_RESTORED" "$work/resize-pty-clean.txt"
    if [[ "$resize_status" -eq 0 ]]; then
        echo "PASS: resized_terminal_session_exits_cleanly"
    else
        echo "FAIL: resized_terminal_session_exits_cleanly (rc=$resize_status)"
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
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
