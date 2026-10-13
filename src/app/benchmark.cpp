#include "app/benchmark.hpp"

#include "core/engines/openmp.hpp"

#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    int minEffectiveThreads;
    int maxEffectiveThreads;
};

TimedRun runOnce(const InputData& input, Engine engine, int threads, int generations) {
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
    bool& equivalent,
    int& minEffectiveThreads,
    int& maxEffectiveThreads,
    std::vector<BenchmarkSample>& samples
) {
    const TimedRun warmup = runOnce(input, engine, threads, generations);
    if (engine == Engine::OpenMP) {
        minEffectiveThreads = std::min(minEffectiveThreads, warmup.minEffectiveThreads);
        maxEffectiveThreads = std::max(maxEffectiveThreads, warmup.maxEffectiveThreads);
    }
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(repetitions));
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        TimedRun run = runOnce(input, engine, threads, generations);
        times.push_back(run.elapsedMs);
        const bool runEquivalent = sameState(run.finalGame, reference);
        equivalent = equivalent && runEquivalent;
        samples.push_back({engine, repetition, run.elapsedMs, generations,
                           engine == Engine::OpenMP ? std::optional<int>{threads} : std::nullopt,
                           run.minEffectiveThreads, run.maxEffectiveThreads, runEquivalent});
        if (engine == Engine::OpenMP) {
            minEffectiveThreads = std::min(minEffectiveThreads, run.minEffectiveThreads);
            maxEffectiveThreads = std::max(maxEffectiveThreads, run.maxEffectiveThreads);
        }
    }
    return times;
}

} // namespace

std::optional<BenchmarkStatistics> calculateBenchmarkStatistics(std::span<const double> times) {
    if (times.empty()) return std::nullopt;
    for (double value : times) {
        if (!std::isfinite(value) || value < 0.0)
            throw std::invalid_argument("Benchmark times must be finite and nonnegative");
    }
    // Scale before accumulating moments: avoids overflow even for DBL_MAX data.
    const auto [minimum, maximum] = std::minmax_element(times.begin(), times.end());
    const long double scale = *maximum;
    long double mean = 0.0L;
    long double squaredDeviations = 0.0L;
    std::size_t count = 0;
    for (double value : times) {
        const long double normalized = scale > 0 ? value / scale : 0.0L;
        const long double delta = normalized - mean;
        mean += delta / static_cast<long double>(++count);
        squaredDeviations += delta * (normalized - mean);
    }
    std::vector<double> sorted(times.begin(), times.end());
    std::sort(sorted.begin(), sorted.end());
    const auto middle = sorted.size() / 2;
    const double centralSum = sorted.size() % 2 ? 0.0 : sorted[middle - 1] + sorted[middle];
    const double medianValue = sorted.size() % 2 ? sorted[middle]
        : std::isfinite(centralSum) ? centralSum / 2.0
        : sorted[middle - 1] + (sorted[middle] - sorted[middle - 1]) / 2.0;
    return BenchmarkStatistics{*minimum, *maximum, medianValue,
        static_cast<double>(mean * scale),
        static_cast<double>(std::sqrt(std::max(0.0L, squaredDeviations / count)) * scale)};
}

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

    std::vector<BenchmarkSample> samples;
    samples.reserve(static_cast<std::size_t>(repetitions) * 2);
    bool equivalent = true;
    const TimedRun sequential = runOnce(input, Engine::Sequential, 1, generations);
    int minEffectiveThreads = std::numeric_limits<int>::max();
    int maxEffectiveThreads = 0;
    const auto sequentialTimes = measure(
        input, Engine::Sequential, 1, generations, repetitions, sequential.finalGame, equivalent,
        minEffectiveThreads, maxEffectiveThreads, samples);
    const auto openMPTimes = measure(
        input, Engine::OpenMP, threads, generations, repetitions, sequential.finalGame, equivalent,
        minEffectiveThreads, maxEffectiveThreads, samples);

    const double sequentialMedian = median(sequentialTimes);
    const double openMPMedian = median(openMPTimes);
    const double speedup = openMPMedian > 0.0 ? sequentialMedian / openMPMedian : 0.0;
    const bool threadCountStable = minEffectiveThreads == maxEffectiveThreads;
    const bool efficiencyValid = threadCountStable && minEffectiveThreads > 0 &&
        openMPMedian > 0.0 && equivalent;
    const double efficiency = efficiencyValid
        ? speedup / static_cast<double>(minEffectiveThreads)
        : 0.0;

    auto sequentialStatistics = calculateBenchmarkStatistics(sequentialTimes);
    auto openMPStatistics = calculateBenchmarkStatistics(openMPTimes);

    return {
        sequentialMedian,
        openMPMedian,
        speedup,
        efficiency,
        threads,
        minEffectiveThreads,
        maxEffectiveThreads,
        threadCountStable,
        efficiencyValid,
        generations,
        repetitions,
        equivalent,
        std::move(samples),
        sequentialStatistics,
        openMPStatistics
    };
}

} // namespace conway::app
