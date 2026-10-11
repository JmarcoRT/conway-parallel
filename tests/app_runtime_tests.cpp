#include "app/benchmark.hpp"
#include "app/pattern_catalog.hpp"
#include "app/simulation.hpp"
#include "app/thread_config.hpp"
#include "core/game.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"
#include "ui/simulation_layout.hpp"
#include "ui/terminal_text.hpp"

#include <omp.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <clocale>

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    if (!condition) {
        ++failures;
    }
}

InputData blinker() {
    InputData input{5, 5, std::vector<std::uint8_t>(25, 0)};
    input.cells[2 * input.width + 1] = 1;
    input.cells[2 * input.width + 2] = 1;
    input.cells[2 * input.width + 3] = 1;
    return input;
}

bool equalBoards(const conway::Game& lhs, const conway::Game& rhs) {
    if (lhs.width() != rhs.width() || lhs.height() != rhs.height() ||
        lhs.generation() != rhs.generation() || lhs.aliveCount() != rhs.aliveCount()) {
        return false;
    }
    for (std::size_t row = 0; row < lhs.height(); ++row) {
        for (std::size_t col = 0; col < lhs.width(); ++col) {
            if (lhs.isAlive(row, col) != rhs.isAlive(row, col)) {
                return false;
            }
        }
    }
    return true;
}

std::size_t maximumLineWidth(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    std::size_t maximum = 0;
    while (std::getline(lines, line)) {
        std::size_t width = 0;
        for (const unsigned char character : line) {
            if ((character & 0xc0) != 0x80) {
                ++width;
            }
        }
        maximum = std::max(maximum, width);
    }
    return maximum;
}

void writePattern(const std::filesystem::path& path, const std::string& data) {
    std::ofstream file(path);
    file << data;
}

} // namespace

int main(int argc, char** argv) {
    (void)std::setlocale(LC_CTYPE, "");
    using conway::Engine;
    using conway::Game;
    using conway::app::SimulationSession;

    const InputData input = blinker();
    SimulationSession session(input, Engine::OpenMP);
    const auto currentAtStart = conway::ui::boardLines(session.game());
    Game expectedNext(input);
    expectedNext.setEngine(Engine::OpenMP);
    expectedNext.step();

    check(currentAtStart == std::vector<std::string>{
              ".....", ".....", ".###.", ".....", "....."} &&
              session.game().generation() == 0,
          "current_view_matches_initial_board_without_advancing");
    check(conway::ui::nextBoardLines(session.game()) == conway::ui::boardLines(expectedNext) &&
              session.game().generation() == 0,
          "next_view_matches_real_next_generation");

    const auto monochrome = conway::ui::renderBoardLines(
        session.game(), conway::ui::BoardRenderOptions{3, 3, false});
    check(monochrome.size() == 4 && monochrome[0] == "░░ ░░ ░░" &&
              monochrome[2].find("██") != std::string::npos &&
              monochrome[2].find('#') == std::string::npos &&
              monochrome[2].find('.') == std::string::npos,
          "board_view_renders_solid_distinct_cells_without_markers");
    check(monochrome[3].find("columnas") != std::string::npos &&
              monochrome[3].find("filas") != std::string::npos,
          "board_view_reports_hidden_dimensions");
    const auto colored = conway::ui::renderBoardLines(
        session.game(), conway::ui::BoardRenderOptions{5, 5, true});
    check(colored[2].find("\033[42m  \033[0m") != std::string::npos &&
              colored[0].find("\033[100m  \033[0m") != std::string::npos,
          "board_view_uses_background_colors_for_both_cell_states");
    const auto enlarged = conway::ui::renderBoardLines(
        session.game(), conway::ui::BoardRenderOptions{5, 5, false, 3, 3});
    check(enlarged.size() == 15 && enlarged[0] == enlarged[1] &&
              enlarged[1] == enlarged[2] &&
              enlarged[6].find("██") != std::string::npos,
          "board_view_scales_small_cells_in_both_dimensions");

    std::ostringstream wideLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{82, 4, false, 32}, wideLayout);
    check(wideLayout.str().find("\033[") == std::string::npos &&
              wideLayout.str().find("CURRENT") != std::string::npos &&
              wideLayout.str().find("NEXT") != std::string::npos,
          "simulation_layout_supports_monochrome_output");
    const auto currentTitle = wideLayout.str().find("CURRENT");
    const auto nextTitle = wideLayout.str().find("NEXT", currentTitle);
    check(currentTitle != std::string::npos && nextTitle != std::string::npos &&
              nextTitle - currentTitle > 8,
          "simulation_layout_aligns_current_and_next_panels");
    std::ostringstream narrowLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{50, 2, false}, narrowLayout);
    check(narrowLayout.str().find("CURRENT") != std::string::npos &&
              narrowLayout.str().find("NEXT") != std::string::npos,
          "simulation_layout_adapts_to_narrow_terminal");
    std::ostringstream veryNarrowLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{24, 2, false}, veryNarrowLayout);
    check(maximumLineWidth(veryNarrowLayout.str()) <= 24,
          "simulation_layout_does_not_exceed_very_narrow_terminal");
    std::ostringstream compactHeightLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{80, 18, false}, compactHeightLayout);
    check(compactHeightLayout.str().find("CURRENT") != std::string::npos &&
              compactHeightLayout.str().find("NEXT") != std::string::npos &&
              compactHeightLayout.str().find("Esc Menú") != std::string::npos &&
              maximumLineWidth(compactHeightLayout.str()) <= 80,
          "simulation_layout_preserves_controls_in_compact_geometry");
    std::ostringstream tinyLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{1, 2, false}, tinyLayout);
    check(maximumLineWidth(tinyLayout.str()) <= 1,
          "simulation_layout_handles_tiny_terminal_without_overflow");
    std::istringstream layoutLines(wideLayout.str());
    std::size_t renderedRows = 0;
    std::string renderedLine;
    while (std::getline(layoutLines, renderedLine)) {
        ++renderedRows;
    }
    check(renderedRows == 32 && wideLayout.str().front() == '\n',
          "simulation_layout_fills_and_centers_terminal_height");
    std::ostringstream tallLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{120, 22, false, 40}, tallLayout);
    std::istringstream tallRows(tallLayout.str());
    renderedRows = 0;
    while (std::getline(tallRows, renderedLine)) {
        ++renderedRows;
    }
    check(renderedRows == 40 && maximumLineWidth(tallLayout.str()) <= 120,
          "simulation_layout_adapts_to_large_terminal_geometry");
    std::ostringstream centeredBoards;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", enlarged}, {"NEXT", enlarged},
        conway::ui::SimulationLayoutConfig{120, enlarged.size(), false, 40}, centeredBoards);
    check(centeredBoards.str().find("██") != std::string::npos &&
              centeredBoards.str().find("░░") != std::string::npos &&
              maximumLineWidth(centeredBoards.str()) <= 120,
          "simulation_layout_centers_scaled_boards_without_oversized_panels");

    std::ostringstream menuLayout;
    auto* const previousBuffer = std::cout.rdbuf(menuLayout.rdbuf());
    conway::ui::renderLayout(
        "CONWAY PARALLEL", "Menú adaptable", {}, {{"1", "Benchmark"}, {"0", "Salir"}},
        0, conway::ui::LayoutConfig{120, 40, false});
    std::cout.rdbuf(previousBuffer);
    std::istringstream menuRows(menuLayout.str());
    renderedRows = 0;
    while (std::getline(menuRows, renderedLine)) {
        ++renderedRows;
    }
    check(renderedRows == 40 && maximumLineWidth(menuLayout.str()) <= 120 &&
              menuLayout.str().front() == '\n' &&
              menuLayout.str().find("CONWAY PARALLEL") != std::string::npos,
          "main_menu_uses_and_centers_full_terminal_geometry");

    std::ostringstream compactMenu;
    auto* const compactPreviousBuffer = std::cout.rdbuf(compactMenu.rdbuf());
    conway::ui::renderLayout(
        "CONWAY", "Compacto", {}, {{"1", "Benchmark"}, {"0", "Salir"}},
        0, conway::ui::LayoutConfig{30, 10, false});
    std::cout.rdbuf(compactPreviousBuffer);
    check(maximumLineWidth(compactMenu.str()) <= 30 &&
              compactMenu.str().find("Benchmark") != std::string::npos,
          "main_menu_remains_usable_in_compact_terminal");

    const auto temporaryDirectory = std::filesystem::temp_directory_path() /
        ("conway-pattern-catalog-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporaryDirectory);
    writePattern(temporaryDirectory / "zeta.TXT", "2 3\n010\n111\n");
    writePattern(temporaryDirectory / "broken.txt", "not a pattern\n");
    writePattern(temporaryDirectory / "ignored.csv", "ignored\n");
    const auto discovered = conway::app::discoverPatterns(temporaryDirectory);
    check(discovered.error.empty() && discovered.entries.size() == 2 &&
              discovered.entries[0].name == "broken" &&
              !discovered.entries[0].valid() &&
              discovered.entries[1].name == "zeta" &&
              discovered.entries[1].valid() &&
              discovered.entries[1].width == 3 &&
              discovered.entries[1].height == 2 &&
              discovered.entries[1].aliveCells == 4,
          "pattern_catalog_discovers_txt_files_and_validates_metadata");
    writePattern(temporaryDirectory / "new-pattern.txt", "1 1\n1\n");
    const auto refreshed = conway::app::discoverPatterns(temporaryDirectory);
    check(refreshed.entries.size() == 3,
          "pattern_catalog_discovers_files_added_after_initial_scan");
    const auto missingCatalog = conway::app::discoverPatterns(temporaryDirectory / "missing");
    check(!missingCatalog.error.empty(),
          "pattern_catalog_reports_directory_errors");
    std::filesystem::remove_all(temporaryDirectory);

    check(conway::app::parseOpenMPThreadCount("1") == 1 &&
              conway::app::parseOpenMPThreadCount("2") == 2 &&
              conway::app::parseOpenMPThreadCount("4") == 4 &&
              conway::app::parseOpenMPThreadCount("8") == 8 &&
              conway::app::parseOpenMPThreadCount("12") == 12 &&
              conway::app::parseOpenMPThreadCount("64") == 64,
          "thread_configuration_accepts_presets_and_custom_values");
    check(!conway::app::parseOpenMPThreadCount("0") &&
              !conway::app::parseOpenMPThreadCount("65") &&
              !conway::app::parseOpenMPThreadCount("-2") &&
              !conway::app::parseOpenMPThreadCount("two") &&
              !conway::app::parseOpenMPThreadCount("4x"),
          "thread_configuration_rejects_invalid_values");
    const std::string wideUnicode = "A界";
    check(conway::ui::terminal_text::visibleWidth(wideUnicode) == 3 &&
              conway::ui::terminal_text::visibleWidth("\033[42m  \033[0m") == 2 &&
              conway::ui::terminal_text::visibleWidth(
                  conway::ui::terminal_text::fit(wideUnicode, 5, true)) == 5,
          "terminal_layout_counts_display_columns_not_utf8_bytes");

    session.step();
    check(session.game().generation() == 1 && session.game().aliveCount() == 3 &&
              session.game().isAlive(1, 2) && session.game().isAlive(2, 2) &&
              session.game().isAlive(3, 2),
          "manual_step_advances_current");
    session.toggleRunning();
    session.advanceIfRunning();
    check(session.isRunning() && session.game().generation() == 2,
          "running_session_advances_automatically");
    session.toggleRunning();
    session.advanceIfRunning();
    check(!session.isRunning() && session.game().generation() == 2,
          "paused_session_does_not_advance");
    session.reset();
    Game original(input);
    original.setEngine(Engine::OpenMP);
    check(!session.isRunning() && session.game().generation() == 0 &&
              session.game().engine() == Engine::OpenMP && equalBoards(session.game(), original),
          "reset_restores_original_board_and_engine");

    omp_set_dynamic(0);
    const bool threadLimitCase = argc > 1 && std::string(argv[1]) == "--thread-limit-one";
    const int requestedThreads = threadLimitCase ? 4 : 2;
    const int expectedEffectiveThreads = threadLimitCase ? 1 : 2;
    const auto result = conway::app::runBenchmark(input, 8, 3, requestedThreads);
    check(result.equivalent && result.generations == 8 && result.repetitions == 3 &&
              result.requestedThreads == requestedThreads,
          "benchmark_reports_valid_equivalent_results");
    check(result.effectiveThreadCountStable &&
              result.minEffectiveThreads == expectedEffectiveThreads &&
              result.maxEffectiveThreads == expectedEffectiveThreads,
          threadLimitCase
              ? "benchmark_reports_effective_thread_limit"
              : "benchmark_reports_actual_multi_thread_team");
    check(result.efficiencyValid &&
              result.efficiency == result.speedup / static_cast<double>(expectedEffectiveThreads),
          "benchmark_efficiency_uses_effective_threads");
    check(result.sequentialMedianMs >= 0.0 && result.openMPMedianMs >= 0.0 &&
              result.speedup >= 0.0 && result.efficiency >= 0.0,
          "benchmark_reports_nonnegative_metrics");

    const InputData empty{3, 2, std::vector<std::uint8_t>(6, 0)};
    const auto emptyResult = conway::app::runBenchmark(empty, 3, 2, 1);
    SimulationSession emptySession(empty, Engine::Sequential);
    emptySession.step();
    check(emptyResult.equivalent && emptyResult.sequentialMedianMs >= 0.0 &&
              emptySession.game().aliveCount() == 0 && emptySession.game().generation() == 1,
          "empty_pattern_is_supported");

    bool rejectedInvalidInput = false;
    try {
        (void)conway::app::runBenchmark(InputData{0, 0, {}}, 1, 1, 1);
    } catch (const std::invalid_argument&) {
        rejectedInvalidInput = true;
    }
    check(rejectedInvalidInput, "benchmark_rejects_invalid_dimensions");

    std::cout << "Resultado: " << (failures == 0 ? "todo correcto" : "fallos")
              << " (" << failures << " FAIL)\n";
    return failures == 0 ? 0 : 1;
}
