#pragma once

// Private helpers shared by the legacy benchmark and the experimental runner.
#include "core/game.hpp"
#include "core/engines/openmp.hpp"
#include <omp.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace conway::app::detail {

using Clock = std::chrono::steady_clock;

struct TimedRun {
    double elapsedMs;
    Game finalGame;
    int minEffectiveThreads;
    int maxEffectiveThreads;
};

inline TimedRun runOnce(const InputData& input, Engine engine, int threads, int generations) {
    Game game(input);
    game.setEngine(engine);
    if (engine == Engine::OpenMP) {
        omp_set_num_threads(threads);
    }

    const auto start = Clock::now();
    int minEffectiveThreads = std::numeric_limits<int>::max();
    int maxEffectiveThreads = 0;
    for (int generation = 0; generation < generations; ++generation) {
        game.step();
        if (engine == Engine::OpenMP) {
            const int actualThreads = core::last_openmp_team_size();
            minEffectiveThreads = std::min(minEffectiveThreads, actualThreads);
            maxEffectiveThreads = std::max(maxEffectiveThreads, actualThreads);
        }
    }
    const auto finish = Clock::now();

    return {
        std::chrono::duration<double, std::milli>(finish - start).count(),
        std::move(game),
        engine == Engine::OpenMP ? minEffectiveThreads : 1,
        engine == Engine::OpenMP ? maxEffectiveThreads : 1
    };
}

inline bool sameState(const Game& lhs, const Game& rhs) {
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

struct OpenMPSettingsGuard {
    int threads{omp_get_max_threads()};
    int dynamic{omp_get_dynamic()};

    ~OpenMPSettingsGuard() {
        omp_set_num_threads(threads);
        omp_set_dynamic(dynamic);
    }
};

} // namespace conway::app::detail
