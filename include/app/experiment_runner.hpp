#pragma once

#include "app/benchmark.hpp"
#include "input/generated_input.hpp"

#include <cstdint>
#include <string>

namespace conway::app {

enum class MeasurementOrder {
    AlternatingSequentialFirst,
    AlternatingOpenMPFirst
};

struct ExperimentConfig {
    int generations{20};
    int warmups{1}; // Per engine; may be zero.
    int repetitions{5}; // Measured rounds, one sample per engine per round.
    int requestedThreads{1};
    MeasurementOrder order{MeasurementOrder::AlternatingSequentialFirst};
};

struct GeneratedInputIdentity {
    std::uint32_t seed;
    int requestedDensity;
    double observedDensity;
    std::string generatorVersion; // Owned: results outlive the generated input.
};

struct ExperimentInputIdentity {
    std::size_t width;
    std::size_t height;
    std::size_t aliveCount;
    std::uint64_t fingerprint; // FNV-1a-64 over dimensions and cells; not cryptographic.
    std::optional<GeneratedInputIdentity> generated;
};

struct ExperimentSample {
    BenchmarkSample measurement;
    int roundIndex; // Zero-based measured round, also the engine's repetitionIndex.
    int positionInRound; // 0/1 for runExperiment; actual position for scaling.
    // Global run position: oracle is 0, then warmup runs, then measured runs.
    std::size_t executionIndex;

    bool validForComparison() const noexcept;
};

struct ExperimentSummary {
    std::optional<BenchmarkStatistics> sequentialStatistics;
    std::optional<BenchmarkStatistics> openMPStatistics;
    bool equivalent{false};
    bool validForComparison{false};
    std::optional<int> minEffectiveThreads; // Measured OpenMP runs only.
    std::optional<int> maxEffectiveThreads;
    bool effectiveThreadCountStable{false};
    // Absent on invalid comparison or zero median (insufficient resolution).
    std::optional<double> speedup;
    // Also absent for varying effective teams; never uses requested threads.
    std::optional<double> efficiency;
};

// Pure summarization, without timing or OpenMP configuration changes.
// Invalid observations are retained by the caller and cannot yield valid metrics.
ExperimentSummary summarizeExperimentSamples(std::span<const ExperimentSample> samples);

struct ExperimentResult {
    ExperimentConfig config;
    ExperimentInputIdentity input;
    std::size_t completedWarmupRuns;
    std::vector<ExperimentSample> samples; // Actual acquisition order; no warmups.
    ExperimentSummary summary;
};

// Both overloads borrow the same input throughout; no regeneration or board copy.
// Validate parameters before any Game execution. OpenMP ICVs restored on all exits.
ExperimentResult runExperiment(const InputData& input, const ExperimentConfig& config);
ExperimentResult runExperiment(const input::GeneratedInput& input, const ExperimentConfig& config);

} // namespace conway::app
