#include "app/benchmark.hpp"

#include <omp.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace conway::app {

namespace {

using Clock = std::chrono::steady_clock;

struct TimedRun {
    double elapsedMs;
    Game finalGame;
};

TimedRun runOnce(const InputData& input, Engine engine, int threads, int generations) {
    Game game(input);
    game.setEngine(engine);
    if (engine == Engine::OpenMP) {
        omp_set_num_threads(threads);
    }

    const auto start = Clock::now();
    for (int generation = 0; generation < generations; ++generation) {
        game.step();
    }
    const auto finish = Clock::now();

    return {
        std::chrono::duration<double, std::milli>(finish - start).count(),
        std::move(game)
    };
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

bool sameState(const Game& lhs, const Game& rhs) {
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

std::vector<double> measure(
    const InputData& input,
    Engine engine,
    int threads,
    int generations,
    int repetitions,
    const Game& reference,
    bool& equivalent
) {
    (void)runOnce(input, engine, threads, generations);
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(repetitions));
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        TimedRun run = runOnce(input, engine, threads, generations);
        times.push_back(run.elapsedMs);
        equivalent = equivalent && sameState(run.finalGame, reference);
    }
    return times;
}

} // namespace

int benchmarkThreadCount() noexcept {
    return std::max(1, std::min(8, omp_get_max_threads()));
}

BenchmarkResult runBenchmark(
    const InputData& input,
    int generations,
    int repetitions,
    int threads
) {
    if (input.width == 0 || input.height == 0 ||
        input.width > std::numeric_limits<std::size_t>::max() / input.height ||
        input.cells.size() != input.width * input.height) {
        throw std::invalid_argument("La entrada del benchmark no tiene dimensiones o celdas validas");
    }
    if (generations <= 0 || repetitions <= 0 || threads <= 0) {
        throw std::invalid_argument("Generaciones, repeticiones e hilos deben ser mayores que cero");
    }

    OpenMPSettingsGuard settings;
    omp_set_dynamic(0);

    bool equivalent = true;
    const TimedRun sequential = runOnce(input, Engine::Sequential, 1, generations);
    const auto sequentialTimes = measure(
        input, Engine::Sequential, 1, generations, repetitions, sequential.finalGame, equivalent);
    const auto openMPTimes = measure(
        input, Engine::OpenMP, threads, generations, repetitions, sequential.finalGame, equivalent);

    const double sequentialMedian = median(sequentialTimes);
    const double openMPMedian = median(openMPTimes);
    const double speedup = openMPMedian > 0.0 ? sequentialMedian / openMPMedian : 0.0;

    return {
        sequentialMedian,
        openMPMedian,
        speedup,
        speedup / static_cast<double>(threads),
        threads,
        generations,
        repetitions,
        equivalent
    };
}

} // namespace conway::app
