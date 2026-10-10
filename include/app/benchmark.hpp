#pragma once

#include "core/game.hpp"

#include <cstddef>

namespace conway::app {

struct BenchmarkResult {
    double sequentialMedianMs{0.0};
    double openMPMedianMs{0.0};
    double speedup{0.0};
    double efficiency{0.0};
    int threads{1};
    int generations{0};
    int repetitions{0};
    bool equivalent{false};
};

BenchmarkResult runBenchmark(
    const InputData& input,
    int generations,
    int repetitions,
    int threads
);

int benchmarkThreadCount() noexcept;

} // namespace conway::app
