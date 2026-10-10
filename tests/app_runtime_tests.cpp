#include "app/benchmark.hpp"
#include "app/simulation.hpp"
#include "core/game.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"

#include <omp.h>

#include <cstdint>
#include <iostream>
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
