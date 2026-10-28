#include "app/scaling_study.hpp"
#include "benchmark_detail.hpp"
#include "experiment_input_detail.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace conway::app {
namespace {
using detail::checkedAdd;
using detail::checkedMultiply;

struct StudyShape {
    std::size_t configurations;
    std::size_t samples;
    std::size_t warmupRuns;
    std::size_t auxiliaryBytes;
};

StudyShape validateConfig(const ScalingConfig& config) {
    if (config.generations <= 0 || config.warmups < 0 || config.repetitions <= 0
        || config.requestedThreads.empty() || !config.maxMemoryBytes)
        throw std::invalid_argument("Invalid scaling generations, warmups, repetitions, threads or budget");
    if (config.order != ScalingOrder::RotateLeft && config.order != ScalingOrder::RotateRight)
        throw std::invalid_argument("Unknown scaling order policy");
    const auto threads = config.requestedThreads.size();
    // ExperimentSample's positionInRound uses int, as does the OpenMP API.
    if (threads >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Scaling configuration count exceeds position index range");
    const auto configurations = checkedAdd(threads, 1);
    const auto repetitions = static_cast<std::size_t>(config.repetitions);
    const auto samples = checkedMultiply(configurations, repetitions);
    const auto warmupRuns = checkedMultiply(configurations, static_cast<std::size_t>(config.warmups));
    (void)checkedAdd(1, checkedAdd(samples, warmupRuns));
    const auto pairedCount = checkedMultiply(repetitions, 2);
    if (samples > std::vector<ScalingSample>{}.max_size()
        || threads > std::vector<ScalingPoint>{}.max_size()
        || pairedCount > std::vector<ExperimentSample>{}.max_size())
        throw std::length_error("Scaling records exceed vector capacity");

    // Conservative payload accounting, including workspaces from different phases.
    auto bytes = checkedAdd(sizeof(ScalingResult), checkedMultiply(samples, sizeof(ScalingSample)));
    bytes = checkedAdd(bytes, checkedMultiply(threads, sizeof(ScalingPoint)));
    // Caller config, returned config and temporary sorted validation copy.
    bytes = checkedAdd(bytes, checkedMultiply(checkedAdd(config.requestedThreads.capacity(), checkedMultiply(threads, 2)),
                                               sizeof(std::int64_t)));
    // One paired summary at a time: records, two time series and one sorted copy.
    bytes = checkedAdd(bytes, checkedMultiply(pairedCount, sizeof(ExperimentSample)));
    bytes = checkedAdd(bytes, checkedMultiply(checkedMultiply(repetitions, 3), sizeof(double)));
    if (bytes > config.maxMemoryBytes)
        throw std::length_error("Scaling records exceed memory budget");
    for (auto value : config.requestedThreads) {
        if (value <= 0 || value > std::numeric_limits<int>::max())
            throw std::invalid_argument("Scaling thread request must fit a positive OpenMP int");
    }
    auto sorted = config.requestedThreads;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw std::invalid_argument("Duplicate scaling thread request");
    return {configurations, samples, warmupRuns, bytes};
}

std::size_t configurationAt(ScalingOrder order, std::size_t round,
                            std::size_t position, std::size_t count) {
    const auto offset = round % count;
    return order == ScalingOrder::RotateLeft ? (position + offset) % count
                                             : (position + count - offset) % count;
}
std::size_t positionOf(ScalingOrder order, std::size_t round,
                      std::size_t configuration, std::size_t count) {
    const auto offset = round % count;
    return order == ScalingOrder::RotateLeft ? (configuration + count - offset) % count
                                             : (configuration + offset) % count;
}

ScalingResult execute(const InputData& input, const ScalingConfig& config,
                      const input::GenerationMetadata* generated) {
    if(config.parallelEngine==Engine::MPI) {
        if(config.requestedProcesses.empty() ||
           (config.order!=ScalingOrder::RotateLeft && config.order!=ScalingOrder::RotateRight))
            throw std::invalid_argument("Invalid MPI scaling configurations/order");
        std::vector<ExecutionConfiguration> configurations{{Engine::Sequential,{},{}}};
        for(auto p:config.requestedProcesses) {
            if(p<1 || p>1024) throw std::invalid_argument("MPI processes must be in 1..1024");
            configurations.push_back({Engine::MPI,{},static_cast<int>(p)});
        }
        auto identity=detail::validateInput(input);
        detail::attachGeneratedMetadata(identity,input,generated);
        ScalingResult result{config,std::move(identity),0,0,{},{}};
        result.study=runExperimentalStudy(input,std::move(configurations),config.generations,
            config.warmups,config.repetitions,config.order==ScalingOrder::RotateRight,
            config.mpiOptions,config.maxMemoryBytes);
        result.completedWarmupRuns=result.study->completedWarmupRuns;
        result.estimatedPeakBytes=result.study->estimatedPeakBytes;
        return result;
    }
    if(config.parallelEngine!=Engine::OpenMP) throw std::invalid_argument("Scaling requires OpenMP or MPI");
    const auto shape = validateConfig(config);
    auto identity = detail::validateInput(input);
    const auto padded = checkedMultiply(checkedAdd(input.width, 2), checkedAdd(input.height, 2));
    auto estimate = checkedAdd(shape.auxiliaryBytes,
                              checkedAdd(input.cells.capacity(), checkedMultiply(4, padded)));
    if (generated) estimate = checkedAdd(estimate, checkedAdd(generated->version.size(), 1));
    if (estimate > config.maxMemoryBytes)
        throw std::length_error("Scaling total payload exceeds memory budget");
    detail::attachGeneratedMetadata(identity, input, generated);
    ScalingResult result{config, std::move(identity), estimate, 0, {}, {}};
    result.samples.reserve(shape.samples);

    detail::OpenMPSettingsGuard settings;
    omp_set_dynamic(0);
    // Correctness oracle only, never a performance sample. Enables rotated rounds
    // without retaining a final Game for every OpenMP configuration.
    const auto oracle = detail::runOnce(input, Engine::Sequential, 1, config.generations);
    for (int round = 0; round < config.warmups; ++round) {
        for (std::size_t position = 0; position < shape.configurations; ++position) {
            const auto index = configurationAt(config.order, static_cast<std::size_t>(round),
                                                position, shape.configurations);
            const auto engine = index ? Engine::OpenMP : Engine::Sequential;
            const int threads = index ? static_cast<int>(config.requestedThreads[index - 1]) : 1;
            const auto warmed=detail::runOnce(input, engine, threads, config.generations);
            if(!detail::sameState(warmed.finalGame,oracle.finalGame))
                throw std::runtime_error("Warmup differs from sequential oracle");
            ++result.completedWarmupRuns;
        }
    }
    std::size_t executionIndex = 1 + result.completedWarmupRuns;
    for (int round = 0; round < config.repetitions; ++round) {
        const auto roundIndex = static_cast<std::size_t>(round);
        const auto base = roundIndex * shape.configurations;
        const auto referenceIndex = base + positionOf(config.order, roundIndex, 0, shape.configurations);
        for (std::size_t position = 0; position < shape.configurations; ++position) {
            const auto index = configurationAt(config.order, roundIndex, position, shape.configurations);
            const auto engine = index ? Engine::OpenMP : Engine::Sequential;
            const int threads = index ? static_cast<int>(config.requestedThreads[index - 1]) : 1;
            auto run = detail::runOnce(input, engine, threads, config.generations);
            const bool equivalent = detail::sameState(run.finalGame, oracle.finalGame);
            result.samples.push_back({
                {{engine, round, run.elapsedMs, config.generations,
                  index ? std::optional<int>{threads} : std::nullopt,
                  run.minEffectiveThreads, run.maxEffectiveThreads, equivalent},
                 round, static_cast<int>(position), executionIndex++},
                index, referenceIndex});
        }
    }
    result.summary = summarizeScalingSamples(result.samples, config);
    return result;
}
} // namespace

bool ScalingSample::validForComparison(const ScalingSample& reference) const noexcept {
    return reference.configurationIndex == 0
        && reference.execution.measurement.engine == Engine::Sequential
        && reference.execution.roundIndex == execution.roundIndex
        && reference.execution.measurement.generations == execution.measurement.generations
        && reference.execution.validForComparison() && execution.validForComparison();
}

ScalingSummary summarizeScalingSamples(std::span<const ScalingSample> samples,
                                      const ScalingConfig& config) {
    const auto shape = validateConfig(config);
    if (samples.size() != shape.samples)
        throw std::invalid_argument("Scaling audit has an incorrect sample count");
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto round = index / shape.configurations;
        const auto position = index % shape.configurations;
        const auto expected = configurationAt(config.order, round, position, shape.configurations);
        const auto& sample = samples[index];
        const auto& execution = sample.execution;
        const auto referenceIndex = round * shape.configurations
            + positionOf(config.order, round, 0, shape.configurations);
        const auto requested = expected
            ? std::optional<int>{static_cast<int>(config.requestedThreads[expected - 1])} : std::nullopt;
        if (sample.configurationIndex != expected || sample.sequentialReferenceIndex != referenceIndex
            || execution.roundIndex != static_cast<int>(round)
            || execution.positionInRound != static_cast<int>(position)
            || execution.executionIndex != 1 + shape.warmupRuns + index
            || execution.measurement.repetitionIndex != static_cast<int>(round)
            || execution.measurement.engine != (expected ? Engine::OpenMP : Engine::Sequential)
            || execution.measurement.requestedThreads != requested
            || execution.measurement.generations != config.generations)
            throw std::invalid_argument("Scaling audit does not match declared configuration/order");
    }
    ScalingSummary result;
    result.equivalent = result.validForComparison = true;
    result.points.reserve(config.requestedThreads.size());
    std::vector<ExperimentSample> paired;
    paired.reserve(checkedMultiply(static_cast<std::size_t>(config.repetitions), 2));
    for (std::size_t point = 1; point < shape.configurations; ++point) {
        paired.clear();
        for (int round = 0; round < config.repetitions; ++round) {
            const auto r = static_cast<std::size_t>(round);
            const auto base = r * shape.configurations;
            const auto referenceIndex = base + positionOf(config.order, r, 0, shape.configurations);
            const auto parallelIndex = base + positionOf(config.order, r, point, shape.configurations);
            const auto first = std::min(referenceIndex, parallelIndex);
            const auto second = std::max(referenceIndex, parallelIndex);
            paired.push_back(samples[first].execution);
            paired.push_back(samples[second].execution);
        }
        auto summary = summarizeExperimentSamples(paired);
        result.equivalent = result.equivalent && summary.equivalent;
        result.validForComparison = result.validForComparison && summary.validForComparison;
        result.points.push_back({static_cast<int>(config.requestedThreads[point - 1]), std::move(summary)});
    }
    result.sequentialStatistics = result.points.front().summary.sequentialStatistics;
    return result;
}

ScalingResult runScalingStudy(const InputData& input, const ScalingConfig& config) {
    return execute(input, config, nullptr);
}
ScalingResult runScalingStudy(const input::GeneratedInput& input, const ScalingConfig& config) {
    return execute(input.input, config, &input.metadata);
}
} // namespace conway::app
