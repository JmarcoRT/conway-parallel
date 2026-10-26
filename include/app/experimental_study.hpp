#pragma once
#include "app/benchmark.hpp"
#include "mpi/client.hpp"
#include <string>

namespace conway::app {
// Independent resources: MPI processes are never represented as OpenMP threads.
struct ExecutionConfiguration {
    Engine engine{Engine::Sequential};
    std::optional<int> requestedThreads;
    std::optional<int> requestedProcesses;
    bool operator==(const ExecutionConfiguration&) const = default;
};
struct StudySample {
    std::size_t configurationIndex{};
    int round{}, position{};
    std::size_t executionIndex{}, completedWarmups{};
    double elapsedMs{};
    std::optional<int> minEffectiveThreads, maxEffectiveThreads, observedProcesses;
    std::optional<std::size_t> sessionIndex;
    std::optional<double> loadMs, snapshotMs, requestMs;
    bool equivalent{}, valid{};
};
struct StudySession {
    std::size_t configurationIndex{};
    int round{}, declaredSlots{};
    double startupMs{}, closeMs{};
    std::size_t completedWarmups{};
    std::vector<int> workerPids;
    std::string runtime, diagnostic;
    bool cleanupConfirmed{};
};
struct StudySummary {
    std::size_t configurationIndex{}, sampleCount{};
    std::optional<BenchmarkStatistics> statistics;
    std::optional<int> minimumResources, maximumResources;
    std::optional<double> speedup, efficiency;
    bool equivalent{}, valid{};
};
struct ExperimentalStudy {
    std::vector<ExecutionConfiguration> configurations;
    std::vector<StudySample> samples;
    std::vector<StudySession> sessions;
    std::vector<StudySummary> summaries;
    int generations{}, warmups{}, repetitions{};
    bool rotateRight{}, complete{}, cleanupConfirmed{true};
    std::size_t completedWarmupRuns{}, completedLocalWarmups{}, estimatedPeakBytes{}, maxMemoryBytes{};
    mpi::ClientOptions mpiOptions;
    std::string diagnostic;
};
// Empty launcher/worker resolved from the configured runtime and sibling worker.
mpi::ClientOptions experimentalMPIOptions();
// Internal execution result retains partial evidence on operational failures.
// Invalid input/configuration throws before starting processes. Never retries.
ExperimentalStudy runExperimentalStudy(const InputData&, std::vector<ExecutionConfiguration>,
    int generations, int warmups, int repetitions, bool rotateRight,
    mpi::ClientOptions options={}, std::size_t maxMemoryBytes=256u*1024u*1024u);
std::vector<StudySummary> summarizeStudy(const ExperimentalStudy&);
void validateStudy(const ExperimentalStudy&);
std::string configurationName(const ExecutionConfiguration&);
} // namespace conway::app
