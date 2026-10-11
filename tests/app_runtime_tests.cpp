#include "app/benchmark.hpp"
#include "app/simulation.hpp"
#include "app/thread_config.hpp"
#include "core/game.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"
#include "ui/simulation_layout.hpp"

#include <omp.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

} // namespace

int main(int argc, char** argv) {
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
    check(monochrome.size() == 5 && monochrome[0] == "[ ][ ][ ]" &&
              monochrome[2].find("[#]") != std::string::npos,
          "board_view_renders_distinct_grid_cells_and_clips_large_boards");
    check(monochrome[3].find("columnas omitidas") != std::string::npos &&
              monochrome[4].find("filas omitidas") != std::string::npos,
          "board_view_reports_hidden_dimensions");
    const auto colored = conway::ui::renderBoardLines(
        session.game(), conway::ui::BoardRenderOptions{5, 5, true});
    check(colored[2].find("\033[1;32m[#]\033[0m") != std::string::npos,
          "board_view_can_render_ansi_colors");

    std::ostringstream wideLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{82, 4, false}, wideLayout);
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
    std::ostringstream tinyLayout;
    conway::ui::renderSimulationLayout(
        {}, {}, {"CURRENT", monochrome}, {"NEXT", monochrome},
        conway::ui::SimulationLayoutConfig{1, 2, false}, tinyLayout);
    check(maximumLineWidth(tinyLayout.str()) <= 1,
          "simulation_layout_handles_tiny_terminal_without_overflow");

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
