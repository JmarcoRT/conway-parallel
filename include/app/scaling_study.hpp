#pragma once

#include "app/experiment_runner.hpp"

namespace conway::app {

enum class ScalingOrder { RotateLeft, RotateRight };

struct ScalingConfig {
    int generations{20};
    int warmups{1}; // Per configuration, including Sequential; zero permitted.
    int repetitions{5}; // One Sequential and each OpenMP configuration per round.
    // Wider than OpenMP's int so out-of-range requests can be rejected safely.
    std::vector<std::int64_t> requestedThreads{1, 2, 4, 8};
    ScalingOrder order{ScalingOrder::RotateLeft};
    // Payload policy, adjustable; includes input, Games, results and summary workspace.
    std::size_t maxMemoryBytes{256u * 1024u * 1024u};
    Engine parallelEngine{Engine::OpenMP};
    std::vector<std::int64_t> requestedProcesses{1, 2, 4};
    mpi::ClientOptions mpiOptions;
};

struct ScalingSample {
    ExperimentSample execution;
    std::size_t configurationIndex; // 0=Sequential; i+1=requestedThreads[i].
    std::size_t sequentialReferenceIndex; // Index into the one shared sample vector.

    // Includes the same round's Sequential reference validity, without inferred teams.
    bool validForComparison(const ScalingSample& sequentialReference) const noexcept;
};

struct ScalingPoint {
    int requestedThreads;
    ExperimentSummary summary; // Shared Sequential series versus this OpenMP series.
};

struct ScalingSummary {
    std::optional<BenchmarkStatistics> sequentialStatistics;
    std::vector<ScalingPoint> points; // Configuration-list order, not execution order.
    bool equivalent{false};
    bool validForComparison{false};
};

// Pure summarization. Rejects malformed audit structure/configuration; invalid
// observations keep their evidence and yield unavailable comparison metrics.
ScalingSummary summarizeScalingSamples(std::span<const ScalingSample> samples,
                                      const ScalingConfig& config);

struct ScalingResult {
    ScalingConfig config;
    ExperimentInputIdentity input;
    std::size_t estimatedPeakBytes; // Payload estimate; excludes allocator/runtime overhead.
    std::size_t completedWarmupRuns;
    std::vector<ScalingSample> samples; // Actual order; shared Sequential samples stored once.
    ScalingSummary summary;
    std::optional<ExperimentalStudy> study; // MPI study, CSV v2.
};

ScalingResult runScalingStudy(const InputData& input, const ScalingConfig& config);
ScalingResult runScalingStudy(const input::GeneratedInput& input, const ScalingConfig& config);

} // namespace conway::app
