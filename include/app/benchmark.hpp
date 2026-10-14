#pragma once

#include "core/game.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace conway::app {

struct BenchmarkSample {
    Engine engine;
    int repetitionIndex; // Zero-based, within this engine's measured series.
    double elapsedMs; // Total batch wall time, unrounded; excludes warmup.
    int generations;
    std::optional<int> requestedThreads; // Absent for sequential execution.
    int minEffectiveThreads;
    int maxEffectiveThreads;
    bool equivalent; // This execution compared with the sequential reference.
};

struct BenchmarkStatistics {
    double minimumMs;
    double maximumMs;
    double medianMs;
    double meanMs;
    double populationStddevMs; // Divisor N; one sample has deviation zero.
};

// Empty: nullopt. Negative or non-finite times: invalid_argument.
// Does not mutate or reorder the caller's observations. Zero is a valid time.
std::optional<BenchmarkStatistics> calculateBenchmarkStatistics(std::span<const double> times);

struct BenchmarkResult {
    double sequentialMedianMs{0.0};
    double openMPMedianMs{0.0};
    double speedup{0.0};
    double efficiency{0.0};
    int requestedThreads{1};
    int minEffectiveThreads{1};
    int maxEffectiveThreads{1};
    bool effectiveThreadCountStable{true};
    bool efficiencyValid{false};
    int generations{0};
    int repetitions{0};
    bool equivalent{false};
    // Acquisition order: sequential series followed by OpenMP; no warmups.
    std::vector<BenchmarkSample> samples;
    std::optional<BenchmarkStatistics> sequentialStatistics;
    std::optional<BenchmarkStatistics> openMPStatistics;
};

BenchmarkResult runBenchmark(
    const InputData& input,
    int generations,
    int repetitions,
    int threads
);

int benchmarkThreadCount() noexcept;

} // namespace conway::app
